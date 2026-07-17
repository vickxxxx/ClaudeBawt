


#include "features.h"
#include "config.h"
#include "il2cpp.h"
#include "log.h"

#include <windows.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <unordered_map>
#include <vector>
#include "MinHook.h"

namespace dodge {
namespace {

std::atomic<bool> g_bindSuspended{false};

struct Vec2 {
    float x;
    float y;
};

struct PathPoint {
    Vec2 position;
    int timeMs;
};

struct Threat {
    uintptr_t object;
    int startTime;
    int attacker;
    int objectType;
    float lifetimeMs;
    bool visible;
    float minX;
    float maxX;
    float minY;
    float maxY;
    std::vector<PathPoint> path;
};

struct Candidate {
    Vec2 position{};
    unsigned safeForMs = 0;
    bool legal = false;
};

struct TileInfo {
    bool present = false;
    int type = 37;
    bool blocked = false;
};


constexpr uintptr_t kMoveUpdateRva = 0x14F79F0; // 2026-07-17 native signature: CJCEGCEMIGE(float, float)
constexpr uintptr_t kMoveSpeedRva = 0x14FCA00; // 2026-07-17 native signature: GAFGPNKFMOJ()

constexpr uintptr_t kPlayerUpdateRva = 0x14FE560; // 2026-07-17 native signature: GJFKGLJEGKO(int, int)
constexpr uintptr_t kCollisionRadiusMultiplierOffset = 0x788; // ObjectProperties::collisionRadiusMultiplier


constexpr float kCandidatePadding = 0.06f;
constexpr float kDangerWeight = 0.02f;
constexpr float kDistanceWeight = 61.0f;
constexpr float kTwoPi = 6.2831855f;
constexpr unsigned kNoThreatTime = 40000;


constexpr int kAoeObjectType = 14174;
constexpr float kAoeLifetimeBonusMs = 500.0f;

using MoveUpdateFn = int64_t(__fastcall*)(uintptr_t, float, float);


using MoveSpeedFn = float(__fastcall*)(uintptr_t);
using ProjectilePositionFn =
    uint64_t(__fastcall*)(uintptr_t, float, float*, int*);

MoveUpdateFn g_originalMoveUpdate = nullptr;

float Distance(Vec2 a, Vec2 b) {
    const float dx = a.x - b.x;
    const float dy = a.y - b.y;
    return std::sqrt(dx * dx + dy * dy);
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

    const int x = static_cast<int>(point.x);
    const int y = static_cast<int>(point.y);
    if (x < 0 || y < 0 || x >= width || y >= height) return false;
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
            fullOccupy = *reinterpret_cast<uint8_t*>(status + 1698) != 0;
            objectAllowsWalk = *reinterpret_cast<uint8_t*>(status + 1764) != 0;
        }
    }

    bool damaging = false;
    int damage = 0;
    const uintptr_t props = *reinterpret_cast<uintptr_t*>(square + 80);
    if (props) {
        damaging = *reinterpret_cast<uint8_t*>(props + 260) != 0;
        damage = *reinterpret_cast<int*>(props + 268);
    }
    out.blocked = out.type == 34 || out.type == 5 || damaging || fullOccupy;
    if (damage > 0 && (out.type == 37 || (object && !objectAllowsWalk)))
        out.blocked = true;
    return true;
}

bool SegmentBlocked(Vec2 from, Vec2 to) {
    const float midpointX = std::round((from.x + to.x) * 0.5f);
    const float midpointY = std::round((from.y + to.y) * 0.5f);
    static constexpr Vec2 corners[] = {
        {-0.5f, -0.5f}, {0.5f, -0.5f}, {-0.5f, 0.5f}, {0.5f, 0.5f},
    };

    const float dx = to.x - from.x;
    const float dy = to.y - from.y;
    const float lengthSq = dx * dx + dy * dy;
    for (const Vec2 corner : corners) {
        const Vec2 center{midpointX + corner.x, midpointY + corner.y};
        TileInfo tile;
        if (!QueryTile(center, tile) || !tile.blocked) continue;

        float closestX = to.x;
        float closestY = to.y;
        if (lengthSq == 0.0f) {
            closestX = from.x;
            closestY = from.y;
        } else {
            const float t = ((center.x - from.x) * dx + (center.y - from.y) * dy) /
                            lengthSq;
            if (t < 0.0f) {
                closestX = from.x;
                closestY = from.y;
            } else if (t <= 1.0f) {
                closestX = from.x + t * dx;
                closestY = from.y + t * dy;
            }
        }
        const float ex = center.x - closestX;
        const float ey = center.y - closestY;
        if (std::sqrt(ex * ex + ey * ey) <= 0.9f) return true;
    }
    return false;
}

