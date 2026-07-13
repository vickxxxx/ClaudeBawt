#include "render_units_grid.h"

#include "config.h"
#include "il2cpp.h"
#include "imgui.h"

#include <windows.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <vector>

extern int g_viewW, g_viewH;

namespace render_units_grid {
namespace {

constexpr float kProjectionWidthScale = 0.8125f;
constexpr float kNearPlane = 0.098f;
constexpr float kGridRadius = 18.0f;
constexpr uint32_t kMaxObjects = 20000;

struct Matrix {
    float column[4][4]{};
};

struct Unit {
    float x;
    float y;
    int type;
    int hp;
    bool targetable;
    bool invulnerable;
};

template <typename T>
bool Read(uintptr_t address, T& value) {
    if (!address)
        return false;
#if defined(_MSC_VER)
    __try {
        std::memcpy(&value, reinterpret_cast<const void*>(address), sizeof(T));
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
#else
    std::memcpy(&value, reinterpret_cast<const void*>(address), sizeof(T));
    return true;
#endif
}

bool ReadCamera(uintptr_t root, Matrix& matrix) {
    uintptr_t a = 0, b = 0, camera = 0;
    if (!Read(root + 0x30, a) || !a ||
        !Read(a + 0x50, b) || !b ||
        !Read(b + 0x10, camera) || !camera)
        return false;

    for (int i = 0; i < 4; ++i) {
        if (!Read(camera + 0x2FC + 16ull * i, matrix.column[i]))
            return false;
    }
    return true;
}

bool Project(const Matrix& matrix, float worldX, float worldY, ImVec2& screen) {
    if (g_viewW <= 0 || g_viewH <= 0)
        return false;

    const float x = worldX;
    const float y = -worldY;
    const float w = matrix.column[0][3] * x +
                    matrix.column[1][3] * y +
                    matrix.column[3][3];
    if (!std::isfinite(w) || w < kNearPlane)
        return false;

    const float width =
        kProjectionWidthScale * static_cast<float>(g_viewW);
    const float height = static_cast<float>(g_viewH);
    const float clipX = matrix.column[0][0] * x +
                        matrix.column[1][0] * y +
                        matrix.column[3][0];
    const float clipY = matrix.column[0][1] * x +
                        matrix.column[1][1] * y +
                        matrix.column[3][1];
    screen.x = (clipX / w + 1.0f) * width * 0.5f;
    screen.y = (1.0f - clipY / w) * height * 0.5f;
    return std::isfinite(screen.x) && std::isfinite(screen.y);
}

bool SnapshotUnits(uintptr_t root, uintptr_t player, std::vector<Unit>& units) {
    uintptr_t world = 0, manager = 0, list = 0;
    uint32_t count = 0;
    if (!Read(root + 0x28, world) || !world ||
        !Read(world + ga::off::WORLD_OBJECT_MANAGER, manager) || !manager ||
        !Read(manager + 0x18, list) || !list ||
        !Read(list + 0x18, count) || count == 0 || count >= kMaxObjects)
        return false;

    units.clear();
    units.reserve(std::min<uint32_t>(count, 2048));
    for (uint32_t i = 0; i < count; ++i) {
        uintptr_t object = 0;
        if (!Read(list + 0x30 + 0x18ull * i, object) || !object || object == player)
            continue;

        Unit unit{};
        uintptr_t status = 0;
        uint8_t targetable = 0, invulnerable = 0;
        if (!Read(object + ga::off::OBJECT_X, unit.x) ||
            !Read(object + ga::off::OBJECT_Y, unit.y) ||
            !Read(object + ga::off::OBJECT_TYPE, unit.type) ||
            !Read(object + ga::off::OBJECT_HEALTH, unit.hp) ||
            !Read(object + ga::off::OBJECT_STATUS, status))
            continue;
        if (!std::isfinite(unit.x) || !std::isfinite(unit.y))
            continue;

        if (status)
            Read(status + ga::off::STATUS_CAN_TARGET, targetable);
        Read(object + ga::off::OBJECT_INVULNERABLE, invulnerable);
        unit.targetable = targetable != 0;
        unit.invulnerable = invulnerable != 0;
        units.push_back(unit);
    }
    return true;
}

void DrawGrid(const Matrix& matrix, float playerX, float playerY) {
    ImDrawList* draw = ImGui::GetBackgroundDrawList();
    const ImU32 minor = IM_COL32(110, 170, 220, 70);
    const ImU32 major = IM_COL32(140, 210, 255, 125);
    const int minX = static_cast<int>(std::floor(playerX - kGridRadius));
    const int maxX = static_cast<int>(std::ceil(playerX + kGridRadius));
    const int minY = static_cast<int>(std::floor(playerY - kGridRadius));
    const int maxY = static_cast<int>(std::ceil(playerY + kGridRadius));

    for (int x = minX; x <= maxX; ++x) {
        ImVec2 a{}, b{};
        if (Project(matrix, static_cast<float>(x), static_cast<float>(minY), a) &&
            Project(matrix, static_cast<float>(x), static_cast<float>(maxY), b))
            draw->AddLine(a, b, (x % 5) ? minor : major, (x % 5) ? 1.0f : 1.5f);
    }
    for (int y = minY; y <= maxY; ++y) {
        ImVec2 a{}, b{};
        if (Project(matrix, static_cast<float>(minX), static_cast<float>(y), a) &&
            Project(matrix, static_cast<float>(maxX), static_cast<float>(y), b))
            draw->AddLine(a, b, (y % 5) ? minor : major, (y % 5) ? 1.0f : 1.5f);
    }
}

void DrawUnits(const Matrix& matrix, const std::vector<Unit>& units) {
    ImDrawList* draw = ImGui::GetBackgroundDrawList();
    for (const Unit& unit : units) {
        ImVec2 point{};
        if (!Project(matrix, unit.x, unit.y, point))
            continue;
        if (point.x < 0.0f || point.y < 0.0f ||
            point.x > g_viewW * kProjectionWidthScale || point.y > g_viewH)
            continue;

        const ImU32 color = unit.invulnerable
            ? IM_COL32(150, 150, 150, 220)
            : unit.targetable ? IM_COL32(255, 80, 80, 235)
                              : IM_COL32(255, 205, 70, 220);
        draw->AddCircle(point, 5.0f, color, 12, 2.0f);
        char label[48]{};
        std::snprintf(label, sizeof(label), "%d  hp:%d", unit.type, unit.hp);
        draw->AddText(ImVec2(point.x + 7.0f, point.y - 7.0f), color, label);
    }
}

}

void Tick() {
    const bool renderUnits = g_cfg.renderUnits;
    const bool renderGrid = g_cfg.renderGrid;
    if ((!renderUnits && !renderGrid) || !ImGui::GetCurrentContext())
        return;

    const uintptr_t root = game::Root();
    const uintptr_t player = game::Player();
    if (!root || !player)
        return;

    Matrix matrix{};
    float playerX = 0.0f, playerY = 0.0f;
    if (!ReadCamera(root, matrix) ||
        !Read(player + ga::off::OBJECT_X, playerX) ||
        !Read(player + ga::off::OBJECT_Y, playerY) ||
        !std::isfinite(playerX) || !std::isfinite(playerY))
        return;

    if (renderGrid)
        DrawGrid(matrix, playerX, playerY);

    if (renderUnits) {
        std::vector<Unit> units;
        if (SnapshotUnits(root, player, units))
            DrawUnits(matrix, units);
    }
}

}
