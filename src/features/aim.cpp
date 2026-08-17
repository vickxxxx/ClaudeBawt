


#include "features.h"
#include "config.h"
#include "il2cpp.h"
#include "log.h"
#include "overlay.h"

#include <windows.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>
#include "MinHook.h"
#include "imgui.h"

// MinHook's internal allocator places executable slots within rel32 range of
// the supplied address. buffer.c is already part of this build.
extern "C" void* AllocateBuffer(void* origin);

int g_viewW = 0;
int g_viewH = 0;

bool Keybind::Pressed() const {
    if (!vk) return false;
    if (vk < 0) {
        const int button = -vk - 1;
        return ImGui::GetCurrentContext() && button >= 0 && button < 5 &&
               ImGui::IsMouseDown(static_cast<ImGuiMouseButton>(button));
    }
    return (GetAsyncKeyState(vk) & 0x8000) != 0;
}

namespace aim {
namespace {

struct Vec2 {
    float x;
    float y;
};

struct Target {
    Vec2 position;
    int hp;
    bool targetable;
    bool invulnerable;
    uint64_t conditions;
    int type;
};

struct ProjectileInfo {
    int id = -1;
    float lifetimeMs = 0.0f;
    int speedTenths = 0;
};

struct CameraMatrix {
    float column[4][4];
};

struct TileInfo {
    bool present = false;
    int type = 37;
    bool blocked = false;
};

constexpr float kProjectionWidthScale = 0.8125f;
constexpr float kLosStep = 0.1f;
constexpr float kNearLosDistance = 1.8f;
constexpr float kLosStartAdvanceMax = 0.5f;
constexpr uintptr_t kEpSpreadScaleRva = 0x14BF870; // 2026-08-17 exact mulss xmm13,xmm0 site

float Distance(Vec2 a, Vec2 b) {
    const float dx = a.x - b.x;
    const float dy = a.y - b.y;
    return std::sqrt(dx * dx + dy * dy);
}

template <typename T>
bool SafeRead(uintptr_t address, T& value) {
    if (address <= 0xFFFF) return false;
    __try {
        value = *reinterpret_cast<const T*>(address);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        value = {};
        return false;
    }
}

template <typename T>
bool SafeWrite(uintptr_t address, const T& value) {
    if (address <= 0xFFFF) return false;
    __try {
        *reinterpret_cast<T*>(address) = value;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool QueryTile(Vec2 point, TileInfo& out) {
    out = {};
    if (point.x < 0.0f || point.y < 0.0f) return false;

    const uintptr_t world = game::Root();
    if (!world) return false;

    const uintptr_t squares =
        *reinterpret_cast<uintptr_t*>(world + ga::off::WORLD_TILE_GRID);
    const int height =
        *reinterpret_cast<int*>(world + ga::off::WORLD_MAP_HEIGHT);
    const int width =
        *reinterpret_cast<int*>(world + ga::off::WORLD_MAP_WIDTH);
    if (!squares || height <= 0 || width <= 0) return false;

    int x = static_cast<int>(point.x);
    int y = static_cast<int>(point.y);
    if (x >= width || y >= height) return false;

    const uintptr_t square =
        *reinterpret_cast<uintptr_t*>(squares + 32 + 8ull * (x + height * y));
    if (!square) {
        out.blocked = true;
        return true;
    }

    out.present = true;
    out.type = *reinterpret_cast<int*>(square + 68);

    bool fullOccupy = false;
    bool objectAllowsWalk = true;
    const uintptr_t object = *reinterpret_cast<uintptr_t*>(square + 72);
    if (object) {
        const uintptr_t status = *reinterpret_cast<uintptr_t*>(object + 24);
        if (status) {
            fullOccupy = *reinterpret_cast<uint8_t*>(
                status + ga::off::STATUS_FULL_OCCUPY) != 0;
            objectAllowsWalk = *reinterpret_cast<uint8_t*>(
                status + ga::off::STATUS_GROUND_PROTECT) != 0;
        }
    }

    bool damaging = false;
    int damage = 0;
    const uintptr_t props = *reinterpret_cast<uintptr_t*>(square + 80);
    if (props) {
        damaging = *reinterpret_cast<uint8_t*>(props + 260) != 0;
        damage = *reinterpret_cast<int*>(props + 268);
    }

    out.blocked = out.type == 34 || out.type == 5 || damaging;
    if (damage > 0 && (out.type == 37 || (object && !objectAllowsWalk)))
        out.blocked = true;
    if (fullOccupy) out.blocked = true;
    return true;
}

bool LineOfSight(Vec2 from, Vec2 to) {
    TileInfo tile;
    if (!QueryTile(to, tile) || tile.blocked) return false;

    static constexpr Vec2 offsets[] = {
        { 0.5f, -0.5f}, {-0.5f,  0.0f}, { 0.0f,  0.5f}, { 0.5f,  0.5f},
        {-0.5f, -0.5f}, { 0.5f,  0.0f}, {-0.5f,  0.5f}, { 0.0f, -0.5f},
    };
    for (const Vec2 offset : offsets) {
        if (!QueryTile({to.x + offset.x, to.y + offset.y}, tile) ||
            tile.type == 34 || tile.type == 5)
            return false;
    }

    const float distance = Distance(from, to);
    const int steps = static_cast<int>(distance / kLosStep);
    if (steps < 0) return true;
    for (int i = 0; i <= steps; ++i) {
        const float t = steps ? static_cast<float>(i) / static_cast<float>(steps) : 0.0f;
        const Vec2 point{
            from.x + (to.x - from.x) * t,
            from.y + (to.y - from.y) * t,
        };
        if (!QueryTile(point, tile) || tile.blocked) return false;
    }
    return true;
}

uint64_t ReadConditions(uintptr_t object) {


    const uintptr_t effects =
        *reinterpret_cast<uintptr_t*>(object + ga::off::OBJECT_EFFECTS);
    if (!effects || effects == UINT64_C(0xCCCCCCCCCCCCCCCC)) return 0;
    const int count = *reinterpret_cast<int*>(effects + 0x18);
    if (count < 1 || count > 8) return 0;
    const uint32_t low = *reinterpret_cast<uint32_t*>(effects + 0x20);
    const uint32_t high = count >= 2
        ? *reinterpret_cast<uint32_t*>(effects + 0x24)
        : 0;
    return low | (static_cast<uint64_t>(high) << 32);
}

void SnapshotTargets(std::vector<Target>& targets) {
    targets.clear();
    const uintptr_t world = game::Root();
    const uintptr_t manager = world
        ? *reinterpret_cast<uintptr_t*>(world + ga::off::WORLD_OBJECT_MANAGER)
        : 0;
    const uintptr_t list = manager ? *reinterpret_cast<uintptr_t*>(manager + 24) : 0;
    if (!list) return;

    const uint32_t count = *reinterpret_cast<uint32_t*>(list + 24);
    if (!count || count >= 20000) return;
    targets.reserve(count);

    for (uint32_t i = 0; i < count; ++i) {
        const uintptr_t object = *reinterpret_cast<uintptr_t*>(list + 48 + 24ull * i);
        if (!object) continue;

        Target target{};
        target.position.x = *reinterpret_cast<float*>(object + ga::off::OBJECT_X);
        target.position.y = *reinterpret_cast<float*>(object + ga::off::OBJECT_Y);
        target.hp = *reinterpret_cast<int*>(object + ga::off::OBJECT_HEALTH);
        target.invulnerable =
            *reinterpret_cast<uint8_t*>(object + ga::off::OBJECT_INVULNERABLE) != 0;
        target.conditions = ReadConditions(object);
        target.type = *reinterpret_cast<int*>(object + ga::off::OBJECT_TYPE);
        const uintptr_t status =
            *reinterpret_cast<uintptr_t*>(object + ga::off::OBJECT_STATUS);
        target.targetable =
            status && *reinterpret_cast<uint8_t*>(
                status + ga::off::STATUS_CAN_TARGET) != 0;
        if (target.type == 20469 || target.type == 20464)
            target.targetable = true;
        targets.push_back(target);
    }
}

bool Eligible(const Target& target) {
    if (target.hp <= 0 || !target.targetable || target.invulnerable) return false;
    return (target.conditions & 1) == 0 &&
           (target.conditions & UINT64_C(0x200000)) == 0 &&
           (target.conditions & UINT64_C(0x800000)) == 0 &&
           ((target.conditions >> 24) & 1) == 0;
}

bool ReadProjectileInfo(uintptr_t player, ProjectileInfo& out) {
    out = {};
    if (player <= 0xFFFF) return false;

    // August's firing routine reads the active attack container from +0x6B0.
    // ODMIGGKEOJL::FLNJGGLHKNA (+0x10) is ADNDPMIHLGP, whose +0x10 field is
    // the equipped weapon's ObjectProperties. This avoids the stale July
    // database chain rooted at player+0x608.
    uintptr_t container = 0;
    uintptr_t attack = 0;
    uintptr_t weapon = 0;
    uintptr_t projectiles = 0;
    uintptr_t projectile = 0;
    int count = 0;
    if (!SafeRead(player + 0x6B0, container) ||
        !SafeRead(container + 0x10, attack) ||
        !SafeRead(attack + 0x10, weapon) ||
        !SafeRead(weapon + 0x1C0, projectiles) ||
        !SafeRead(projectiles + 0x18, count) || count < 1 || count > 256 ||
        !SafeRead(projectiles + 0x20, projectile)) {
        return false;
    }

    SafeRead(weapon + 0x6B4, out.id); // ObjectProperties::type
    if (!SafeRead(projectile + 0x160, out.lifetimeMs) ||
        !SafeRead(projectile + 0x168, out.speedTenths)) {
        return false;
    }
    if (!std::isfinite(out.lifetimeMs) || out.lifetimeMs <= 0.0f ||
        out.lifetimeMs > 120000.0f || out.speedTenths <= 0 ||
        out.speedTenths > 100000) {
        out = {};
        return false;
    }

    // ProjectileProperties::IsPassesCover is verified at +0x171 in this dump.
    if (g_cfg.projectileNoClip) {
        const uint8_t enabled = 1;
        SafeWrite(projectile + 0x171, enabled);
    }
    return true;
}

bool GetCamera(CameraMatrix& matrix) {
    return game::CameraMatrix(matrix.column);
}

bool ProjectWorld(const CameraMatrix& m, Vec2 point, Vec2& screen) {
    const float x = point.x;
    const float y = -point.y;
    const float w = m.column[0][3] * x + m.column[1][3] * y + m.column[3][3];
    if (w < 0.098f) return false;

    const int width = static_cast<int>(kProjectionWidthScale * static_cast<float>(g_viewW));
    const int height = g_viewH;
    if (!width && !height) return false;

    const float clipX = m.column[0][0] * x + m.column[1][0] * y + m.column[3][0];
    const float clipY = m.column[0][1] * x + m.column[1][1] * y + m.column[3][1];
    screen.x = (clipX / w + 1.0f) * (static_cast<float>(width) * 0.5f);
    screen.y = (1.0f - clipY / w) * (static_cast<float>(height) * 0.5f);
    return std::isfinite(screen.x) && std::isfinite(screen.y);
}

bool WorldToScreen(const CameraMatrix& m, Vec2 point, Vec2& screen) {
    if (!ProjectWorld(m, point, screen)) return false;
    const int width = static_cast<int>(kProjectionWidthScale * static_cast<float>(g_viewW));
    const int height = g_viewH;
    return screen.x >= 0.0f && screen.x <= width &&
           screen.y >= 0.0f && screen.y <= height;
}

bool DrawRangeCircle(const CameraMatrix& camera, Vec2 center, float radius,
                     ImU32 fill, ImU32 shadow, ImU32 outline) {
    if (radius <= 0.05f) return false;

    constexpr int kSegments = 96;
    constexpr float kTwoPi = 6.28318530718f;
    std::vector<ImVec2> points;
    points.reserve(kSegments);
    for (int i = 0; i < kSegments; ++i) {
        const float angle = kTwoPi * static_cast<float>(i) /
                            static_cast<float>(kSegments);
        Vec2 screen{};
        if (!ProjectWorld(camera,
                {center.x + std::cos(angle) * radius,
                 center.y + std::sin(angle) * radius}, screen))
            return false;
        points.emplace_back(screen.x, screen.y);
    }

    ImDrawList* draw = ImGui::GetBackgroundDrawList();
    if ((fill & IM_COL32_A_MASK) != 0)
        draw->AddConvexPolyFilled(points.data(), static_cast<int>(points.size()), fill);
    draw->AddPolyline(points.data(), static_cast<int>(points.size()), shadow,
                      ImDrawFlags_Closed, 3.0f);
    draw->AddPolyline(points.data(), static_cast<int>(points.size()), outline,
                      ImDrawFlags_Closed, 1.35f);
    return true;
}

void DrawAimRanges() {
    if (!ImGui::GetCurrentContext() || g_viewW <= 0 || g_viewH <= 0)
        return;

    const bool showMagnet = g_cfg.renderMagnetRange && g_cfg.magnetAim;
    const bool showNormal = g_cfg.renderNormalAimRange && g_cfg.autoAim;
    if (!showMagnet && !showNormal) return;

    const uintptr_t player = game::Player();
    if (!player) return;
    const Vec2 center{*reinterpret_cast<float*>(player + ga::off::OBJECT_X),
                      *reinterpret_cast<float*>(player + ga::off::OBJECT_Y)};
    CameraMatrix camera{};
    if (!GetCamera(camera)) return;

    if (showNormal) {
        ProjectileInfo projectile{};
        if (ReadProjectileInfo(player, projectile)) {
            const float speedPerMs =
                (static_cast<float>(projectile.speedTenths) / 10.0f) / 1000.0f;
            const float normalRadius = std::clamp(
                speedPerMs * (projectile.lifetimeMs + 200.0f), 0.1f, 40.0f);
            DrawRangeCircle(camera, center, normalRadius,
                            IM_COL32(76, 181, 225, 8),
                            IM_COL32(5, 18, 24, 175),
                            IM_COL32(92, 203, 245, 205));
        }
    }

    if (showMagnet) {
        const float magnetRadius =
            std::clamp(g_cfg.magnetAimRange, 0.1f, 20.0f);
        DrawRangeCircle(camera, center, magnetRadius,
                        IM_COL32(139, 117, 230, 15),
                        IM_COL32(17, 13, 31, 180),
                        IM_COL32(164, 146, 244, 220));
    }
}

uint8_t* g_cave = nullptr;
uint8_t* g_epCave = nullptr;
float* g_aimPoint = nullptr;
float* g_epAimData = nullptr; // player x/y, direction x/y, radius
bool g_patchEnabled = false;
bool g_haveBackup = false;
uint8_t g_backup[40]{};
bool g_epSpreadPatched = false;
uint8_t* g_epSpreadCave = nullptr;

void SetEpSpreadPatch(bool enable) {
    if (enable == g_epSpreadPatched) return;
    auto* site = static_cast<uint8_t*>(ga::Rva(kEpSpreadScaleRva));
    if (!site) return;

    // Native: mulss xmm13,xmm0. xmm13 is the weapon's angular step. Scaling
    // it to zero aligns every projectile in the native subattack/burst loop
    // without replacing that loop or its projectile IDs.
    static constexpr uint8_t kNative[5] =
        {0xF3, 0x44, 0x0F, 0x59, 0xE8};

    if (enable && std::memcmp(site, kNative, sizeof(kNative)) != 0) {
        DBLOG("SetEpSpreadPatch: signature mismatch at GA+0x%llX",
              (unsigned long long)kEpSpreadScaleRva);
        return;
    }

    uint8_t patch[5]{};
    if (enable) {
        if (!g_epSpreadCave) {
            g_epSpreadCave =
                static_cast<uint8_t*>(AllocateBuffer(static_cast<void*>(site)));
            if (!g_epSpreadCave) return;

            static constexpr uint8_t kStub[15] = {
                0xF3,0x44,0x0F,0x59,0xE8,             // native mulss xmm13,xmm0
                0xF3,0x44,0x0F,0x59,0x2D,0x01,0,0,0, // mulss xmm13,[scale]
                0xC3                                   // ret
            };
            std::memcpy(g_epSpreadCave, kStub, sizeof(kStub));
            const float scale = 0.0f;
            std::memcpy(g_epSpreadCave + sizeof(kStub), &scale, sizeof(scale));
            FlushInstructionCache(GetCurrentProcess(), g_epSpreadCave, 32);
        }

        const intptr_t displacement =
            reinterpret_cast<intptr_t>(g_epSpreadCave) -
            (reinterpret_cast<intptr_t>(site) + sizeof(patch));
        if (displacement < INT32_MIN || displacement > INT32_MAX) {
            DBLOG("SetEpSpreadPatch: cave is outside rel32 range");
            return;
        }
        patch[0] = 0xE8;
        const int32_t rel = static_cast<int32_t>(displacement);
        std::memcpy(patch + 1, &rel, sizeof(rel));
    } else {
        if (site[0] != 0xE8) {
            DBLOG("SetEpSpreadPatch: restore refused; call patch missing");
            return;
        }
        std::memcpy(patch, kNative, sizeof(patch));
    }

    DWORD oldProtection = 0;
    if (!VirtualProtect(site, sizeof(patch), PAGE_EXECUTE_READWRITE,
                        &oldProtection))
        return;
    std::memcpy(site, patch, sizeof(patch));
    VirtualProtect(site, sizeof(patch), oldProtection, &oldProtection);
    FlushInstructionCache(GetCurrentProcess(), site, sizeof(patch));
    g_epSpreadPatched = enable;
    DBLOG("SetEpSpreadPatch: enable=%d angular-step scale=%.3f", (int)enable,
          enable ? 0.0f : 1.0f);
}

void SetAutoAimPatch(bool enable) {
    if (enable == g_patchEnabled) return;
    if (!ga::rva::AIM_POINT_PATCH) {
        if (enable)
            DBLOG("SetAutoAimPatch: no verified patch site for this build");
        return;
    }
    auto* site = static_cast<uint8_t*>(ga::Rva(ga::rva::AIM_POINT_PATCH));
    DBLOG("SetAutoAimPatch: enable=%d site=%p (GA+0x%llX)", (int)enable, (void*)site,
          (unsigned long long)ga::rva::AIM_POINT_PATCH);
    if (!site) return;

    DWORD oldProtection = 0;
    if (enable) {
        static constexpr uint8_t kExpected[27] = {
            0xF3,0x44,0x0F,0x58,0x4B,0x3C,
            0xF3,0x44,0x0F,0x59,0xD0,
            0xF3,0x44,0x0F,0x58,0x53,0x40,
            0xF3,0x44,0x0F,0x58,0xC9,
            0xF3,0x44,0x0F,0x58,0xD6,
        };
        if (std::memcmp(site, kExpected, sizeof(kExpected)) != 0) {
            DBLOG("SetAutoAimPatch: byte signature mismatch; patch refused");
            return;
        }
        if (!g_cave) {
            g_cave = static_cast<uint8_t*>(
                VirtualAlloc(nullptr, 160, MEM_COMMIT | MEM_RESERVE,
                             PAGE_EXECUTE_READWRITE));
            if (!g_cave) return;
            const uint8_t stub[24] = {
                0xF3,0x44,0x0F,0x10,0x0D,0x0F,0x00,0x00,0x00,
                0xF3,0x44,0x0F,0x10,0x15,0x0A,0x00,0x00,0x00,
                0xC3,0x90,0x90,0x00,0x00,0x00
            };
            std::memcpy(g_cave, stub, sizeof(stub));
            g_aimPoint = reinterpret_cast<float*>(g_cave + 24);

            // Per-projectile EP origin. The native loop index/count live at
            // caller stack +78h/+74h. A call pushes eight bytes, so this cave
            // reads them at +80h/+7Ch and computes:
            // player + direction * radius * index / (count - 1).
            static constexpr uint8_t epStub[] = {
                0x8B,0x44,0x24,0x80,             // mov eax,[rsp+80h]
                0xF3,0x0F,0x2A,0xC0,             // cvtsi2ss xmm0,eax
                0x8B,0x4C,0x24,0x7C,             // mov ecx,[rsp+7Ch]
                0xFF,0xC9,                        // dec ecx (count - 1)
                0x83,0xF9,0x01,                   // cmp ecx,1
                0x7D,0x05,                        // jge +5
                0xB9,0x01,0x00,0x00,0x00,        // mov ecx,1
                0xF3,0x0F,0x2A,0xC9,             // cvtsi2ss xmm1,ecx
                0xF3,0x0F,0x5E,0xC1,             // divss xmm0,xmm1
                0xF3,0x0F,0x59,0x05,0x48,0,0,0, // mulss xmm0,[radius]
                0xF3,0x44,0x0F,0x10,0x0D,0x37,0,0,0, // movss xmm9,[dirX]
                0xF3,0x44,0x0F,0x59,0xC8,        // mulss xmm9,xmm0
                0xF3,0x44,0x0F,0x58,0x0D,0x21,0,0,0, // addss xmm9,[playerX]
                0xF3,0x44,0x0F,0x10,0x15,0x24,0,0,0, // movss xmm10,[dirY]
                0xF3,0x44,0x0F,0x59,0xD0,        // mulss xmm10,xmm0
                0xF3,0x44,0x0F,0x58,0x15,0x0E,0,0,0, // addss xmm10,[playerY]
                0xC3
            };
            g_epCave = g_cave + 32;
            std::memcpy(g_epCave, epStub, sizeof(epStub));
            g_epAimData = reinterpret_cast<float*>(g_cave + 128);
        }
        if (!g_haveBackup) {
            std::memcpy(g_backup, site, sizeof(g_backup));
            g_haveBackup = true;
        }

        uint8_t patch[27] = {
            0xFF,0x15,0x02,0x00,0x00,0x00,0xEB,0x08,
            0,0,0,0,0,0,0,0,
            0x90,0x90,0x90,0x90,0x90,0x90,0x90,0x90,0x90,0x90,0x90
        };
        const uintptr_t cave = reinterpret_cast<uintptr_t>(g_cave);
        std::memcpy(patch + 8, &cave, sizeof(cave));
        VirtualProtect(site, sizeof(g_backup), PAGE_EXECUTE_READWRITE, &oldProtection);
        std::memcpy(site, patch, sizeof(patch));
        VirtualProtect(site, sizeof(g_backup), oldProtection, &oldProtection);
        FlushInstructionCache(GetCurrentProcess(), site, sizeof(patch));
    } else if (g_haveBackup) {
        VirtualProtect(site, sizeof(g_backup), PAGE_EXECUTE_READWRITE, &oldProtection);
        std::memcpy(site, g_backup, sizeof(g_backup));
        VirtualProtect(site, sizeof(g_backup), oldProtection, &oldProtection);
        FlushInstructionCache(GetCurrentProcess(), site, sizeof(g_backup));
    }
    g_patchEnabled = enable;
}

void SetAimCaveMode(bool experimental) {
    if (!g_patchEnabled || !g_cave || !g_epCave) return;
    auto* site = static_cast<uint8_t*>(ga::Rva(ga::rva::AIM_POINT_PATCH));
    if (!site) return;
    const uintptr_t desired = reinterpret_cast<uintptr_t>(
        experimental ? g_epCave : g_cave);
    uintptr_t current = 0;
    std::memcpy(&current, site + 8, sizeof(current));
    if (current == desired) return;
    DWORD oldProtection = 0;
    if (!VirtualProtect(site + 8, sizeof(desired), PAGE_EXECUTE_READWRITE,
                        &oldProtection))
        return;
    std::memcpy(site + 8, &desired, sizeof(desired));
    VirtualProtect(site + 8, sizeof(desired), oldProtection, &oldProtection);
    FlushInstructionCache(GetCurrentProcess(), site + 8, sizeof(desired));
}

using AimFn = void(__fastcall*)(uintptr_t, float, uintptr_t);
AimFn g_originalAim = nullptr;

void __fastcall HookAim(uintptr_t self, float inputAngle, uintptr_t methodInfo) {
    static bool once = false;
    if (!once) { once = true; DBLOG("HookAim: first call self=%p angle=%.3f", (void*)self, inputAngle); }
    if ((!g_cfg.autoAim && !g_cfg.magnetAim) || !game::Player()) {
        if (g_originalAim) g_originalAim(self, inputAngle, methodInfo);
        return;
    }

    const uintptr_t player = game::Player();
    const Vec2 playerPosition{
        *reinterpret_cast<float*>(player + 0x3C),
        *reinterpret_cast<float*>(player + 0x40),
    };

    ProjectileInfo projectile{};
    ReadProjectileInfo(player, projectile);
    const bool epRangeAura = g_cfg.experimentalEpRangeAura && g_cfg.magnetAim;
    const float nativeProjectileRange =
        projectile.lifetimeMs > 0.0f && projectile.speedTenths > 0
            ? (static_cast<float>(projectile.speedTenths) / 10.0f) / 1000.0f *
                  projectile.lifetimeMs
            : 0.0f;

    std::vector<Target> targets;
    SnapshotTargets(targets);

    POINT cursor{};
    GetCursorPos(&cursor);


    if (HWND window = overlay::Window())
        ScreenToClient(window, &cursor);

    CameraMatrix camera{};
    const bool haveCamera =
        g_cfg.targetingStyle == Config::TS_CURSOR && GetCamera(camera);

    const Target* best = nullptr;
    float bestMetric = 99999.0f;
    int hpTargets = 0;
    int enemyTargets = 0;
    int vulnerableTargets = 0;
    int conditionTargets = 0;
    for (const Target& target : targets) {
        if (target.hp > 0) ++hpTargets;
        if (target.hp > 0 && target.targetable) ++enemyTargets;
        if (target.hp > 0 && target.targetable && !target.invulnerable)
            ++vulnerableTargets;
        if (Eligible(target)) ++conditionTargets;
        if (!Eligible(target)) continue;
        if (epRangeAura) {
            const float targetDistance = Distance(target.position, playerPosition);
            if (nativeProjectileRange <= 0.0f ||
                targetDistance > nativeProjectileRange +
                                     1.99f + 0.15f ||
                !LineOfSight(playerPosition, target.position))
                continue;
        }
        float metric = 99999.0f;
        if (g_cfg.targetingStyle == Config::TS_DISTANCE ||
            (g_cfg.targetingStyle == Config::TS_CURSOR && !haveCamera)) {
            metric = Distance(target.position, playerPosition);
        } else if (g_cfg.targetingStyle == Config::TS_CURSOR) {
            Vec2 screen{};
            if (!haveCamera || !WorldToScreen(camera, target.position, screen)) continue;
            metric = Distance(screen, {static_cast<float>(cursor.x), static_cast<float>(cursor.y)});
        } else if (g_cfg.targetingStyle == Config::TS_HEALTH) {
            metric = static_cast<float>(target.hp);
        }
        if (bestMetric > metric) {
            bestMetric = metric;
            best = &target;
        }
    }

    // isEnemy is verified in the August ObjectProperties layout. If every
    // enemy was rejected only by the still-obfuscated invulnerability or
    // condition fields, keep magnet aim useful by selecting the nearest live
    // enemy as a conservative fallback.
    if (!best && g_cfg.magnetAim) {
        for (const Target& target : targets) {
            if (target.hp <= 0 || !target.targetable) continue;
            const float metric = Distance(target.position, playerPosition);
            if (metric > 0.05f && metric < bestMetric) {
                bestMetric = metric;
                best = &target;
            }
        }
    }

    // Experimental cave mode executes for every generated projectile, even
    // when no aura target was selected. Always seed it with a valid local
    // fallback so a target loss can never emit projectiles near world (0, 0).
    if (epRangeAura && g_epAimData) {
        g_epAimData[0] = playerPosition.x;
        g_epAimData[1] = playerPosition.y;
        g_epAimData[2] = std::cos(inputAngle);
        g_epAimData[3] = std::sin(inputAngle);
        g_epAimData[4] = 0.0f;
    }

    if (g_aimPoint) {
        Vec2 output = playerPosition;
        if (best && g_cfg.magnetAim) {
            const Vec2 delta{
                best->position.x - playerPosition.x,
                best->position.y - playerPosition.y,
            };
            float distance = Distance(best->position, playerPosition);
            if (distance <= 0.0f) distance = 0.01f;
            const Vec2 direction{delta.x / distance, delta.y / distance};

            Vec2 losStart = playerPosition;
            if (distance > kNearLosDistance) {
                const float advance =
                    std::min(distance - kNearLosDistance, kLosStartAdvanceMax);
                losStart.x += direction.x * advance;
                losStart.y += direction.y * advance;
            }

            if (epRangeAura) {
                // Keep the shared origin inside the server-accepted movement
                // envelope. Placing it directly on a distant target causes an
                // immediate disconnect; 1.99 tiles is the proven safe advance.
                const float auraAdvance = std::min(distance, 1.99f);
                output.x = playerPosition.x + direction.x * auraAdvance;
                output.y = playerPosition.y + direction.y * auraAdvance;
                if (g_epAimData) {
                    g_epAimData[0] = playerPosition.x;
                    g_epAimData[1] = playerPosition.y;
                    g_epAimData[2] = direction.x;
                    g_epAimData[3] = direction.y;
                    g_epAimData[4] = auraAdvance;
                }
            } else if (distance <= g_cfg.magnetAimRange) {
                if (distance < kNearLosDistance ||
                    LineOfSight(playerPosition, losStart)) {
                    output = best->position;
                }
            } else {
                const float speedPerMs =
                    (static_cast<float>(projectile.speedTenths) / 10.0f) / 1000.0f;
                const float projectileReach =
                    speedPerMs * projectile.lifetimeMs + g_cfg.magnetAimRange;
                if (g_cfg.magnetRangeExt && projectileReach >= distance &&
                    LineOfSight(playerPosition, losStart)) {
                    output.x = playerPosition.x + direction.x * g_cfg.magnetAimRange;
                    output.y = playerPosition.y + direction.y * g_cfg.magnetAimRange;
                }
            }
        }
        g_aimPoint[0] = output.x;
        g_aimPoint[1] = output.y;
    }

    float outputAngle = inputAngle;
    if (best && (g_cfg.autoAim || g_cfg.magnetAim)) {
        outputAngle = std::atan2(
            best->position.y - playerPosition.y,
            best->position.x - playerPosition.x);
    }

    static int debugShots = 0;
    if (debugShots < 8) {
        ++debugShots;
        DBLOG("HookAim: shot=%d targets=%llu hp=%d enemy=%d vulnerable=%d eligible=%d best=%p style=%d camera=%d input=%.3f output=%.3f",
              debugShots, (unsigned long long)targets.size(), hpTargets,
              enemyTargets, vulnerableTargets, conditionTargets,
              (const void*)best, g_cfg.targetingStyle, (int)haveCamera,
              inputAngle, outputAngle);
    }

    if (g_originalAim) g_originalAim(self, outputAngle, methodInfo);
}

}

void Install() {
    void* target = ga::Rva(ga::rva::SHOT_UPDATE);
    DBLOG("aim::Install: shot target=%p (GA+0x%llX)", target,
          (unsigned long long)ga::rva::SHOT_UPDATE);
    if (!target) return;
    const MH_STATUS st = MH_CreateHook(
        target, reinterpret_cast<void*>(&HookAim),
        reinterpret_cast<void**>(&g_originalAim));
    DBLOG("aim::Install: MH_CreateHook=%d orig=%p", (int)st,
          (void*)g_originalAim);
}

void Tick() {
    static bool wasDown = false;
    const bool down = g_cfg.aimbotHotkey.Pressed();
    if (down && !wasDown) g_cfg.magnetAim = !g_cfg.magnetAim;
    wasDown = down;
    // 0x7D8AD1 is the verified start of the 27-byte world-space aim-point
    // calculation that produces xmm9/xmm10 immediately before projectile setup.
    SetAutoAimPatch(g_cfg.magnetAim);
    // The protocol permits one shared origin per shot group. Supplying a
    // different origin for each EP projectile causes an immediate disconnect,
    // so experimental mode keeps the normal shared Kill Aura cave.
    SetAimCaveMode(false);
    SetEpSpreadPatch(g_cfg.magnetAim && g_cfg.experimentalEpRangeAura);
    DrawAimRanges();
}

}
