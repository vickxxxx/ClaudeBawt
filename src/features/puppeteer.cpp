#include "features.h"
#include "config.h"
#include "il2cpp.h"
#include "log.h"
#include "overlay.h"

#include <windows.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <limits>
#include "imgui.h"

extern int g_viewW;
extern int g_viewH;

namespace puppeteer {
namespace {

constexpr float kViewScale = 0.8125f;
constexpr uint32_t kMaxObjects = 20000;
constexpr int kMaxMapDimension = 2048;
constexpr int kCarboniferousBeaconObject = 0xCEF9;
constexpr int kCarboniferousBeaconFirst = 0xA312;
constexpr int kCarboniferousBeaconLast = 0xA362;
constexpr int kIgnoredGoldAnchor = 0x86B1;
constexpr int kIgnoredGoldMinion = 0x86B2;

struct Vec2 { float x; float y; };
struct Matrix { float column[4][4]{}; };
struct KeyLatch { WORD vk; bool down; };

KeyLatch g_left{'A', false};
KeyLatch g_right{'D', false};
KeyLatch g_up{'W', false};
KeyLatch g_down{'S', false};
bool g_mouseWasDown = false;
bool g_hotkeyWasDown = false;
bool g_fameHotkeyWasDown = false;
bool g_haveDestination = false;
Vec2 g_destination{};
Vec2 g_smoothedDirection{};
bool g_haveSmoothedDirection = false;
bool g_fameBotWasEnabled = false;
bool g_automationNoClipOverride = false;
bool g_savedAutoNoClip = false;
bool g_fameBotFireDown = false;
uintptr_t g_fameBotWorld = 0;
bool g_haveBeacon = false;
Vec2 g_beaconCenter{};
bool g_haveFameWaypoint = false;
Vec2 g_fameWaypoint{};
float g_fameExploreHeading = 0.0f;
bool g_haveFameExploreHeading = false;
Vec2 g_fameSmoothedDirection{};
bool g_haveFameSmoothedDirection = false;
Vec2 g_fameProgressPosition{};
bool g_haveFameProgressPosition = false;
bool g_returningToBeacon = false;
uint32_t g_fameRandom = 0x8A5CD789u;
ULONGLONG g_lastBeaconScan = 0;
ULONGLONG g_lastBeaconReturn = 0;
ULONGLONG g_nextFameWaypoint = 0;
ULONGLONG g_fameProgressCheck = 0;
ULONGLONG g_lastFameTerrainScan = 0;
int g_fameAllowedTerrain[16]{};
int g_fameAllowedTerrainCount = 0;
Vec2 g_fameExclusionCenters[16]{};
float g_fameExclusionRadii[16]{};
int g_fameExclusionCount = 0;
ULONGLONG g_lastFameExclusionScan = 0;

template <typename T>
bool Read(uintptr_t address, T& value) {
    if (!address) return false;
#if defined(_MSC_VER)
    __try {
#endif
        std::memcpy(&value, reinterpret_cast<const void*>(address), sizeof(T));
        return true;
#if defined(_MSC_VER)
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
#endif
}

void SetKey(KeyLatch& key, bool down) {
    if (key.down == down) return;
    INPUT input{};
    input.type = INPUT_KEYBOARD;
    input.ki.wVk = key.vk;
    input.ki.dwFlags = down ? 0 : KEYEVENTF_KEYUP;
    if (SendInput(1, &input, sizeof(input)) == 1)
        key.down = down;
}

void ReleaseMovement() {
    SetKey(g_left, false);
    SetKey(g_right, false);
    SetKey(g_up, false);
    SetKey(g_down, false);
    g_haveSmoothedDirection = false;
}

void SetFameBotFire(bool down) {
    if (g_fameBotFireDown == down) return;
    INPUT input{};
    input.type = INPUT_MOUSE;
    input.mi.dwFlags = down ? MOUSEEVENTF_LEFTDOWN : MOUSEEVENTF_LEFTUP;
    if (SendInput(1, &input, sizeof(input)) == 1)
        g_fameBotFireDown = down;
}

bool Project(const Matrix& matrix, Vec2 world, ImVec2& screen) {
    const float x = world.x;
    const float y = -world.y;
    const float w = matrix.column[0][3] * x +
                    matrix.column[1][3] * y + matrix.column[3][3];
    if (!std::isfinite(w) || w < 0.098f || g_viewW <= 0 || g_viewH <= 0)
        return false;
    const float clipX = matrix.column[0][0] * x +
                        matrix.column[1][0] * y + matrix.column[3][0];
    const float clipY = matrix.column[0][1] * x +
                        matrix.column[1][1] * y + matrix.column[3][1];
    const float width = kViewScale * static_cast<float>(g_viewW);
    const float height = static_cast<float>(g_viewH);
    screen.x = (clipX / w + 1.0f) * width * 0.5f;
    screen.y = (1.0f - clipY / w) * height * 0.5f;
    return std::isfinite(screen.x) && std::isfinite(screen.y);
}

bool LiveMousePosition(HWND window, ImVec2& mouse) {
    POINT point{};
    if (!window || !GetCursorPos(&point) || !ScreenToClient(window, &point))
        return false;
    mouse = ImVec2(static_cast<float>(point.x), static_cast<float>(point.y));
    return std::isfinite(mouse.x) && std::isfinite(mouse.y);
}

bool CursorWorld(const Matrix& matrix, HWND window, Vec2& world) {
    ImVec2 mouse{};
    if (!LiveMousePosition(window, mouse)) return false;
    const float width = kViewScale * static_cast<float>(g_viewW);
    const float height = static_cast<float>(g_viewH);
    if (width <= 1.0f || height <= 1.0f || mouse.x < 0.0f || mouse.y < 0.0f ||
        mouse.x > width || mouse.y > height)
        return false;
    const float nx = 2.0f * mouse.x / width - 1.0f;
    const float ny = 1.0f - 2.0f * mouse.y / height;
    const float a = matrix.column[0][0] - nx * matrix.column[0][3];
    const float b = matrix.column[1][0] - nx * matrix.column[1][3];
    const float c = nx * matrix.column[3][3] - matrix.column[3][0];
    const float d = matrix.column[0][1] - ny * matrix.column[0][3];
    const float e = matrix.column[1][1] - ny * matrix.column[1][3];
    const float f = ny * matrix.column[3][3] - matrix.column[3][1];
    const float det = a * e - b * d;
    if (!std::isfinite(det) || std::fabs(det) < 1.0e-7f) return false;
    const float px = (c * e - b * f) / det;
    const float py = (a * f - c * d) / det;
    world = {px, -py};
    return std::isfinite(world.x) && std::isfinite(world.y);
}

bool PlayerPosition(uintptr_t player, Vec2& position) {
    return player > 0xFFFF &&
           Read(player + ga::off::OBJECT_X, position.x) &&
           Read(player + ga::off::OBJECT_Y, position.y) &&
           std::isfinite(position.x) && std::isfinite(position.y);
}

bool FindCarboniferousBeacon(uintptr_t world, Vec2& center) {
    // The minimap/menu beacon is an invisible Class=Beacon world object.
    // Prefer its exact authoritative position over the decorative tile set.
    uintptr_t manager = 0, objects = 0;
    uint32_t objectCount = 0;
    if (Read(world + ga::off::WORLD_OBJECT_MANAGER, manager) && manager &&
        Read(manager + 0x18, objects) && objects &&
        Read(objects + 0x18, objectCount) && objectCount > 0 &&
        objectCount < kMaxObjects) {
        for (uint32_t i = 0; i < objectCount; ++i) {
            uintptr_t object = 0;
            int type = 0;
            Vec2 position{};
            if (!Read(objects + 0x30 + 0x18ull * i, object) || !object ||
                !Read(object + ga::off::OBJECT_TYPE, type) ||
                type != kCarboniferousBeaconObject ||
                !Read(object + ga::off::OBJECT_X, position.x) ||
                !Read(object + ga::off::OBJECT_Y, position.y) ||
                !std::isfinite(position.x) || !std::isfinite(position.y))
                continue;
            center = position;
            DBLOG("famebot: Carboniferous beacon object=%p center=(%.2f, %.2f) type=0x%X",
                  (void*)object, center.x, center.y, type);
            return true;
        }
    }

    // Fallback for builds/maps that omit the invisible beacon object.
    uintptr_t squares = 0;
    int width = 0, height = 0;
    if (!Read(world + ga::off::WORLD_TILE_GRID, squares) || !squares ||
        !Read(world + ga::off::WORLD_MAP_WIDTH, width) ||
        !Read(world + ga::off::WORLD_MAP_HEIGHT, height) ||
        width <= 0 || height <= 0 || width > kMaxMapDimension ||
        height > kMaxMapDimension)
        return false;

    double sumX = 0.0, sumY = 0.0;
    uint32_t matches = 0;
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            uintptr_t square = 0;
            // The live tile array uses map height as its row stride. Keep this
            // identical to noclip::TileIsWall; using width silently reads empty
            // entries on this rectangular map.
            if (!Read(squares + 0x20 + 8ull * (x + height * y), square) ||
                !square)
                continue;
            int type = 0;
            if (!Read(square + 0x44, type) ||
                type < kCarboniferousBeaconFirst ||
                type > kCarboniferousBeaconLast)
                continue;
            sumX += static_cast<double>(x) + 0.5;
            sumY += static_cast<double>(y) + 0.5;
            ++matches;
        }
    }
    if (!matches) return false;
    center.x = static_cast<float>(sumX / matches);
    center.y = static_cast<float>(sumY / matches);
    DBLOG("famebot: Carboniferous beacon center=(%.2f, %.2f) tiles=%u",
          center.x, center.y, matches);
    return true;
}

bool WalkablePoint(uintptr_t world, Vec2 point) {
    uintptr_t squares = 0;
    int width = 0, height = 0;
    if (!Read(world + ga::off::WORLD_TILE_GRID, squares) || !squares ||
        !Read(world + ga::off::WORLD_MAP_WIDTH, width) ||
        !Read(world + ga::off::WORLD_MAP_HEIGHT, height))
        return false;
    const int x = static_cast<int>(std::floor(point.x));
    const int y = static_cast<int>(std::floor(point.y));
    if (x < 0 || y < 0 || x >= width || y >= height) return false;
    uintptr_t square = 0;
    if (!Read(squares + 0x20 + 8ull * (x + height * y), square) || !square)
        return false;
    int type = 0;
    if (!Read(square + 0x44, type) || type == 5 || type == 34) return false;
    uintptr_t props = 0;
    if (Read(square + 0x50, props) && props) {
        uint8_t damaging = 0;
        int damage = 0;
        Read(props + 0x104, damaging);
        Read(props + 0x10C, damage);
        if (damaging || damage > 0) return false;
    }
    return true;
}

bool ReadGroundType(uintptr_t world, Vec2 point, int& type) {
    uintptr_t squares = 0;
    int width = 0, height = 0;
    if (!Read(world + ga::off::WORLD_TILE_GRID, squares) || !squares ||
        !Read(world + ga::off::WORLD_MAP_WIDTH, width) ||
        !Read(world + ga::off::WORLD_MAP_HEIGHT, height))
        return false;
    const int x = static_cast<int>(std::floor(point.x));
    const int y = static_cast<int>(std::floor(point.y));
    if (x < 0 || y < 0 || x >= width || y >= height) return false;
    uintptr_t square = 0;
    return Read(squares + 0x20 + 8ull * (x + height * y), square) && square &&
           Read(square + 0x44, type);
}

void BuildFameTerrainPalette(uintptr_t world, Vec2 beacon) {
    struct TypeCount { int type; int count; } counts[128]{};
    int countSize = 0;
    constexpr int radius = 14;
    for (int y = -radius; y <= radius; ++y) {
        for (int x = -radius; x <= radius; ++x) {
            int type = 0;
            if (!ReadGroundType(world,
                    {beacon.x + static_cast<float>(x),
                     beacon.y + static_cast<float>(y)}, type))
                continue;
            int slot = -1;
            for (int i = 0; i < countSize; ++i) {
                if (counts[i].type == type) { slot = i; break; }
            }
            if (slot >= 0) {
                ++counts[slot].count;
            } else if (countSize < static_cast<int>(_countof(counts))) {
                counts[countSize++] = {type, 1};
            }
        }
    }

    g_fameAllowedTerrainCount = 0;
    // A small dominant palette covers the natural biome without admitting the
    // many constructed floor types used by the grey minion enclosure.
    constexpr int kMaxLearnedTerrainTypes = 6;
    for (int pick = 0; pick < kMaxLearnedTerrainTypes; ++pick) {
        int best = -1;
        for (int i = 0; i < countSize; ++i) {
            if (counts[i].count < 3) continue;
            if (best < 0 || counts[i].count > counts[best].count) best = i;
        }
        if (best < 0) break;
        g_fameAllowedTerrain[g_fameAllowedTerrainCount++] = counts[best].type;
        DBLOG("famebot: allowed terrain type=%d samples=%d",
              counts[best].type, counts[best].count);
        counts[best].count = -1;
    }
    DBLOG("famebot: learned %d dominant terrain types around beacon",
          g_fameAllowedTerrainCount);
}

bool FameTerrainAllowed(uintptr_t world, Vec2 point) {
    int type = 0;
    if (!ReadGroundType(world, point, type)) return false;
    // The beacon object can appear one or two frames before its tile grid. Type
    // 37 is the verified natural Carboniferous ground, so movement can begin
    // while the full palette is retried asynchronously.
    if (!g_fameAllowedTerrainCount) return type == 37;
    if (type >= kCarboniferousBeaconFirst &&
        type <= kCarboniferousBeaconLast)
        return true;
    for (int i = 0; i < g_fameAllowedTerrainCount; ++i) {
        if (g_fameAllowedTerrain[i] == type) return true;
    }
    return false;
}

bool FameOutsideExclusion(Vec2 point, float padding = 0.0f) {
    for (int i = 0; i < g_fameExclusionCount; ++i) {
        const float dx = point.x - g_fameExclusionCenters[i].x;
        const float dy = point.y - g_fameExclusionCenters[i].y;
        const float radius = g_fameExclusionRadii[i] + padding;
        if (dx * dx + dy * dy <= radius * radius) return false;
    }
    return true;
}

void UpdateFameInvulnerableExclusion(uintptr_t world, Vec2 playerPosition,
                                     ULONGLONG now) {
    if (now - g_lastFameExclusionScan < 2000) return;
    g_lastFameExclusionScan = now;

    uintptr_t manager = 0, list = 0;
    uint32_t count = 0;
    if (!Read(world + ga::off::WORLD_OBJECT_MANAGER, manager) || !manager ||
        !Read(manager + 0x18, list) || !list ||
        !Read(list + 0x18, count) || !count || count >= kMaxObjects)
        return;

    Vec2 sleepers[192]{};
    int sleeperCount = 0;
    for (uint32_t i = 0; i < count && sleeperCount < static_cast<int>(_countof(sleepers)); ++i) {
        uintptr_t object = 0;
        int hp = 0, type = 0;
        uint8_t invulnerable = 0;
        Vec2 position{};
        if (!Read(list + 0x30 + 0x18ull * i, object) || !object ||
            !Read(object + ga::off::OBJECT_TYPE, type) ||
            !Read(object + ga::off::OBJECT_X, position.x) ||
            !Read(object + ga::off::OBJECT_Y, position.y) ||
            !std::isfinite(position.x) || !std::isfinite(position.y))
            continue;

        // 0x86B1 is the central object shared by each unwanted gold-minion
        // encounter. Remember the complete encounter footprint wherever the
        // randomized map places it.
        if (type == kIgnoredGoldAnchor) {
            bool known = false;
            for (int zone = 0; zone < g_fameExclusionCount; ++zone) {
                const float dx = position.x - g_fameExclusionCenters[zone].x;
                const float dy = position.y - g_fameExclusionCenters[zone].y;
                if (dx * dx + dy * dy <= 12.0f * 12.0f) {
                    g_fameExclusionRadii[zone] =
                        std::max(g_fameExclusionRadii[zone], 38.0f);
                    known = true;
                    break;
                }
            }
            if (!known && g_fameExclusionCount <
                    static_cast<int>(_countof(g_fameExclusionCenters))) {
                const int zone = g_fameExclusionCount++;
                g_fameExclusionCenters[zone] = position;
                g_fameExclusionRadii[zone] = 38.0f;
                DBLOG("famebot: ignored gold encounter[%d] anchor=(%.2f,%.2f)",
                      zone, position.x, position.y);
            }
            continue;
        }

        if (
            !Read(object + ga::off::OBJECT_HEALTH, hp) || hp <= 0 ||
            !Read(object + ga::off::OBJECT_INVULNERABLE, invulnerable) ||
            !invulnerable)
            continue;

        // Sleeping gold mobs report invulnerable but often do not report
        // targetable. Search locally wherever the randomized set-piece spawns.
        const float px = position.x - playerPosition.x;
        const float py = position.y - playerPosition.y;
        if (px * px + py * py > 50.0f * 50.0f)
            continue;
        sleepers[sleeperCount++] = position;
    }

    constexpr float clusterDistanceSq = 24.0f * 24.0f;
    for (int seed = 0; seed < sleeperCount; ++seed) {
        Vec2 sum{};
        int nearby = 0;
        for (int i = 0; i < sleeperCount; ++i) {
            const float dx = sleepers[i].x - sleepers[seed].x;
            const float dy = sleepers[i].y - sleepers[seed].y;
            if (dx * dx + dy * dy <= clusterDistanceSq) {
                sum.x += sleepers[i].x;
                sum.y += sleepers[i].y;
                ++nearby;
            }
        }
        if (nearby < 4) continue;
        const Vec2 center{sum.x / nearby, sum.y / nearby};
        float radius = 10.0f;
        for (int i = 0; i < sleeperCount; ++i) {
            const float sx = sleepers[i].x - sleepers[seed].x;
            const float sy = sleepers[i].y - sleepers[seed].y;
            if (sx * sx + sy * sy > clusterDistanceSq) continue;
            const float dx = sleepers[i].x - center.x;
            const float dy = sleepers[i].y - center.y;
            radius = std::max(radius, std::sqrt(dx * dx + dy * dy) + 5.0f);
        }
        radius = std::min(radius, 30.0f);

        bool known = false;
        for (int i = 0; i < g_fameExclusionCount; ++i) {
            const float dx = center.x - g_fameExclusionCenters[i].x;
            const float dy = center.y - g_fameExclusionCenters[i].y;
            if (dx * dx + dy * dy <= 12.0f * 12.0f) {
                g_fameExclusionRadii[i] =
                    std::max(g_fameExclusionRadii[i], radius);
                known = true;
                break;
            }
        }
        if (!known && g_fameExclusionCount <
                static_cast<int>(_countof(g_fameExclusionCenters))) {
            const int slot = g_fameExclusionCount++;
            g_fameExclusionCenters[slot] = center;
            g_fameExclusionRadii[slot] = radius;
            DBLOG("famebot: gold cluster[%d] center=(%.2f,%.2f) radius=%.2f count=%d",
                  slot, center.x, center.y, radius, nearby);
        }
    }
}

bool FameRouteAllowed(uintptr_t world, Vec2 from, Vec2 to) {
    const float dx = to.x - from.x;
    const float dy = to.y - from.y;
    const float distance = std::sqrt(dx * dx + dy * dy);
    const int samples = std::max(1, static_cast<int>(std::ceil(distance / 0.75f)));
    for (int i = 1; i <= samples; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(samples);
        const Vec2 sample{from.x + dx * t, from.y + dy * t};
        if (!FameTerrainAllowed(world, sample) ||
            !FameOutsideExclusion(sample, 1.5f))
            return false;
    }
    return true;
}

bool FindNearestTargetableEnemy(uintptr_t world, uintptr_t player, Vec2 position,
                                float radius, Vec2& result) {
    uintptr_t manager = 0, list = 0;
    uint32_t count = 0;
    if (!Read(world + ga::off::WORLD_OBJECT_MANAGER, manager) || !manager ||
        !Read(manager + 0x18, list) || !list ||
        !Read(list + 0x18, count) || !count || count >= kMaxObjects)
        return false;
    const float radiusSq = radius * radius;
    float bestDistanceSq = radiusSq;
    bool found = false;
    for (uint32_t i = 0; i < count; ++i) {
        uintptr_t object = 0, status = 0;
        int hp = 0, type = 0;
        uint8_t invulnerable = 0, targetable = 0;
        Vec2 enemy{};
        if (!Read(list + 0x30 + 0x18ull * i, object) || !object ||
            object == player ||
            !Read(object + ga::off::OBJECT_TYPE, type) ||
            type == kIgnoredGoldAnchor || type == kIgnoredGoldMinion ||
            !Read(object + ga::off::OBJECT_HEALTH, hp) || hp <= 0 ||
            !Read(object + ga::off::OBJECT_INVULNERABLE, invulnerable) ||
            invulnerable ||
            !Read(object + ga::off::OBJECT_STATUS, status) || !status ||
            !Read(status + ga::off::STATUS_CAN_TARGET, targetable) || !targetable ||
            !Read(object + ga::off::OBJECT_X, enemy.x) ||
            !Read(object + ga::off::OBJECT_Y, enemy.y))
            continue;
        const float dx = enemy.x - position.x;
        const float dy = enemy.y - position.y;
        const float distanceSq = dx * dx + dy * dy;
        if (distanceSq > bestDistanceSq ||
            !FameTerrainAllowed(world, enemy) ||
            !FameRouteAllowed(world, position, enemy))
            continue;
        bestDistanceSq = distanceSq;
        result = enemy;
        found = true;
    }
    return found;
}

float FameRandom01() {
    g_fameRandom = g_fameRandom * 1664525u + 1013904223u;
    return static_cast<float>((g_fameRandom >> 8) & 0x00FFFFFFu) /
           static_cast<float>(0x01000000u);
}

bool DriveScreen(float dx, float dy) {
    constexpr float deadZone = 20.0f;
    constexpr float smoothing = 0.28f;
    constexpr float cardinalSnap = 0.48f;

    if (!g_haveSmoothedDirection) {
        g_smoothedDirection = {dx, dy};
        g_haveSmoothedDirection = true;
    } else {
        g_smoothedDirection.x += (dx - g_smoothedDirection.x) * smoothing;
        g_smoothedDirection.y += (dy - g_smoothedDirection.y) * smoothing;
    }

    dx = g_smoothedDirection.x;
    dy = g_smoothedDirection.y;
    const float absX = std::fabs(dx);
    const float absY = std::fabs(dy);
    if (absX < deadZone && absY < deadZone) {
        SetKey(g_left, false);
        SetKey(g_right, false);
        SetKey(g_up, false);
        SetKey(g_down, false);
        return false;
    }

    // Bias a generous cone around each cardinal axis to a single key. This
    // prevents tiny mouse jitter from alternating straight and diagonal input.
    if (absX < absY * cardinalSnap)
        dx = 0.0f;
    else if (absY < absX * cardinalSnap)
        dy = 0.0f;

    // Movement input is already camera-relative: D/A track screen X and
    // W/S track screen Y. The previous diagonal conversion rotated every
    // requested cursor direction into the wrong quadrant.
    const float nativeX = dx;
    const float nativeY = -dy;
    const bool left = nativeX < -deadZone;   // A
    const bool right = nativeX > deadZone;   // D
    const bool up = nativeY > deadZone;      // W
    const bool down = nativeY < -deadZone;   // S
    SetKey(g_left, left);
    SetKey(g_right, right);
    SetKey(g_up, up);
    SetKey(g_down, down);
    return left || right || up || down;
}

bool DriveWorld(const Matrix& matrix, Vec2 player, Vec2 target,
                float stopDistance) {
    const float dx = target.x - player.x;
    const float dy = target.y - player.y;
    if (dx * dx + dy * dy <= stopDistance * stopDistance) {
        ReleaseMovement();
        return false;
    }
    ImVec2 to{};
    if (!Project(matrix, target, to)) {
        ReleaseMovement();
        return false;
    }
    const ImVec2 center(kViewScale * static_cast<float>(g_viewW) * 0.5f,
                        static_cast<float>(g_viewH) * 0.5f);
    return DriveScreen(to.x - center.x, to.y - center.y);
}

bool DriveFameWorld(const Matrix& matrix, Vec2 player, Vec2 target,
                    float stopDistance) {
    const float worldDx = target.x - player.x;
    const float worldDy = target.y - player.y;
    if (worldDx * worldDx + worldDy * worldDy <= stopDistance * stopDistance) {
        ReleaseMovement();
        g_haveFameSmoothedDirection = false;
        return false;
    }
    noclip::NoteMoveTarget(target.x, target.y);
    ImVec2 screen{};
    if (!Project(matrix, target, screen)) {
        ReleaseMovement();
        g_haveFameSmoothedDirection = false;
        return false;
    }
    const ImVec2 center(kViewScale * static_cast<float>(g_viewW) * 0.5f,
                        static_cast<float>(g_viewH) * 0.5f);
    float dx = screen.x - center.x;
    float dy = screen.y - center.y;
    const float length = std::sqrt(dx * dx + dy * dy);
    if (!std::isfinite(length) || length < 0.001f) return false;
    dx /= length;
    dy /= length;

    constexpr float smoothing = 0.10f;
    if (!g_haveFameSmoothedDirection) {
        g_fameSmoothedDirection = {dx, dy};
        g_haveFameSmoothedDirection = true;
    } else {
        g_fameSmoothedDirection.x +=
            (dx - g_fameSmoothedDirection.x) * smoothing;
        g_fameSmoothedDirection.y +=
            (dy - g_fameSmoothedDirection.y) * smoothing;
    }

    float sx = g_fameSmoothedDirection.x;
    float sy = g_fameSmoothedDirection.y;
    const float smoothLength = std::sqrt(sx * sx + sy * sy);
    if (smoothLength > 0.001f) {
        sx /= smoothLength;
        sy /= smoothLength;
    }

    // A narrow cardinal snap plus normalized hysteresis prevents rapid
    // diagonal/cardinal key flipping while retaining smooth turns.
    constexpr float cardinalSnap = 0.22f;
    if (std::fabs(sx) < std::fabs(sy) * cardinalSnap) sx = 0.0f;
    if (std::fabs(sy) < std::fabs(sx) * cardinalSnap) sy = 0.0f;
    constexpr float threshold = 0.18f;
    SetKey(g_left, sx < -threshold);
    SetKey(g_right, sx > threshold);
    SetKey(g_up, -sy > threshold);
    SetKey(g_down, -sy < -threshold);
    return true;
}

bool TryFameMapTeleport() {
    using DomainGetFn = void* (*)();
    using AssemblyOpenFn = void* (*)(void*, const char*);
    using AssemblyImageFn = void* (*)(void*);
    using ClassFromNameFn = void* (*)(void*, const char*, const char*);
    using ClassMethodFn = const void* (*)(void*, const char*, int);
    using ClassTypeFn = const void* (*)(void*);
    using TypeObjectFn = void* (*)(const void*);
    using RuntimeInvokeFn = void* (*)(const void*, void*, void**, void**);

    static bool initialized = false;
    static const void* findObjectMethod = nullptr;
    static const void* teleportMethod = nullptr;
    static void* miniMapTypeObject = nullptr;
    static RuntimeInvokeFn runtimeInvoke = nullptr;
    if (!initialized) {
        initialized = true;
        HMODULE module = GetModuleHandleA("GameAssembly.dll");
        const auto domainGet = reinterpret_cast<DomainGetFn>(
            GetProcAddress(module, "il2cpp_domain_get"));
        const auto assemblyOpen = reinterpret_cast<AssemblyOpenFn>(
            GetProcAddress(module, "il2cpp_domain_assembly_open"));
        const auto assemblyImage = reinterpret_cast<AssemblyImageFn>(
            GetProcAddress(module, "il2cpp_assembly_get_image"));
        const auto classFromName = reinterpret_cast<ClassFromNameFn>(
            GetProcAddress(module, "il2cpp_class_from_name"));
        const auto classMethod = reinterpret_cast<ClassMethodFn>(
            GetProcAddress(module, "il2cpp_class_get_method_from_name"));
        const auto classType = reinterpret_cast<ClassTypeFn>(
            GetProcAddress(module, "il2cpp_class_get_type"));
        const auto typeObject = reinterpret_cast<TypeObjectFn>(
            GetProcAddress(module, "il2cpp_type_get_object"));
        runtimeInvoke = reinterpret_cast<RuntimeInvokeFn>(
            GetProcAddress(module, "il2cpp_runtime_invoke"));
        if (domainGet && assemblyOpen && assemblyImage && classFromName &&
            classMethod && classType && typeObject && runtimeInvoke) {
            void* domain = domainGet();
            void* gameAssembly = domain
                ? assemblyOpen(domain, "Assembly-CSharp") : nullptr;
            void* unityAssembly = domain
                ? assemblyOpen(domain, "UnityEngine.CoreModule") : nullptr;
            void* gameImage = gameAssembly ? assemblyImage(gameAssembly) : nullptr;
            void* unityImage = unityAssembly ? assemblyImage(unityAssembly) : nullptr;
            void* miniMapClass = gameImage ? classFromName(
                gameImage, "DecaGames.RotMG.Managers.MiniMap",
                "MiniMapUIController") : nullptr;
            void* objectClass = unityImage
                ? classFromName(unityImage, "UnityEngine", "Object") : nullptr;
            const void* miniMapType = miniMapClass
                ? classType(miniMapClass) : nullptr;
            miniMapTypeObject = miniMapType ? typeObject(miniMapType) : nullptr;
            findObjectMethod = objectClass
                ? classMethod(objectClass, "FindObjectOfType", 1) : nullptr;
            if (!findObjectMethod && objectClass)
                findObjectMethod = classMethod(
                    objectClass, "FindFirstObjectByType", 1);
            teleportMethod = miniMapClass
                ? classMethod(miniMapClass, "DHIPCFBPGLC", 0) : nullptr;
        }
        DBLOG("famebot: map teleport reflection find=%p type=%p method=%p invoke=%p",
              findObjectMethod, miniMapTypeObject, teleportMethod,
              reinterpret_cast<void*>(runtimeInvoke));
    }
    if (!findObjectMethod || !teleportMethod || !miniMapTypeObject ||
        !runtimeInvoke)
        return false;

    void* typeArgument = miniMapTypeObject;
    void* findArgs[1]{&typeArgument};
    void* exception = nullptr;
    void* controller = runtimeInvoke(
        findObjectMethod, nullptr, findArgs, &exception);
    if (exception || !controller) {
        DBLOG("famebot: map teleport controller unavailable exception=%p",
              exception);
        return false;
    }
    exception = nullptr;
    runtimeInvoke(teleportMethod, controller, nullptr, &exception);
    if (exception) {
        DBLOG("famebot: map teleport invoke exception=%p", exception);
        return false;
    }
    DBLOG("famebot: invoked native minimap teleport controller=%p", controller);
    return true;
}

void ResetFameBot(bool releaseInput) {
    if (releaseInput) {
        ReleaseMovement();
        SetFameBotFire(false);
    }
    g_fameBotWorld = 0;
    g_haveBeacon = false;
    g_haveFameWaypoint = false;
    g_haveFameExploreHeading = false;
    g_haveFameSmoothedDirection = false;
    g_haveFameProgressPosition = false;
    g_returningToBeacon = false;
    g_lastBeaconScan = 0;
    g_lastBeaconReturn = 0;
    g_nextFameWaypoint = 0;
    g_fameProgressCheck = 0;
    g_lastFameTerrainScan = 0;
    g_fameAllowedTerrainCount = 0;
    g_fameExclusionCount = 0;
    g_lastFameExclusionScan = 0;
}

bool RunFameBot(uintptr_t world, uintptr_t player, const Matrix& matrix,
                Vec2 playerPosition, bool menuMouse) {
    const ULONGLONG now = GetTickCount64();
    if (g_fameBotWorld != world) {
        ResetFameBot(true);
        g_fameBotWorld = world;
    }

    // The regular aim hook handles target selection and angle correction.
    g_cfg.autoAim = true;
    g_cfg.targetingStyle = Config::TS_DISTANCE;

    if (!g_haveBeacon && (g_lastBeaconScan == 0 || now - g_lastBeaconScan >= 2000)) {
        g_lastBeaconScan = now;
        g_haveBeacon = FindCarboniferousBeacon(world, g_beaconCenter);
        if (g_haveBeacon) {
            BuildFameTerrainPalette(world, g_beaconCenter);
            g_lastFameTerrainScan = now;
            g_lastBeaconReturn = now;
            g_nextFameWaypoint = now;
        }
    }
    if (!g_haveBeacon) {
        ReleaseMovement();
        SetFameBotFire(false);
        return false;
    }

    // Retry after world loading settles if the beacon was available before the
    // tile array. Previously a single empty scan left the bot motionless.
    if (!g_fameAllowedTerrainCount &&
        now - g_lastFameTerrainScan >= 1000) {
        BuildFameTerrainPalette(world, g_beaconCenter);
        g_lastFameTerrainScan = now;
    }

    UpdateFameInvulnerableExclusion(world, playerPosition, now);
    if (!g_returningToBeacon && !FameOutsideExclusion(playerPosition, 2.0f)) {
        g_returningToBeacon = true;
        g_haveFameWaypoint = false;
        g_haveFameSmoothedDirection = false;
        DBLOG("famebot: inside dormant-minion exclusion; retreating to beacon");
    }

    const ULONGLONG returnMs = static_cast<ULONGLONG>(
        std::clamp(g_cfg.fameBotReturnSeconds, 15.0f, 300.0f) * 1000.0f);
    if (!g_returningToBeacon && g_lastBeaconReturn &&
        now - g_lastBeaconReturn >= returnMs) {
        g_returningToBeacon = true;
        g_haveFameWaypoint = false;
        g_haveFameSmoothedDirection = false;
        DBLOG("famebot: server-valid return walk started");
    }

    if (g_returningToBeacon) {
        const float dx = g_beaconCenter.x - playerPosition.x;
        const float dy = g_beaconCenter.y - playerPosition.y;
        if (dx * dx + dy * dy <= 0.75f * 0.75f) {
            ReleaseMovement();
            g_returningToBeacon = false;
            g_lastBeaconReturn = now;
            g_nextFameWaypoint = now + 600;
            g_haveFameSmoothedDirection = false;
            DBLOG("famebot: reached Carboniferous beacon normally");
            return false;
        }
        SetFameBotFire(false);
        return !menuMouse &&
            DriveFameWorld(matrix, playerPosition, g_beaconCenter, 0.65f);
    }

    Vec2 nearestEnemy{};
    const bool enemy = !menuMouse && FindNearestTargetableEnemy(
        world, player, playerPosition,
        std::max(24.0f, g_cfg.fameBotRoamRadius * 2.5f), nearestEnemy);

    // Check forward progress every 1.5 seconds. If a wall or object traps the
    // character, discard the current random destination immediately.
    if (!g_haveFameProgressPosition) {
        g_fameProgressPosition = playerPosition;
        g_haveFameProgressPosition = true;
        g_fameProgressCheck = now;
    } else if (now - g_fameProgressCheck >= 1500) {
        const float progressX = playerPosition.x - g_fameProgressPosition.x;
        const float progressY = playerPosition.y - g_fameProgressPosition.y;
        if (!enemy && progressX * progressX + progressY * progressY < 0.12f * 0.12f) {
            g_haveFameWaypoint = false;
            g_haveFameSmoothedDirection = false;
        }
        g_fameProgressPosition = playerPosition;
        g_fameProgressCheck = now;
    }

    bool driving = false;
    if (menuMouse) {
        ReleaseMovement();
    } else if (enemy) {
        // Follow enemies only until normal weapon range; auto aim/fire handles
        // the attack while the movement direction remains stable and smooth.
        g_haveFameWaypoint = false;
        driving = DriveFameWorld(matrix, playerPosition, nearestEnemy, 4.25f);
    } else {
        const float waypointDx = g_fameWaypoint.x - playerPosition.x;
        const float waypointDy = g_fameWaypoint.y - playerPosition.y;
        const bool reached = g_haveFameWaypoint &&
            waypointDx * waypointDx + waypointDy * waypointDy <= 0.55f * 0.55f;
        if (!g_haveFameWaypoint || reached || now >= g_nextFameWaypoint) {
            const float roamRadius =
                std::clamp(g_cfg.fameBotRoamRadius, 3.0f, 25.0f);
            g_haveFameWaypoint = false;
            constexpr float kTwoPi = 6.28318530718f;
            if (!g_haveFameExploreHeading) {
                g_fameExploreHeading = FameRandom01() * kTwoPi;
                g_haveFameExploreHeading = true;
            } else {
                // Preserve forward momentum instead of choosing unrelated
                // nearby points that make the character orbit in circles.
                g_fameExploreHeading += (FameRandom01() - 0.5f) * 1.10f;
            }
            for (int attempt = 0; attempt < 32; ++attempt) {
                // First try the current heading. If obstructed, fan outward in
                // alternating directions until a legal long route is found.
                const int step = (attempt + 1) / 2;
                const float side = (attempt & 1) ? 1.0f : -1.0f;
                const float angle = g_fameExploreHeading +
                    (attempt == 0 ? 0.0f : side * step * 0.24f);
                const float distance = std::max(16.0f, roamRadius * 1.8f) *
                    (0.90f + FameRandom01() * 0.25f);
                const Vec2 candidate{
                    playerPosition.x + std::cos(angle) * distance,
                    playerPosition.y + std::sin(angle) * distance,
                };
                if (!WalkablePoint(world, candidate) ||
                    !FameTerrainAllowed(world, candidate) ||
                    !FameOutsideExclusion(candidate, 2.0f) ||
                    !FameRouteAllowed(world, playerPosition, candidate))
                    continue;
                g_fameWaypoint = candidate;
                g_fameExploreHeading = angle;
                g_haveFameWaypoint = true;
                DBLOG("famebot: exploration waypoint=(%.2f,%.2f) heading=%.2f distance=%.2f",
                      candidate.x, candidate.y, angle, distance);
                break;
            }
            g_nextFameWaypoint = now + 25000;
            g_haveFameSmoothedDirection = false;
        }
        if (g_haveFameWaypoint)
            driving = DriveFameWorld(matrix, playerPosition, g_fameWaypoint, 0.45f);
        else
            ReleaseMovement();
    }

    SetFameBotFire(g_cfg.fameBotAutoFire && enemy);
    return driving;
}

bool FindLantern(uintptr_t world, uintptr_t player, Vec2 playerPosition,
                 Vec2& target) {
    uintptr_t manager = 0, list = 0;
    uint32_t count = 0;
    if (!Read(world + ga::off::WORLD_OBJECT_MANAGER, manager) || !manager ||
        !Read(manager + 0x18, list) || !list ||
        !Read(list + 0x18, count) || count == 0 || count >= kMaxObjects)
        return false;

    float bestDistance = std::numeric_limits<float>::max();
    bool found = false;
    for (uint32_t i = 0; i < count; ++i) {
        uintptr_t object = 0;
        int type = 0, hp = 0;
        Vec2 position{};
        if (!Read(list + 0x30 + 0x18ull * i, object) || !object ||
            object == player ||
            !Read(object + ga::off::OBJECT_TYPE, type) ||
            type != g_cfg.lanternType ||
            !Read(object + ga::off::OBJECT_X, position.x) ||
            !Read(object + ga::off::OBJECT_Y, position.y) ||
            !Read(object + ga::off::OBJECT_HEALTH, hp) || hp == 0 ||
            !std::isfinite(position.x) || !std::isfinite(position.y))
            continue;
        const float dx = position.x - playerPosition.x;
        const float dy = position.y - playerPosition.y;
        const float distance = dx * dx + dy * dy;
        if (distance < bestDistance) {
            bestDistance = distance;
            target = position;
            found = true;
        }
    }
    return found;
}

void DrawOverlay(bool driving, bool lantern) {
    if (!g_cfg.puppeteerOverlay || !ImGui::GetCurrentContext()) return;
    const char* title = lantern ? "Follow Lantern" :
        (driving ? "Puppeteer: ACTIVE" : "Puppeteer");
    const ImVec2 display = ImGui::GetIO().DisplaySize;
    const ImVec2 size = ImGui::CalcTextSize(title);
    ImGui::GetForegroundDrawList()->AddText(
        ImVec2((display.x - size.x) * 0.5f, 18.0f),
        driving ? IM_COL32(255, 45, 45, 255) : IM_COL32(150, 85, 255, 255),
        title);
}

void DrawFameBotOverlay(bool driving) {
    if (!g_cfg.puppeteerOverlay || !ImGui::GetCurrentContext()) return;
    char text[128]{};
    if (!g_haveBeacon) {
        std::snprintf(text, sizeof(text), "Fame Bot: searching for Carboniferous Beacon");
    } else {
        const ULONGLONG now = GetTickCount64();
        const float interval =
            std::clamp(g_cfg.fameBotReturnSeconds, 15.0f, 300.0f);
        const float elapsed = g_lastBeaconReturn
            ? static_cast<float>(now - g_lastBeaconReturn) / 1000.0f : 0.0f;
        std::snprintf(text, sizeof(text), "Fame Bot: %s | beacon return %.0fs",
                      g_returningToBeacon ? "RETURNING" :
                          (driving ? "SEARCHING" : "FIGHTING"),
                      std::max(0.0f, interval - elapsed));
    }
    const ImVec2 display = ImGui::GetIO().DisplaySize;
    const ImVec2 size = ImGui::CalcTextSize(text);
    ImGui::GetForegroundDrawList()->AddText(
        ImVec2((display.x - size.x) * 0.5f, 18.0f),
        IM_COL32(80, 210, 255, 255), text);
}

} // namespace

