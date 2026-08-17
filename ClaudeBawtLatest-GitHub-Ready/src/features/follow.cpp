#include "features.h"
#include "config.h"
#include "il2cpp.h"
#include "overlay.h"

#include <windows.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include "imgui.h"

extern int g_viewW;
extern int g_viewH;

namespace follow {
namespace {

constexpr float kViewScale = 0.8125f;
constexpr uint32_t kMaxObjects = 20000;
struct Vec2 { float x; float y; };
struct Matrix { float column[4][4]{}; };
struct KeyLatch { WORD vk; bool down; };

KeyLatch g_left{'A', false};
KeyLatch g_right{'D', false};
KeyLatch g_up{'W', false};
KeyLatch g_down{'S', false};
Vec2 g_smoothed{};
bool g_haveSmoothed = false;
bool g_wasEnabled = false;
uintptr_t g_target = 0;
uintptr_t g_world = 0;
ULONGLONG g_nextScan = 0;
char g_lastName[32]{};

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
    if (SendInput(1, &input, sizeof(input)) == 1) key.down = down;
}

void Stop() {
    SetKey(g_left, false);
    SetKey(g_right, false);
    SetKey(g_up, false);
    SetKey(g_down, false);
    g_haveSmoothed = false;
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
    screen.x = (clipX / w + 1.0f) *
               (kViewScale * static_cast<float>(g_viewW)) * 0.5f;
    screen.y = (1.0f - clipY / w) * static_cast<float>(g_viewH) * 0.5f;
    return std::isfinite(screen.x) && std::isfinite(screen.y);
}

bool ManagedStringEquals(uintptr_t string, const char* expected) {
    if (string <= 0xFFFF || !expected || !expected[0]) return false;
    const size_t wantedLength = std::strlen(expected);
    if (wantedLength == 0 || wantedLength >= sizeof(g_cfg.followPlayerName))
        return false;
    int length = 0;
    if (!Read(string + 0x10, length) || length != static_cast<int>(wantedLength))
        return false;
    for (int i = 0; i < length; ++i) {
        wchar_t actual = 0;
        if (!Read(string + 0x14 + static_cast<uintptr_t>(i) * sizeof(wchar_t),
                  actual))
            return false;
        unsigned char wanted = static_cast<unsigned char>(expected[i]);
        if (actual >= L'A' && actual <= L'Z') actual += L'a' - L'A';
        if (wanted >= 'A' && wanted <= 'Z') wanted += 'a' - 'A';
        if (actual != static_cast<wchar_t>(wanted)) return false;
    }
    return true;
}

bool ObjectHasName(uintptr_t object, const char* expected) {
    for (uintptr_t offset = 0x10; offset < 0x700; offset += sizeof(uintptr_t)) {
        uintptr_t value = 0;
        if (Read(object + offset, value) && ManagedStringEquals(value, expected))
            return true;
    }
    uintptr_t status = 0;
    if (!Read(object + ga::off::OBJECT_STATUS, status) || status <= 0xFFFF)
        return false;
    for (uintptr_t offset = 0x10; offset < 0x800; offset += sizeof(uintptr_t)) {
        uintptr_t value = 0;
        if (Read(status + offset, value) && ManagedStringEquals(value, expected))
            return true;
    }
    return false;
}

uintptr_t FindTarget(uintptr_t world, uintptr_t player) {
    uintptr_t manager = 0, list = 0, playerClass = 0, playerStatus = 0;
    uintptr_t playerStatusClass = 0;
    uint32_t count = 0;
    if (!g_cfg.followPlayerName[0] ||
        !Read(world + ga::off::WORLD_OBJECT_MANAGER, manager) || !manager ||
        !Read(manager + 0x18, list) || !list ||
        !Read(list + 0x18, count) || count == 0 || count >= kMaxObjects ||
        !Read(player, playerClass) || !playerClass)
        return 0;
    if (Read(player + ga::off::OBJECT_STATUS, playerStatus) && playerStatus)
        Read(playerStatus, playerStatusClass);

    for (uint32_t i = 0; i < count; ++i) {
        uintptr_t object = 0, objectClass = 0, status = 0, statusClass = 0;
        if (!Read(list + 0x30 + 0x18ull * i, object) || object <= 0xFFFF ||
            object == player || !Read(object, objectClass))
            continue;
        bool playerLike = objectClass == playerClass;
        if (!playerLike && playerStatusClass &&
            Read(object + ga::off::OBJECT_STATUS, status) && status &&
            Read(status, statusClass))
            playerLike = statusClass == playerStatusClass;
        if (playerLike && ObjectHasName(object, g_cfg.followPlayerName))
            return object;
    }
    return 0;
}

bool TargetPosition(uintptr_t object, Vec2& position) {
    return object > 0xFFFF &&
           Read(object + ga::off::OBJECT_X, position.x) &&
           Read(object + ga::off::OBJECT_Y, position.y) &&
           std::isfinite(position.x) && std::isfinite(position.y);
}

bool Drive(const Matrix& matrix, Vec2 player, Vec2 target, float stopDistance) {
    const float wx = target.x - player.x;
    const float wy = target.y - player.y;
    if (wx * wx + wy * wy <= stopDistance * stopDistance) {
        Stop();
        return false;
    }
    noclip::NoteMoveTarget(target.x, target.y);
    ImVec2 screen{};
    if (!Project(matrix, target, screen)) {
        Stop();
        return false;
    }
    float dx = screen.x - kViewScale * static_cast<float>(g_viewW) * 0.5f;
    float dy = screen.y - static_cast<float>(g_viewH) * 0.5f;
    const float length = std::sqrt(dx * dx + dy * dy);
    if (!std::isfinite(length) || length < 0.001f) return false;
    dx /= length;
    dy /= length;
    constexpr float smoothing = 0.12f;
    if (!g_haveSmoothed) {
        g_smoothed = {dx, dy};
        g_haveSmoothed = true;
    } else {
        g_smoothed.x += (dx - g_smoothed.x) * smoothing;
        g_smoothed.y += (dy - g_smoothed.y) * smoothing;
    }
    float sx = g_smoothed.x;
    float sy = g_smoothed.y;
    constexpr float snap = 0.24f;
    if (std::fabs(sx) < std::fabs(sy) * snap) sx = 0.0f;
    if (std::fabs(sy) < std::fabs(sx) * snap) sy = 0.0f;
    constexpr float threshold = 0.18f;
    SetKey(g_left, sx < -threshold);
    SetKey(g_right, sx > threshold);
    SetKey(g_up, -sy > threshold);
    SetKey(g_down, -sy < -threshold);
    return true;
}

void DrawStatus(bool found, bool driving) {
    if (!g_cfg.puppeteerOverlay || !ImGui::GetCurrentContext()) return;
    char text[96]{};
    if (!g_cfg.followPlayerName[0])
        std::snprintf(text, sizeof(text), "Follow Player: enter a username");
    else if (!found)
        std::snprintf(text, sizeof(text), "Follow Player: %s not found",
                      g_cfg.followPlayerName);
    else
        std::snprintf(text, sizeof(text), "Following %s%s",
                      g_cfg.followPlayerName, driving ? "" : " (in range)");
    const ImVec2 display = ImGui::GetIO().DisplaySize;
    const ImVec2 size = ImGui::CalcTextSize(text);
    ImGui::GetForegroundDrawList()->AddText(
        ImVec2((display.x - size.x) * 0.5f, 18.0f),
        found ? IM_COL32(80, 210, 255, 255) : IM_COL32(255, 180, 70, 255), text);
}

} // namespace

