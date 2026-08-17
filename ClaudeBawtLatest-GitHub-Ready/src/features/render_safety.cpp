


#include "render_safety.h"

#include "config.h"
#include "il2cpp.h"

#include "imgui.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <vector>

extern int g_viewW, g_viewH;

namespace render_safety {
namespace {

struct Vec2 {
    float x;
    float y;
};

struct Matrix {
    float column[4][4];
};

struct PathPoint {
    Vec2 position;
    int timeMs;
};

struct Threat {
    int startTime;
    float lifetimeMs;
    float minX;
    float maxX;
    float minY;
    float maxY;
    std::vector<PathPoint> path;
};

struct Candidate {
    Vec2 position;
    unsigned safeForMs;
    bool legal;
};

constexpr unsigned kNoThreatTime = 40000;
constexpr float kTwoPi = 6.2831855f;
constexpr float kCandidatePadding = 0.06f;
constexpr float kViewScale = 0.8125f;
constexpr int kAoeObjectType = 14174;
constexpr float kAoeLifetimeBonusMs = 500.0f;

using ProjectilePositionFn =
    uint64_t(__fastcall*)(uintptr_t, float, float*, int*);

bool Camera(Matrix& out) {
    return game::CameraMatrix(out.column);
}

bool Project(const Matrix& matrix, Vec2 point, ImVec2& out) {
    const float x = point.x;
    const float y = -point.y;
    const float w = matrix.column[0][3] * x + matrix.column[1][3] * y +
                    matrix.column[3][3];
    if (w < 0.098f) return false;

    const int width = static_cast<int>(kViewScale * static_cast<float>(g_viewW));
    const int height = g_viewH;
    if (width <= 0 || height <= 0) return false;

    const float clipX = matrix.column[0][0] * x + matrix.column[1][0] * y +
                        matrix.column[3][0];
    const float clipY = matrix.column[0][1] * x + matrix.column[1][1] * y +
                        matrix.column[3][1];
    out.x = (clipX / w + 1.0f) * (static_cast<float>(width) * 0.5f);
    out.y = (1.0f - clipY / w) * (static_cast<float>(height) * 0.5f);
    return std::isfinite(out.x) && std::isfinite(out.y);
}

bool ProjectilePosition(uintptr_t projectile, float futureMs, Vec2& out) {
    auto fn = reinterpret_cast<ProjectilePositionFn>(
        ga::Rva(ga::rva::PROJECTILE_POSITION));
    if (!fn) return false;
    float scratch[4]{};
    int scratchInt[2]{};
    const uint64_t packed = fn(projectile, futureMs, scratch, scratchInt);
    std::memcpy(&out, &packed, sizeof(out));
    return std::isfinite(out.x) && std::isfinite(out.y) &&
           !(out.x == -1.0f && out.y == -1.0f);
}

bool TileBlocked(Vec2 point) {
    if (point.x < 0.0f || point.y < 0.0f) return true;
    const uintptr_t world = game::Root();
    if (!world) return true;
    const uintptr_t squares =
        *reinterpret_cast<uintptr_t*>(world + ga::off::WORLD_TILE_GRID);
    const int width =
        *reinterpret_cast<int*>(world + ga::off::WORLD_MAP_WIDTH);
    const int height =
        *reinterpret_cast<int*>(world + ga::off::WORLD_MAP_HEIGHT);
    const int x = static_cast<int>(point.x);
    const int y = static_cast<int>(point.y);
    if (!squares || x < 0 || y < 0 || x >= width || y >= height) return true;

    const uintptr_t square =
        *reinterpret_cast<uintptr_t*>(squares + 0x20 + 8ull * (x + height * y));
    if (!square) return true;
    const int type = *reinterpret_cast<int*>(square + 0x44);
    if (type == 34 || type == 5) return true;

    const uintptr_t object = *reinterpret_cast<uintptr_t*>(square + 0x48);
    const uintptr_t status =
        object ? *reinterpret_cast<uintptr_t*>(object + 0x18) : 0;
    if (status && *reinterpret_cast<uint8_t*>(
            status + ga::off::STATUS_FULL_OCCUPY)) return true;

    const uintptr_t props = *reinterpret_cast<uintptr_t*>(square + 0x50);
    return props && *reinterpret_cast<uint8_t*>(props + 260);
}

bool SegmentBlocked(Vec2 from, Vec2 to) {
    const Vec2 delta{to.x - from.x, to.y - from.y};
    const float lengthSq = delta.x * delta.x + delta.y * delta.y;
    const float mx = std::round((from.x + to.x) * 0.5f);
    const float my = std::round((from.y + to.y) * 0.5f);
    constexpr Vec2 offsets[] = {
        {-0.5f, -0.5f}, {0.5f, -0.5f}, {-0.5f, 0.5f}, {0.5f, 0.5f},
    };
    for (const Vec2 offset : offsets) {
        const Vec2 center{mx + offset.x, my + offset.y};
        if (!TileBlocked(center)) continue;
        float t = lengthSq > 0.0f
                      ? ((center.x - from.x) * delta.x +
                         (center.y - from.y) * delta.y) /
                            lengthSq
                      : 0.0f;
        t = std::clamp(t, 0.0f, 1.0f);
        const float dx = center.x - (from.x + delta.x * t);
        const float dy = center.y - (from.y + delta.y * t);
        if (std::sqrt(dx * dx + dy * dy) <= 0.9f) return true;
    }
    return false;
}

void GatherThreats(std::vector<Threat>& threats, int gameTime) {
    const uintptr_t world = game::Root();
    const uintptr_t manager = world
        ? *reinterpret_cast<uintptr_t*>(world + ga::off::WORLD_PROJECTILE_MANAGER)
        : 0;
    const uintptr_t list = manager ? *reinterpret_cast<uintptr_t*>(manager + 0x18) : 0;
    if (!list) return;
    const uint32_t count = *reinterpret_cast<uint32_t*>(list + 0x18);
    if (!count || count >= 10000) return;
    threats.reserve(std::min<uint32_t>(count, 256));

    for (uint32_t i = 0; i < count; ++i) {
        const uintptr_t projectile =
            *reinterpret_cast<uintptr_t*>(list + 0x30 + 0x18ull * i);
        if (projectile <= 0xFFFF) continue;
        const uintptr_t klass = *reinterpret_cast<uintptr_t*>(projectile + 0x18);
        const int objectType =
            klass > 0xFFFF ? *reinterpret_cast<int*>(klass + 1700) : 0;
        const bool aoe = objectType == kAoeObjectType;
        if (aoe && !g_cfg.dodgeAoeBombs) continue;

        Threat threat{};
        threat.startTime = *reinterpret_cast<int*>(projectile + 0x16C);
        threat.lifetimeMs = *reinterpret_cast<float*>(projectile + 0x190) +
                            (aoe ? kAoeLifetimeBonusMs : 0.0f);
        if (threat.startTime <= 0 || !std::isfinite(threat.lifetimeMs) ||
            threat.lifetimeMs <= 0.0f || threat.lifetimeMs > 125000.0f ||
            (!aoe && gameTime > threat.startTime + threat.lifetimeMs))
            continue;

        threat.minX = threat.minY = std::numeric_limits<float>::infinity();
        threat.maxX = threat.maxY = -std::numeric_limits<float>::infinity();
        const int samples = std::clamp(static_cast<int>(threat.lifetimeMs), 1, 300);
        const float step = threat.lifetimeMs / static_cast<float>(samples);
        threat.path.reserve(samples + 1);
        for (int sample = 0; sample <= samples; ++sample) {
            const float time =
                sample == samples ? threat.lifetimeMs : sample * step;
            Vec2 position{};
            if (!ProjectilePosition(projectile, time, position)) break;
            threat.path.push_back({position, static_cast<int>(time)});
            threat.minX = std::min(threat.minX, position.x);
            threat.maxX = std::max(threat.maxX, position.x);
            threat.minY = std::min(threat.minY, position.y);
            threat.maxY = std::max(threat.maxY, position.y);
        }
        if (!threat.path.empty()) threats.push_back(std::move(threat));
    }
}

bool SegmentHitsBox(Vec2 from, Vec2 to, Vec2 center, float halfSize,
                    float& entry) {
    float leave = 1.0f;
    entry = 0.0f;
    const auto clip = [&](float start, float delta, float low, float high) {
        if (std::fabs(delta) < 1e-7f) return start > low && start < high;
        float a = (low - start) / delta;
        float b = (high - start) / delta;
        if (a > b) std::swap(a, b);
        entry = std::max(entry, a);
        leave = std::min(leave, b);
        return entry <= leave;
    };
    return clip(from.x, to.x - from.x, center.x - halfSize, center.x + halfSize) &&
           clip(from.y, to.y - from.y, center.y - halfSize, center.y + halfSize) &&
           leave >= 0.0f && entry <= 1.0f;
}

bool PositionSafe(Vec2 position, int gameTime,
                  const std::vector<Threat>& threats, unsigned& safeForMs) {
    safeForMs = kNoThreatTime;
    bool safe = true;
    const float half = g_cfg.dodgeHitboxSize;
    const float halfSq = half * half;
    for (const Threat& threat : threats) {
        if (position.x + half <= threat.minX || position.x - half >= threat.maxX ||
            position.y + half <= threat.minY || position.y - half >= threat.maxY)
            continue;
        for (size_t i = 0; i < threat.path.size(); ++i) {
            const PathPoint& point = threat.path[i];
            if (i) {
                float entry = 0.0f;
                const PathPoint& previous = threat.path[i - 1];
                if (SegmentHitsBox(previous.position, point.position, position, half,
                                   entry)) {
                    const int span = std::max(0, point.timeMs - previous.timeMs);
                    const int hit = threat.startTime + previous.timeMs +
                                    static_cast<int>(span * entry);
                    if (hit >= gameTime) {
                        safeForMs =
                            std::min(safeForMs, static_cast<unsigned>(hit - gameTime));
                        safe = false;
                    }
                }
            }
            const float dx = point.position.x - position.x;
            const float dy = point.position.y - position.y;
            const int hit = threat.startTime + point.timeMs;
            if (dx * dx < halfSq && dy * dy < halfSq && hit >= gameTime) {
                safeForMs =
                    std::min(safeForMs, static_cast<unsigned>(hit - gameTime));
                safe = false;
            }
        }
    }
    return safe;
}

std::vector<Candidate> BuildRing(Vec2 center, int gameTime,
                                 const std::vector<Threat>& threats,
                                 float scale, float density) {
    const int count =
        std::max(1, static_cast<int>(static_cast<int>(scale * 16.0f) * density));
    const float radius = (g_cfg.dodgeHitboxSize + kCandidatePadding) * scale;
    std::vector<Candidate> ring;
    ring.reserve(count);
    for (int i = 0; i < count; ++i) {
        const float angle = kTwoPi * static_cast<float>(i) / count;
        Candidate candidate{
            {center.x + std::cos(angle) * radius,
             center.y + std::sin(angle) * radius},
            0,
            false,
        };
        candidate.legal = !SegmentBlocked(center, candidate.position);
        if (candidate.legal)
            PositionSafe(candidate.position, gameTime, threats, candidate.safeForMs);
        ring.push_back(candidate);
    }
    return ring;
}

Candidate SmoothedBest(const std::vector<Candidate>& ring) {
    Candidate best{};
    int64_t bestScore = std::numeric_limits<int64_t>::min();
    const int count = static_cast<int>(ring.size());
    for (int i = 0; i < count; ++i) {
        int64_t score = ring[i].safeForMs;
        for (int gap = 1; gap <= 3; ++gap) {
            score += ring[(i + gap) % count].safeForMs;
            score += ring[(i - gap + count) % count].safeForMs;
        }
        if (score > bestScore) {
            bestScore = score;
            best = ring[i];
        }
    }
    return best;
}

Candidate Choose(Vec2 center, int gameTime, const std::vector<Threat>& threats,
                 std::vector<Candidate>& inner) {
    inner = BuildRing(center, gameTime, threats, 0.5f, 2.0f);
    const Candidate outerBest[] = {
        SmoothedBest(BuildRing(center, gameTime, threats, 1.0f, 1.0f)),
        SmoothedBest(BuildRing(center, gameTime, threats, 1.5f, 1.0f)),
        SmoothedBest(BuildRing(center, gameTime, threats, 2.0f, 1.0f)),
    };
    const Candidate* attractor = &outerBest[0];
    if (outerBest[1].safeForMs > attractor->safeForMs) attractor = &outerBest[1];
    if (outerBest[2].safeForMs > attractor->safeForMs) attractor = &outerBest[2];

    Candidate best{};
    double bestScore = -std::numeric_limits<double>::infinity();
    for (const Candidate& candidate : inner) {
        const float dx = candidate.position.x - attractor->position.x;
        const float dy = candidate.position.y - attractor->position.y;
        const float distance = std::max(std::sqrt(dx * dx + dy * dy), 1e-6f);
        const double score = 61.0 / distance + candidate.safeForMs * 0.02;
        if (candidate.legal && score > bestScore) {
            bestScore = score;
            best = candidate;
        }
    }
    return best;
}

}

void Tick() {
    if (!g_cfg.renderSafetyPath) return;
    const uintptr_t world = game::Root();
    const uintptr_t player = game::Player();
    if (!world || !player) return;

    Matrix matrix{};
    if (!Camera(matrix)) return;
    const Vec2 center{*reinterpret_cast<float*>(player + ga::off::OBJECT_X),
                      *reinterpret_cast<float*>(player + ga::off::OBJECT_Y)};
    const int gameTime = *reinterpret_cast<int*>(world + ga::off::WORLD_GAME_TIME);
    std::vector<Threat> threats;
    GatherThreats(threats, gameTime);
    std::vector<Candidate> inner;
    const Candidate chosen = Choose(center, gameTime, threats, inner);

    ImVec2 centerScreen{};
    if (!Project(matrix, center, centerScreen)) return;
    ImDrawList* draw = ImGui::GetBackgroundDrawList();
    for (const Candidate& candidate : inner) {
        ImVec2 screen{};
        if (!Project(matrix, candidate.position, screen)) continue;
        const ImU32 color = !candidate.legal
                                ? IM_COL32(120, 120, 120, 180)
                                : candidate.safeForMs == kNoThreatTime
                                      ? IM_COL32(80, 230, 120, 210)
                                      : IM_COL32(255, 170, 40, 210);
        draw->AddCircleFilled(screen, candidate.legal ? 3.0f : 2.0f, color);
    }

    if (chosen.legal) {
        ImVec2 chosenScreen{};
        if (Project(matrix, chosen.position, chosenScreen)) {
            draw->AddLine(centerScreen, chosenScreen, IM_COL32(70, 255, 160, 235),
                          3.0f);
            draw->AddCircle(chosenScreen, 6.0f, IM_COL32(255, 255, 255, 245), 0,
                            2.0f);
        }
    }
}

}
