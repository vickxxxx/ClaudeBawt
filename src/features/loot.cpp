


#include "features.h"
#include "config.h"
#include "il2cpp.h"

#include <cstdint>
#include <cmath>
#include <cstring>
#include "imgui.h"

extern int g_viewW, g_viewH;

namespace loot {
namespace {

constexpr int kPoiType = 20800;
constexpr float kViewScale = 0.8125f;
constexpr float kMaxDistance = 60.0f;

struct Matrix {
    float c0[4], c1[4], c2[4], c3[4];
};

bool Camera(Matrix& out) {
    return game::CameraMatrix(reinterpret_cast<float (*)[4]>(&out));
}

bool Project(const Matrix& m, float x, float y, ImVec2& out) {
    y = -y;
    float w = m.c0[3] * x + m.c1[3] * y + m.c3[3];
    if (w < 0.098f)
        return false;
    int width = static_cast<int>(kViewScale * static_cast<float>(g_viewW));
    int height = g_viewH;
    if (!width || !height)
        return false;
    float clipX = m.c0[0] * x + m.c1[0] * y + m.c3[0];
    float clipY = m.c0[1] * x + m.c1[1] * y + m.c3[1];
    out.x = (clipX / w + 1.0f) * (static_cast<float>(width) * 0.5f);
    out.y = (1.0f - clipY / w) * (static_cast<float>(height) * 0.5f);
    return out.x >= 0.0f && out.x <= width && out.y >= 0.0f && out.y <= height;
}

void DrawPoi(const Matrix& matrix, float px, float py, float x, float y) {
    float dx = x - px;
    float dy = y - py;
    float distance = std::sqrt(dx * dx + dy * dy);
    if (!(distance > 0.0f) || distance > kMaxDistance)
        return;
    dx /= distance;
    dy /= distance;

    ImVec2 nearPoint{}, farPoint{};
    if (!Project(matrix, px + dx * 0.25f, py + dy * 0.25f, nearPoint) ||
        !Project(matrix, px + dx, py + dy, farPoint))
        return;

    ImDrawList* draw = ImGui::GetBackgroundDrawList();
    constexpr ImU32 color = IM_COL32(255, 132, 255, 255);
    draw->AddLine(nearPoint, farPoint, color, 2.0f);
    draw->AddText(ImVec2(farPoint.x - ImGui::CalcTextSize("POI").x, farPoint.y),
                  color, "POI");
}

}

void Install() {}

void Tick() {
    if (!g_cfg.enablePoisBags)
        return;

    uintptr_t world = game::Root();
    uintptr_t player = game::Player();
    if (!world || !player)
        return;
    uintptr_t owner =
        *reinterpret_cast<uintptr_t*>(world + ga::off::WORLD_OBJECT_MANAGER);
    if (!owner)
        return;
    uintptr_t list = *reinterpret_cast<uintptr_t*>(owner + 0x18);
    if (!list)
        return;
    uint32_t count = *reinterpret_cast<uint32_t*>(list + 0x18);
    if (!count || count >= 0x4E20)
        return;

    Matrix matrix{};
    if (!Camera(matrix))
        return;
    float px = *reinterpret_cast<float*>(player + ga::off::OBJECT_X);
    float py = *reinterpret_cast<float*>(player + ga::off::OBJECT_Y);

    for (uint32_t i = 0; i < count; ++i) {
        uintptr_t object = *reinterpret_cast<uintptr_t*>(list + 0x30 + 0x18ull * i);
        if (!object || *reinterpret_cast<int*>(object + ga::off::OBJECT_TYPE) != kPoiType)
            continue;
        DrawPoi(matrix, px, py,
                *reinterpret_cast<float*>(object + ga::off::OBJECT_X),
                *reinterpret_cast<float*>(object + ga::off::OBJECT_Y));
    }
}

}