int CurrentGameTime(uintptr_t world) {
    return world
        ? *reinterpret_cast<int*>(world + ga::off::WORLD_GAME_TIME)
        : 0;
}

using VisibilityMap = std::unordered_map<int, bool>;

VisibilityMap SnapshotVisibility() {
    VisibilityMap visibility;
    if (g_cfg.dodgeInvisible)
        return visibility;

    const uintptr_t world = game::Root();
    const uintptr_t manager = world
        ? *reinterpret_cast<uintptr_t*>(world + ga::off::WORLD_OBJECT_MANAGER)
        : 0;
    const uintptr_t list = manager ? *reinterpret_cast<uintptr_t*>(manager + 24) : 0;
    if (!list) return visibility;

    const uint32_t count = *reinterpret_cast<uint32_t*>(list + 24);
    if (count >= 20000) return visibility;
    visibility.reserve(std::min<uint32_t>(count, 1024));

    for (uint32_t i = 0; i < count; ++i) {
        const uintptr_t object = *reinterpret_cast<uintptr_t*>(list + 48 + 24ull * i);
        if (!object) continue;
        const int objectId = *reinterpret_cast<int*>(object + 0x34);
        const uintptr_t status = *reinterpret_cast<uintptr_t*>(object + 0x18);
        visibility[objectId] =
            status && *reinterpret_cast<uint8_t*>(status + 1745) != 0;
    }
    return visibility;
}

bool ShooterVisible(int attacker, const VisibilityMap& visibility) {
    if (g_cfg.dodgeInvisible) return true;
    const auto found = visibility.find(attacker);
    if (found != visibility.end())
        return found->second;


    return true;
}

std::vector<Vec2> GatherEnemies() {
    std::vector<Vec2> enemies;
    if (g_cfg.dodgeKeepDistance <= 0.0f)
        return enemies;

    const uintptr_t world = game::Root();
    const uintptr_t manager = world
        ? *reinterpret_cast<uintptr_t*>(world + ga::off::WORLD_OBJECT_MANAGER)
        : 0;
    const uintptr_t list = manager ? *reinterpret_cast<uintptr_t*>(manager + 24) : 0;
    if (!list) return enemies;

    const uint32_t count = *reinterpret_cast<uint32_t*>(list + 24);
    if (!count || count >= 20000) return enemies;
    enemies.reserve(std::min<uint32_t>(count, 256));

    for (uint32_t i = 0; i < count; ++i) {
        const uintptr_t object = *reinterpret_cast<uintptr_t*>(list + 48 + 24ull * i);
        if (object <= 0xFFFF) continue;
        if (*reinterpret_cast<int*>(object + ga::off::OBJECT_HEALTH) <= 0) continue;
        const uintptr_t status =
            *reinterpret_cast<uintptr_t*>(object + ga::off::OBJECT_STATUS);
        if (status <= 0xFFFF || *reinterpret_cast<uint8_t*>(status + 1745) == 0) continue;
        enemies.push_back({*reinterpret_cast<float*>(object + ga::off::OBJECT_X),
                           *reinterpret_cast<float*>(object + ga::off::OBJECT_Y)});
    }
    return enemies;
}

float EnemyClearance(Vec2 p, const std::vector<Vec2>& enemies) {
    float best = std::numeric_limits<float>::infinity();
    for (const Vec2& e : enemies) {
        const float dx = e.x - p.x;
        const float dy = e.y - p.y;
        best = std::min(best, std::sqrt(dx * dx + dy * dy));
    }
    return best;
}


bool ProjectileBlockedByWall(Vec2 p, bool passThrough) {
    TileInfo tile;
    if (!QueryTile(p, tile) || !tile.blocked)
        return false;
    if (passThrough)
        return tile.type == 5;
    return tile.type == 4 || tile.type == 5 || tile.type == 34;
}

