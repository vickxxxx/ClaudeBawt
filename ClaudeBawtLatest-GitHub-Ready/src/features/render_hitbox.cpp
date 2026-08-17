#include "render_hitbox.h"

#include "config.h"
#include "il2cpp.h"

#include <cstdint>
#include <cstring>
#include "imgui.h"

extern int g_viewW;
extern int g_viewH;

namespace render_hitbox {
namespace {

constexpr float kProjectionWidthScale = 0.8125f;
constexpr float kNearPlane = 0.098f;

struct CameraMatrix {
    float column[4][4];
};

bool GetCamera(CameraMatrix& matrix) {
    return game::CameraMatrix(matrix.column);
}

bool Project(const CameraMatrix& matrix, float worldX, float worldY,
             ImVec2& screen) {
    const float x = worldX;
    const float y = -worldY;
    const float w =
        matrix.column[0][3] * x +
        matrix.column[1][3] * y +
        matrix.column[3][3];
    if (w < kNearPlane)
        return false;

    const int width =
        static_cast<int>(kProjectionWidthScale * static_cast<float>(g_viewW));
    const int height = g_viewH;
    if (width <= 0 || height <= 0)
        return false;

    const float clipX =
        matrix.column[0][0] * x +
        matrix.column[1][0] * y +
        matrix.column[3][0];
    const float clipY =
        matrix.column[0][1] * x +
        matrix.column[1][1] * y +
        matrix.column[3][1];

    screen.x = (clipX / w + 1.0f) * (static_cast<float>(width) * 0.5f);
    screen.y = (1.0f - clipY / w) * (static_cast<float>(height) * 0.5f);
    return screen.x >= 0.0f && screen.x <= static_cast<float>(width) &&
           screen.y >= 0.0f && screen.y <= static_cast<float>(height);
}

}

void Tick() {
    if (!g_cfg.renderHitbox || !ImGui::GetCurrentContext())
        return;

    const uintptr_t player = game::Player();
    if (!player)
        return;

    CameraMatrix camera{};
    if (!GetCamera(camera))
        return;

    const float x = *reinterpret_cast<float*>(player + ga::off::OBJECT_X);
    const float y = *reinterpret_cast<float*>(player + ga::off::OBJECT_Y);
    const float halfSize = g_cfg.dodgeHitboxSize;
    if (!(halfSize > 0.0f))
        return;


    ImVec2 corners[4];
    if (!Project(camera, x - halfSize, y - halfSize, corners[0]) ||
        !Project(camera, x + halfSize, y - halfSize, corners[1]) ||
        !Project(camera, x + halfSize, y + halfSize, corners[2]) ||
        !Project(camera, x - halfSize, y + halfSize, corners[3]))
        return;

    ImDrawList* draw = ImGui::GetBackgroundDrawList();
    constexpr ImU32 fill = IM_COL32(255, 64, 64, 36);
    constexpr ImU32 outline = IM_COL32(255, 96, 96, 235);
    draw->AddConvexPolyFilled(corners, 4, fill);
    draw->AddPolyline(corners, 4, outline, 2.0f, ImDrawFlags_Closed);
}

}
