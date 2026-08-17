


#include "render_projectiles.h"

#include "config.h"
#include "il2cpp.h"
#include "overlay.h"

#include "imgui.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <cstring>

extern int g_viewW, g_viewH;

namespace render_projectiles {
namespace {

constexpr int kAoeObjectType = 14174;
constexpr uint32_t kMaxProjectileCount = 10000;
constexpr float kMaxLifetimeMs = 125000.0f;
constexpr float kViewScale = 0.8125f;
constexpr int kDebugSegments = 24;
constexpr int kBreadcrumbSegments = 20;

struct Vec2 {
    float x;
    float y;
};

struct Matrix {
    float c0[4], c1[4], c2[4], c3[4];
};

using ProjectilePositionFn =
    uint64_t(__fastcall*)(uintptr_t projectile, float timeMs, float* scratch,
                         int* scratchInt);

bool SanePointer(uintptr_t value) {
    return value > 0xFFFF;
}

bool Camera(Matrix& out) {
    return game::CameraMatrix(reinterpret_cast<float (*)[4]>(&out));
}

bool Project(const Matrix& matrix, Vec2 world, ImVec2& screen) {
    const float x = world.x;
    const float y = -world.y;
    const float w =
        matrix.c0[3] * x + matrix.c1[3] * y + matrix.c3[3];
    if (!std::isfinite(w) || w < 0.098f) return false;

    const int width =
        static_cast<int>(kViewScale * static_cast<float>(g_viewW));
    const int height = g_viewH;
    if (width <= 0 || height <= 0) return false;

    const float clipX =
        matrix.c0[0] * x + matrix.c1[0] * y + matrix.c3[0];
    const float clipY =
        matrix.c0[1] * x + matrix.c1[1] * y + matrix.c3[1];
    screen.x = (clipX / w + 1.0f) * (static_cast<float>(width) * 0.5f);
    screen.y = (1.0f - clipY / w) * (static_cast<float>(height) * 0.5f);
    return std::isfinite(screen.x) && std::isfinite(screen.y);
}

bool ProjectilePosition(uintptr_t projectile, float timeMs, Vec2& out) {
    const auto fn =
        reinterpret_cast<ProjectilePositionFn>(ga::Rva(ga::rva::PROJECTILE_POSITION));
    if (!fn || !SanePointer(projectile) || !std::isfinite(timeMs)) return false;


    float scratch[4]{};
    int scratchInt[2]{};
    const uint64_t packed = fn(projectile, timeMs, scratch, scratchInt);
    std::memcpy(&out, &packed, sizeof(out));
    return std::isfinite(out.x) && std::isfinite(out.y) &&
           !(out.x == -1.0f && out.y == -1.0f);
}

bool IsAoe(uintptr_t projectile) {
    const uintptr_t klass = *reinterpret_cast<uintptr_t*>(projectile + 0x18);
    return SanePointer(klass) &&
           *reinterpret_cast<int*>(klass + 0x6A4) == kAoeObjectType;
}

void DrawProjectile(ImDrawList* draw, const Matrix& matrix, uintptr_t projectile,
                    int gameTime) {
    const int startTick = *reinterpret_cast<int*>(projectile + 0x16C);
    const float lifetime = *reinterpret_cast<float*>(projectile + 0x190);
    if (startTick <= 0 || !std::isfinite(lifetime) || lifetime <= 0.0f ||
        lifetime > kMaxLifetimeMs)
        return;

    const float elapsed =
        std::clamp(static_cast<float>(gameTime - startTick), 0.0f, lifetime);
    if (gameTime > startTick + static_cast<int>(lifetime)) return;

    const bool aoe = IsAoe(projectile);
    const ImU32 pointColor =
        aoe ? IM_COL32(255, 80, 80, 235) : IM_COL32(255, 210, 45, 235);
    const ImU32 pathColor =
        aoe ? IM_COL32(255, 70, 70, 180) : IM_COL32(80, 210, 255, 150);

    Vec2 current{};
    ImVec2 currentScreen{};
    const bool haveCurrent =
        ProjectilePosition(projectile, elapsed, current) &&
        Project(matrix, current, currentScreen);

    if (g_cfg.renderProjectiles && haveCurrent) {
        draw->AddCircleFilled(currentScreen, aoe ? 5.0f : 3.5f, pointColor);
        draw->AddCircle(currentScreen, aoe ? 8.0f : 6.0f,
                        IM_COL32(0, 0, 0, 190), 16, 1.5f);
    }

    if (g_cfg.projectileBreadcrumbs && elapsed > 0.0f) {
        const float historyMs =
            std::min(elapsed, g_cfg.projectileBreadcrumbLifetime * 1000.0f);
        const float firstTime = elapsed - historyMs;
        ImVec2 previousScreen{};
        bool havePrevious = false;
        for (int i = 0; i <= kBreadcrumbSegments; ++i) {
            const float progress = static_cast<float>(i) / kBreadcrumbSegments;
            const float sampleTime = firstTime + historyMs * progress;
            Vec2 point{};
            ImVec2 screen{};
            if (!ProjectilePosition(projectile, sampleTime, point) ||
                !Project(matrix, point, screen)) {
                havePrevious = false;
                continue;
            }

            const int alpha = static_cast<int>(18.0f + 172.0f * progress * progress);
            const ImU32 color = aoe
                ? IM_COL32(255, 82, 94, alpha)
                : IM_COL32(92, 204, 255, alpha);
            if (havePrevious) {
                const float thickness = g_cfg.projectileBreadcrumbThickness *
                    (0.55f + 0.45f * progress);
                draw->AddLine(previousScreen, screen, color, thickness);
            }
            if ((i % 4) == 0 && i < kBreadcrumbSegments)
                draw->AddCircleFilled(screen,
                                      0.65f + progress * 0.85f, color, 8);
            previousScreen = screen;
            havePrevious = true;
        }
    }

    if (!g_cfg.renderAoeDebug) return;

    ImVec2 previousScreen{};
    bool havePrevious = false;
    for (int i = 0; i <= kDebugSegments; ++i) {
        const float t = elapsed + (lifetime - elapsed) *
                                      (static_cast<float>(i) / kDebugSegments);
        Vec2 point{};
        ImVec2 screen{};
        if (!ProjectilePosition(projectile, t, point) ||
            !Project(matrix, point, screen)) {
            havePrevious = false;
            continue;
        }
        if (havePrevious)
            draw->AddLine(previousScreen, screen, pathColor, aoe ? 2.5f : 1.5f);
        previousScreen = screen;
        havePrevious = true;
    }

    if (aoe && haveCurrent) {
        Vec2 landing{};
        ImVec2 landingScreen{};
        if (ProjectilePosition(projectile, lifetime, landing) &&
            Project(matrix, landing, landingScreen)) {
            ImVec2 radiusScreen{};
            const Vec2 radiusPoint{landing.x + g_cfg.aoeDebugRadius, landing.y};
            float radiusPx = 12.0f;
            if (Project(matrix, radiusPoint, radiusScreen))
                radiusPx = std::clamp(std::fabs(radiusScreen.x - landingScreen.x),
                                      6.0f, 360.0f);

            draw->AddCircleFilled(landingScreen, radiusPx,
                                  IM_COL32(255, 55, 70, 34), 48);
            draw->AddCircle(landingScreen, radiusPx,
                            IM_COL32(255, 76, 88, 220), 48, 2.0f);
            draw->AddCircle(landingScreen, std::max(3.0f, radiusPx * 0.12f),
                            IM_COL32(255, 220, 225, 245), 24, 1.5f);

            char label[64]{};
            const float remainingMs = std::max(
                0.0f, static_cast<float>(startTick) + lifetime - gameTime);
            if (g_cfg.aoeDebugCountdown)
                std::snprintf(label, sizeof(label), "AOE %.2fs  r=%.2f",
                              remainingMs / 1000.0f, g_cfg.aoeDebugRadius);
            else
                std::snprintf(label, sizeof(label), "AOE  r=%.2f",
                              g_cfg.aoeDebugRadius);
            draw->AddText(ImVec2(landingScreen.x + 8.0f,
                                 landingScreen.y - radiusPx - 17.0f),
                          pointColor, label);
        }
    }
}

void TickGuarded() {
    if ((!g_cfg.renderProjectiles && !g_cfg.renderAoeDebug &&
         !g_cfg.projectileBreadcrumbs) ||
        !overlay::Initialized())
        return;

    const uintptr_t world = game::Root();
    if (!SanePointer(world)) return;
    const uintptr_t manager =
        *reinterpret_cast<uintptr_t*>(world + ga::off::WORLD_PROJECTILE_MANAGER);
    if (!SanePointer(manager)) return;
    const uintptr_t list = *reinterpret_cast<uintptr_t*>(manager + 0x18);
    if (!SanePointer(list)) return;

    const uint32_t count = *reinterpret_cast<uint32_t*>(list + 0x18);
    if (!count || count >= kMaxProjectileCount) return;

    Matrix matrix{};
    if (!Camera(matrix)) return;
    ImDrawList* draw = ImGui::GetBackgroundDrawList();
    if (!draw) return;

    const int gameTime = *reinterpret_cast<int*>(world + ga::off::WORLD_GAME_TIME);
    for (uint32_t i = 0; i < count; ++i) {
        const uintptr_t projectile =
            *reinterpret_cast<uintptr_t*>(list + 0x30 + 0x18ull * i);
        if (SanePointer(projectile))
            DrawProjectile(draw, matrix, projectile, gameTime);
    }
}

}

void Install() {}

void Tick() {
#if defined(_MSC_VER)

    __try {
        TickGuarded();
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
#else
    TickGuarded();
#endif
}

}