bool ProjectilePosition(uintptr_t projectile, float futureMs, Vec2& out) {
    auto fn = reinterpret_cast<ProjectilePositionFn>(
        ga::Rva(ga::rva::PROJECTILE_POSITION));
    if (!fn || projectile <= 0xFFFF) return false;


    float scratch[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    int scratchInt[2] = {0, 0};
    const uint64_t packed = fn(projectile, futureMs, scratch, scratchInt);
    std::memcpy(&out, &packed, sizeof(out));
    return std::isfinite(out.x) && std::isfinite(out.y) &&
           !(out.x == -1.0f && out.y == -1.0f);
}

void GatherThreats(std::vector<Threat>& threats, int gameTime) {
    threats.clear();
    const uintptr_t world = game::Root();
    const uintptr_t manager = world
        ? *reinterpret_cast<uintptr_t*>(world + ga::off::WORLD_PROJECTILE_MANAGER)
        : 0;
    const uintptr_t list = manager ? *reinterpret_cast<uintptr_t*>(manager + 24) : 0;
    if (!list) return;

    const uint32_t count = *reinterpret_cast<uint32_t*>(list + 24);
    if (!count || count >= 10000) return;
    threats.reserve(std::min<uint32_t>(count, 256));
    const VisibilityMap visibility = SnapshotVisibility();

    for (uint32_t index = 0; index < count; ++index) {
        const uintptr_t projectile =
            *reinterpret_cast<uintptr_t*>(list + 48 + 24ull * index);
        if (projectile <= 0xFFFF) continue;

        Threat threat{};
        threat.object = projectile;
        threat.startTime = *reinterpret_cast<int*>(projectile + 364);
        threat.attacker = *reinterpret_cast<int*>(projectile + 368);
        threat.lifetimeMs = *reinterpret_cast<float*>(projectile + 400);
        threat.visible = ShooterVisible(threat.attacker, visibility);
        threat.minX = threat.minY = std::numeric_limits<float>::infinity();
        threat.maxX = threat.maxY = -std::numeric_limits<float>::infinity();
        const uintptr_t klass = *reinterpret_cast<uintptr_t*>(projectile + 24);
        threat.objectType = klass > 0xFFFF
            ? *reinterpret_cast<int*>(klass + 1700)
            : 0;

        const bool isAoe = threat.objectType == kAoeObjectType;
        if (isAoe) {
            if (!g_cfg.dodgeAoeBombs)
                continue;
            threat.lifetimeMs += kAoeLifetimeBonusMs;
        }

        if (!std::isfinite(threat.lifetimeMs) || threat.lifetimeMs <= 0.0f ||
            threat.lifetimeMs > 125000.0f || threat.startTime <= 0 ||
            !threat.visible)
            continue;

        if (!isAoe &&
            static_cast<float>(gameTime) > threat.startTime + threat.lifetimeMs)
            continue;


        const uintptr_t pprops = *reinterpret_cast<uintptr_t*>(projectile + 280);
        const bool passThrough =
            pprops > 0xFFFF && *reinterpret_cast<uint8_t*>(pprops + 369) != 0;

        int samples = static_cast<int>(threat.lifetimeMs);
        samples = std::clamp(samples, 1, 300);
        const float interval = threat.lifetimeMs / static_cast<float>(samples);
        threat.path.reserve(samples + 1);
        bool truncated = false;
        for (int i = 0; i < samples; ++i) {
            Vec2 position{};
            const float future = static_cast<float>(i) * interval;
            if (!ProjectilePosition(projectile, future, position)) break;
            if (ProjectileBlockedByWall(position, passThrough)) {
                truncated = true;
                break;
            }
            threat.path.push_back({position, static_cast<int>(future)});
            threat.minX = std::min(threat.minX, position.x);
            threat.maxX = std::max(threat.maxX, position.x);
            threat.minY = std::min(threat.minY, position.y);
            threat.maxY = std::max(threat.maxY, position.y);
        }
        Vec2 finalPosition{};
        if (!truncated && ProjectilePosition(projectile, threat.lifetimeMs, finalPosition)) {
            threat.path.push_back({finalPosition, static_cast<int>(threat.lifetimeMs)});
            threat.minX = std::min(threat.minX, finalPosition.x);
            threat.maxX = std::max(threat.maxX, finalPosition.x);
            threat.minY = std::min(threat.minY, finalPosition.y);
            threat.maxY = std::max(threat.maxY, finalPosition.y);
        }
        if (!threat.path.empty()) threats.push_back(std::move(threat));
    }
}

bool SegmentHitBox(Vec2 from, Vec2 to, Vec2 center, float halfSize,
                   float& entryFraction) {
    float enter = 0.0f;
    float leave = 1.0f;

    const auto clipAxis = [&](float start, float delta, float minValue,
                              float maxValue) -> bool {
        if (std::fabs(delta) < 1e-7f)
            return start > minValue && start < maxValue;
        float a = (minValue - start) / delta;
        float b = (maxValue - start) / delta;
        if (a > b) std::swap(a, b);
        enter = std::max(enter, a);
        leave = std::min(leave, b);
        return enter <= leave;
    };

    const float dx = to.x - from.x;
    const float dy = to.y - from.y;
    if (!clipAxis(from.x, dx, center.x - halfSize, center.x + halfSize) ||
        !clipAxis(from.y, dy, center.y - halfSize, center.y + halfSize))
        return false;
    entryFraction = std::clamp(enter, 0.0f, 1.0f);
    return leave >= 0.0f && enter <= 1.0f;
}

bool PositionSafe(Vec2 position, int gameTime, const std::vector<Threat>& threats,
                  unsigned& safeForMs) {
    safeForMs = kNoThreatTime;
    if (threats.empty()) return true;

    const float hitboxHalf = g_cfg.dodgeHitboxSize;
    const float hitboxSq = hitboxHalf * hitboxHalf;
    bool safe = true;
    for (const Threat& threat : threats) {
        if (threat.path.empty()) continue;
        if (position.x + hitboxHalf <= threat.minX ||
            position.x - hitboxHalf >= threat.maxX ||
            position.y + hitboxHalf <= threat.minY ||
            position.y - hitboxHalf >= threat.maxY)
            continue;

        size_t first = 0;
        while (first + 1 < threat.path.size() &&
               static_cast<unsigned>(threat.startTime + threat.path[first].timeMs) <
                   static_cast<unsigned>(gameTime))
            ++first;
        if (first > 1) --first;

        for (size_t i = first; i < threat.path.size(); ++i) {
            const PathPoint& point = threat.path[i];
            const float dx = point.position.x - position.x;
            const float dy = point.position.y - position.y;

            if (i) {
                const PathPoint& previous = threat.path[i - 1];
                float entry = 0.0f;
                if (SegmentHitBox(previous.position, point.position, position,
                                  hitboxHalf, entry)) {
                    const int span = point.timeMs - previous.timeMs;
                    const unsigned hitTime = static_cast<unsigned>(
                        threat.startTime + previous.timeMs +
                        static_cast<int>(std::max(0, span) * entry));
                    if (hitTime >= static_cast<unsigned>(gameTime)) {
                        safeForMs = std::min(
                            safeForMs, hitTime - static_cast<unsigned>(gameTime));
                        safe = false;
                    }
                }
            }

            if (dx * dx < hitboxSq && dy * dy < hitboxSq) {
                const unsigned hitTime =
                    static_cast<unsigned>(threat.startTime + point.timeMs);
                if (hitTime >= static_cast<unsigned>(gameTime)) {
                    safeForMs =
                        std::min(safeForMs, hitTime - static_cast<unsigned>(gameTime));
                    safe = false;
                    break;
                }
            }
        }
    }
    return safe;
}


struct Ring {
    std::vector<Candidate> candidates;
    Candidate best{};
};

Ring BuildRing(Vec2 center, float scale, float density, int gameTime,
               const std::vector<Threat>& threats) {
    const int directions = std::max(
        1, static_cast<int>(static_cast<int>(scale * 16.0f) * density));
    const float radius = (g_cfg.dodgeHitboxSize + kCandidatePadding) * scale;

    Ring ring;
    ring.candidates.reserve(directions);
    for (int i = 0; i < directions; ++i) {
        const float angle = kTwoPi * static_cast<float>(i) /
                            static_cast<float>(directions);
        Candidate candidate{};
        candidate.position = {
            center.x + std::cos(angle) * radius,
            center.y + std::sin(angle) * radius,
        };
        candidate.legal = !SegmentBlocked(center, candidate.position);
        if (candidate.legal)
            PositionSafe(candidate.position, gameTime, threats, candidate.safeForMs);
        else
            candidate.safeForMs = 0;
        ring.candidates.push_back(candidate);
    }

    int64_t bestSmoothed = std::numeric_limits<int64_t>::min();
    for (int i = 0; i < directions; ++i) {
        int64_t smoothed = ring.candidates[i].safeForMs;
        for (int gap = 1; gap <= 3; ++gap) {
            smoothed += ring.candidates[(i + gap) % directions].safeForMs;
            smoothed += ring.candidates[(i - gap + directions) % directions].safeForMs;
        }
        if (smoothed > bestSmoothed) {
            bestSmoothed = smoothed;
            ring.best = ring.candidates[i];
        }
    }
    return ring;
}


Candidate ChooseCandidate(Vec2 center, int gameTime,
                          const std::vector<Threat>& threats,
                          const std::vector<Vec2>& enemies) {
    const Ring rings[] = {
        BuildRing(center, 0.5f, 2.0f, gameTime, threats),
        BuildRing(center, 1.0f, 1.0f, gameTime, threats),
        BuildRing(center, 1.5f, 1.0f, gameTime, threats),
        BuildRing(center, 2.0f, 1.0f, gameTime, threats),
    };

    int outer = 1;
    if (rings[2].best.safeForMs > rings[outer].best.safeForMs) outer = 2;
    if (rings[3].best.safeForMs > rings[outer].best.safeForMs) outer = 3;
    const Vec2 attractor = rings[outer].best.position;

    Candidate best{};
    double bestScore = -std::numeric_limits<double>::infinity();
    for (const Candidate& candidate : rings[0].candidates) {
        const float dx = candidate.position.x - attractor.x;
        const float dy = candidate.position.y - attractor.y;
        const float distance = std::max(std::sqrt(dx * dx + dy * dy), 1e-6f);
        const double score = static_cast<double>(kDistanceWeight) / distance +
                             static_cast<double>(candidate.safeForMs) * kDangerWeight;
        if (score > bestScore) {
            bestScore = score;
            best = candidate;
        }
    }


    if (!enemies.empty()) {
        float bestClearance = EnemyClearance(best.position, enemies);
        for (const Candidate& candidate : rings[0].candidates) {
            if (!candidate.legal || candidate.safeForMs < best.safeForMs)
                continue;
            const float clearance = EnemyClearance(candidate.position, enemies);
            if (clearance > bestClearance) {
                bestClearance = clearance;
                best = candidate;
            }
        }
    }
    return best;
}

float MoveSpeed(uintptr_t player) {
    auto fn = reinterpret_cast<MoveSpeedFn>(ga::Rva(kMoveSpeedRva));
    const float speed = fn ? fn(player) : 0.0f;
    return std::isfinite(speed) && speed > 0.0f ? speed : 0.001f;
}


int64_t __fastcall HookMoveUpdate(uintptr_t player, float targetX, float targetY) {
    if (player > 0xFFFF)
        game::CapturePlayer(player);

    noclip::NoteMoveTarget(targetX, -targetY);


    if (!g_cfg.dodgeProjectiles ||
        g_bindSuspended.load(std::memory_order_acquire) || !player)
        return g_originalMoveUpdate ? g_originalMoveUpdate(player, targetX, targetY) : 0;

    const uintptr_t world = game::Root();
    if (!world)
        return g_originalMoveUpdate ? g_originalMoveUpdate(player, targetX, targetY) : 0;

    static int previousGameTime = 0;
    const int gameTime = CurrentGameTime(world);
    const int frameMs = previousGameTime
        ? std::clamp(gameTime - previousGameTime, 1, 100)
        : 1;
    previousGameTime = gameTime;
    const Vec2 current{*reinterpret_cast<float*>(player + ga::off::OBJECT_X),
                       *reinterpret_cast<float*>(player + ga::off::OBJECT_Y)};
    const Vec2 requested{targetX, -targetY};

    std::vector<Threat> threats;
    GatherThreats(threats, gameTime);
    const std::vector<Vec2> enemies = GatherEnemies();
    const float keep = g_cfg.dodgeKeepDistance;
    auto InBuffer = [&](Vec2 p) {
        return keep > 0.0f && !enemies.empty() &&
               EnemyClearance(p, enemies) < keep;
    };
    const bool currentInBuffer = InBuffer(current);


    unsigned currentSafeFor = kNoThreatTime;
    const bool currentSafe =
        PositionSafe(current, gameTime, threats, currentSafeFor) && !currentInBuffer;
    unsigned requestedSafeFor = kNoThreatTime;
    const bool requestedSafe =
        PositionSafe(requested, gameTime, threats, requestedSafeFor) && !InBuffer(requested);


    if (requestedSafe)
        return g_originalMoveUpdate ? g_originalMoveUpdate(player, targetX, targetY) : 0;

    const float speed = MoveSpeed(player);

    if (currentSafe) {


        Vec2 output = requested;
        if (InBuffer(requested) ||
            g_cfg.dodgeHitboxSize / speed > static_cast<float>(requestedSafeFor))
            output = current;
        return g_originalMoveUpdate ? g_originalMoveUpdate(player, output.x, -output.y) : 0;
    }


    const Candidate chosen = ChooseCandidate(current, gameTime, threats, enemies);
    Vec2 output = requested;
    if (chosen.position.x != 0.0f || chosen.position.y != 0.0f) {
        const float distance = Distance(current, chosen.position);
        const int travelMs = static_cast<int>(distance / speed);
        if (currentInBuffer ||
            static_cast<int>(requestedSafeFor) - g_cfg.dodgeMoveAwayMs <= travelMs) {
            const float fraction =
                distance > 0.0f
                    ? std::min(1.0f, static_cast<float>(frameMs) * speed / distance)
                    : 1.0f;
            output.x = current.x + (chosen.position.x - current.x) * fraction;
            output.y = current.y + (chosen.position.y - current.y) * fraction;
        }
    }
    return g_originalMoveUpdate ? g_originalMoveUpdate(player, output.x, -output.y) : 0;
}

using PlayerUpdateFn = char(__fastcall*)(uintptr_t, int32_t, int32_t, void*);
PlayerUpdateFn g_originalPlayerUpdate = nullptr;

char __fastcall HookPlayerUpdate(uintptr_t player, int32_t now, int32_t delta,
                                 void* method) {
    if (player > 0xFFFF)
        game::CapturePlayer(player);
    if (player) {
        const uintptr_t status = *reinterpret_cast<uintptr_t*>(player + 0x18);
        if (status > 0xFFFF)
            *reinterpret_cast<float*>(status + kCollisionRadiusMultiplierOffset) = 0.0f;
    }
    return g_originalPlayerUpdate
        ? g_originalPlayerUpdate(player, now, delta, method)
        : 0;
}

}