void Tick() {
    if (!g_cfg.followPlayer || g_cfg.fameBot) {
        if (g_wasEnabled) Stop();
        g_wasEnabled = false;
        return;
    }
    g_wasEnabled = true;
    const uintptr_t world = game::Root();
    const uintptr_t player = game::Player();
    const HWND gameWindow = overlay::Window();
    if (!world || !player || !gameWindow || GetForegroundWindow() != gameWindow ||
        !ImGui::GetCurrentContext() || ImGui::GetIO().WantCaptureKeyboard) {
        Stop();
        return;
    }

    if (world != g_world || std::strncmp(g_lastName, g_cfg.followPlayerName,
                                         sizeof(g_lastName)) != 0) {
        g_world = world;
        g_target = 0;
        std::strncpy(g_lastName, g_cfg.followPlayerName, sizeof(g_lastName) - 1);
        g_lastName[sizeof(g_lastName) - 1] = '\0';
    }

    const ULONGLONG now = GetTickCount64();
    Vec2 targetPosition{};
    if (!TargetPosition(g_target, targetPosition) && now >= g_nextScan) {
        g_target = FindTarget(world, player);
        g_nextScan = now + 500;
    }
    const bool found = TargetPosition(g_target, targetPosition);
    if (!found) {
        Stop();
        DrawStatus(false, false);
        return;
    }

    Matrix matrix{};
    Vec2 playerPosition{};
    if (!game::CameraMatrix(matrix.column) || !TargetPosition(player, playerPosition)) {
        Stop();
        return;
    }
    const bool driving = Drive(matrix, playerPosition, targetPosition,
                               std::clamp(g_cfg.followPlayerDistance, 0.5f, 6.0f));
    DrawStatus(true, driving);
}

} // namespace follow
