


#include "render_tiles.h"

#include "config.h"
#include "il2cpp.h"
#include "imgui.h"

#include <windows.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>

extern int g_viewW, g_viewH;

namespace render_tiles {
namespace {

constexpr float kViewWidthScale = 0.8125f;
constexpr int kRadius = 18;
constexpr int kMaxDimension = 4096;
constexpr int kMaxTiles = (kRadius * 2 + 1) * (kRadius * 2 + 1);

struct Matrix {
    float c[4][4];
};

bool Readable(uintptr_t address, size_t size) {
    if (address < 0x10000 || !size || address > UINTPTR_MAX - size)
        return false;
    uintptr_t cursor = address;
    const uintptr_t end = address + size;
    while (cursor < end) {
        MEMORY_BASIC_INFORMATION mbi{};
        if (!VirtualQuery(reinterpret_cast<const void*>(cursor), &mbi, sizeof(mbi)))
            return false;
        const DWORD blocked = PAGE_NOACCESS | PAGE_GUARD;
        if (mbi.State != MEM_COMMIT || (mbi.Protect & blocked))
            return false;
        const uintptr_t regionEnd =
            reinterpret_cast<uintptr_t>(mbi.BaseAddress) + mbi.RegionSize;
        if (regionEnd <= cursor)
            return false;
        cursor = std::min(regionEnd, end);
    }
    return true;
}

template <typename T>
bool Read(uintptr_t address, T& out) {
    if (!Readable(address, sizeof(T)))
        return false;
    std::memcpy(&out, reinterpret_cast<const void*>(address), sizeof(T));
    return true;
}

bool Camera(Matrix& out) {
    return game::CameraMatrix(out.c);
}

bool Project(const Matrix& m, float x, float y, ImVec2& out) {
    y = -y;
    const float w = m.c[0][3] * x + m.c[1][3] * y + m.c[3][3];
    if (!std::isfinite(w) || w < 0.098f)
        return false;
    const float width = kViewWidthScale * static_cast<float>(g_viewW);
    const float height = static_cast<float>(g_viewH);
    if (!(width > 0.0f) || !(height > 0.0f))
        return false;
    const float clipX = m.c[0][0] * x + m.c[1][0] * y + m.c[3][0];
    const float clipY = m.c[0][1] * x + m.c[1][1] * y + m.c[3][1];
    out.x = (clipX / w + 1.0f) * width * 0.5f;
    out.y = (1.0f - clipY / w) * height * 0.5f;
    return std::isfinite(out.x) && std::isfinite(out.y);
}

ImU32 TileColor(uintptr_t square) {
    if (!square)
        return IM_COL32(255, 70, 70, 105);

    int type = 37;
    uintptr_t object = 0, props = 0;
    Read(square + 0x44, type);
    Read(square + 0x48, object);
    Read(square + 0x50, props);

    bool blocked = type == 5 || type == 34;
    bool damaging = false;
    if (object) {
        uintptr_t status = 0;
        uint8_t fullOccupy = 0;
        if (Read(object + 0x18, status) && status)
            Read(status + 0x6A2, fullOccupy);
        blocked |= fullOccupy != 0;
    }
    if (props) {
        uint8_t value = 0;
        if (Read(props + 0x104, value))
            damaging = value != 0;
    }

    if (damaging) return IM_COL32(255, 145, 35, 125);
    if (blocked)  return IM_COL32(255, 65, 65, 120);
    return IM_COL32(80, 210, 125, 70);
}

}

void Tick() {
    if (!g_cfg.renderTiles || !ImGui::GetCurrentContext())
        return;

    uintptr_t world = game::Root(), squares = 0;
    uintptr_t player = game::Player();
    int width = 0, height = 0;
    float px = 0.0f, py = 0.0f;
    if (!world || !player ||
        !Read(world + ga::off::WORLD_TILE_GRID, squares) || !squares ||
        !Read(world + ga::off::WORLD_MAP_WIDTH, width) ||
        !Read(world + ga::off::WORLD_MAP_HEIGHT, height) ||
        width <= 0 || height <= 0 || width > kMaxDimension ||
        height > kMaxDimension || !Read(player + 0x3C, px) ||
        !Read(player + 0x40, py) || !std::isfinite(px) || !std::isfinite(py))
        return;

    Matrix matrix{};
    if (!Camera(matrix))
        return;

    const int centerX = static_cast<int>(std::floor(px));
    const int centerY = static_cast<int>(std::floor(py));
    const int minX = std::max(0, centerX - kRadius);
    const int maxX = std::min(width - 1, centerX + kRadius);
    const int minY = std::max(0, centerY - kRadius);
    const int maxY = std::min(height - 1, centerY + kRadius);
    if (minX > maxX || minY > maxY ||
        (maxX - minX + 1) * (maxY - minY + 1) > kMaxTiles)
        return;

    ImDrawList* draw = ImGui::GetBackgroundDrawList();
    if (!draw)
        return;

    for (int y = minY; y <= maxY; ++y) {
        for (int x = minX; x <= maxX; ++x) {
            const uint64_t index =
                static_cast<uint64_t>(x) + static_cast<uint64_t>(height) * y;
            uintptr_t square = 0;
            if (!Read(squares + 0x20 + sizeof(uintptr_t) * index, square))
                return;

            ImVec2 p[4];
            if (!Project(matrix, static_cast<float>(x),     static_cast<float>(y),     p[0]) ||
                !Project(matrix, static_cast<float>(x + 1), static_cast<float>(y),     p[1]) ||
                !Project(matrix, static_cast<float>(x + 1), static_cast<float>(y + 1), p[2]) ||
                !Project(matrix, static_cast<float>(x),     static_cast<float>(y + 1), p[3]))
                continue;

            const ImU32 color = TileColor(square);
            draw->AddQuadFilled(p[0], p[1], p[2], p[3], color);
            draw->AddQuad(p[0], p[1], p[2], p[3],
                          IM_COL32(235, 235, 235, 90), 1.0f);
        }
    }
}

}