void Install() {
    void* target = ga::Rva(kMoveUpdateRva);
    DBLOG("dodge::Install: move-hook target=%p (GA+0x%llX)", target,
          (unsigned long long)kMoveUpdateRva);
    if (target) {
        MH_STATUS st = MH_CreateHook(target, reinterpret_cast<void*>(&HookMoveUpdate),
                      reinterpret_cast<void**>(&g_originalMoveUpdate));
        DBLOG("dodge::Install: MH_CreateHook=%d orig=%p", (int)st, (void*)g_originalMoveUpdate);
    }

    void* playerUpdate = ga::Rva(kPlayerUpdateRva);
    DBLOG("dodge::Install: player-update target=%p (GA+0x%llX)", playerUpdate,
          (unsigned long long)kPlayerUpdateRva);
    if (playerUpdate) {
        const MH_STATUS st = MH_CreateHook(
            playerUpdate, reinterpret_cast<void*>(&HookPlayerUpdate),
            reinterpret_cast<void**>(&g_originalPlayerUpdate));
        DBLOG("dodge::Install: player-update MH_CreateHook=%d orig=%p",
              (int)st, (void*)g_originalPlayerUpdate);
    }

}

void Tick() {
    static bool wasDown = false;
    const bool down = g_cfg.dodgingHotkey.Pressed();
    if (g_cfg.dodgeHoldToToggle) {
        // The hold mode is a temporary override: hold the bind (Space by
        // default in the user's config) to stop dodge, release to resume.
        g_bindSuspended.store(down, std::memory_order_release);
    } else {
        g_bindSuspended.store(false, std::memory_order_release);
        if (down && !wasDown)
            g_cfg.dodgeProjectiles = !g_cfg.dodgeProjectiles;
    }
    wasDown = down;
}

}