void Tick() {
    const bool hotkey = g_cfg.puppeteerHotkey.Pressed();
    if (!g_cfg.puppeteerHotkey.listening && hotkey && !g_hotkeyWasDown)
        g_cfg.puppeteerEnabled = !g_cfg.puppeteerEnabled;
    g_hotkeyWasDown = hotkey;

    const bool fameHotkey = g_cfg.fameBotHotkey.Pressed();
    if (!g_cfg.fameBotHotkey.listening && fameHotkey && !g_fameHotkeyWasDown)
        g_cfg.fameBot = !g_cfg.fameBot;
    g_fameHotkeyWasDown = fameHotkey;

    const bool automatedMovement = g_cfg.fameBot || g_cfg.followPlayer;
    if (automatedMovement && !g_automationNoClipOverride) {
        g_savedAutoNoClip = g_cfg.autoNoClip;
        g_automationNoClipOverride = true;
    }
    if (automatedMovement)
        g_cfg.autoNoClip = g_cfg.automationAutoNoClip;
    else if (g_automationNoClipOverride) {
        g_cfg.autoNoClip = g_savedAutoNoClip;
        g_automationNoClipOverride = false;
    }

    if (!g_cfg.fameBot && g_fameBotWasEnabled) {
        g_fameBotWasEnabled = false;
        ResetFameBot(true);
    }

    const uintptr_t player = game::Player();
    const uintptr_t world = game::Root();
    const bool mouseDown = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
    const HWND gameWindow = overlay::Window();
    if ((!g_cfg.puppeteerEnabled && !g_cfg.followPlayer &&
         !g_cfg.followLantern && !g_cfg.fameBot) ||
        !player || !world ||
        !ImGui::GetCurrentContext() || !gameWindow ||
        GetForegroundWindow() != gameWindow) {
        ReleaseMovement();
        SetFameBotFire(false);
        g_haveDestination = false;
        g_mouseWasDown = mouseDown;
        return;
    }

    Matrix matrix{};
    Vec2 playerPosition{};
    if (!game::CameraMatrix(matrix.column) ||
        !PlayerPosition(player, playerPosition)) {
        ReleaseMovement();
        return;
    }

    const bool menuMouse = ImGui::GetIO().WantCaptureMouse;
    if (g_cfg.fameBot) {
        g_fameBotWasEnabled = true;
        const bool driving = RunFameBot(world, player, matrix, playerPosition,
                                       menuMouse);
        g_mouseWasDown = mouseDown;
        DrawFameBotOverlay(driving);
        return;
    }
    bool driving = false;
    bool followingLantern = false;
    if (!g_cfg.followPlayer && g_cfg.puppeteerEnabled && !menuMouse &&
        g_cfg.puppeteerClickToMove) {
        if (mouseDown && !g_mouseWasDown) {
            Vec2 target{};
            if (CursorWorld(matrix, gameWindow, target)) {
                g_destination = target;
                g_haveDestination = true;
                DBLOG("puppeteer: click destination=(%.2f, %.2f)", target.x, target.y);
            }
        }
        if (g_haveDestination) {
            driving = DriveWorld(matrix, playerPosition, g_destination, 0.20f);
            if (!driving) g_haveDestination = false;
        }
    } else if (!g_cfg.followPlayer && g_cfg.puppeteerEnabled && !menuMouse && mouseDown) {
        const ImVec2 center(kViewScale * static_cast<float>(g_viewW) * 0.5f,
                            static_cast<float>(g_viewH) * 0.5f);
        ImVec2 mouse{};
        if (LiveMousePosition(gameWindow, mouse))
            driving = DriveScreen(mouse.x - center.x, mouse.y - center.y);
    }

    if (!g_cfg.followPlayer && !driving && g_cfg.followLantern) {
        Vec2 lantern{};
        if (FindLantern(world, player, playerPosition, lantern)) {
            driving = DriveWorld(matrix, playerPosition, lantern,
                                 g_cfg.lanternFollowDistance);
            followingLantern = true;
        } else {
            ReleaseMovement();
        }
    } else if (!driving) {
        ReleaseMovement();
    }

    g_mouseWasDown = mouseDown;
    if (!g_cfg.followPlayer)
        DrawOverlay(driving, followingLantern);
}

} // namespace puppeteer
