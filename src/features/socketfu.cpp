


#include "features.h"
#include "config.h"
#include "il2cpp.h"
#include "log.h"

#include <windows.h>
#include <cstdint>
#include "imgui.h"
#include "MinHook.h"

namespace socketfu {
namespace {

constexpr uintptr_t kMoveSpeedRva = 0x362770;

using SpeedFn = float(__fastcall*)(uintptr_t);
SpeedFn g_orig = nullptr;

bool      g_active = false;
bool      g_prev = false;
ULONGLONG g_sinceMs = 0;


float __fastcall hkMoveSpeed(uintptr_t self) {
    float r = g_orig ? g_orig(self) : 0.0f;
    if (g_active && g_cfg.socketFuRestrictMovement) {
        const float mult = speedhack::CurrentSpeed();
        if (mult > 1.5f)
            r = (1.5f / mult) * r;
    }
    return r;
}

}

void Install() {
    void* target = ga::Rva(kMoveSpeedRva);
    DBLOG("socketfu::Install: move-speed target=%p (GA+0x%llX)", target,
          (unsigned long long)kMoveSpeedRva);
    if (target) {
        const MH_STATUS st = MH_CreateHook(target, reinterpret_cast<void*>(&hkMoveSpeed),
                                           reinterpret_cast<void**>(&g_orig));
        DBLOG("socketfu::Install: MH_CreateHook=%d", (int)st);
    }
}

void Tick() {
    const bool active = g_cfg.socketFu;


    if (active && !g_prev) {
        g_sinceMs = GetTickCount64();
        if (g_cfg.socketFuUseSecondSpeed)
            g_cfg.useSpeed1 = false;
    }
    g_prev = active;
    g_active = active;


    noclip::SetManual(active && g_cfg.socketFuNoClip);

    if (active && g_cfg.showSocketFuTimer) {
        const double secs = static_cast<double>(GetTickCount64() - g_sinceMs) / 1000.0;
        ImGui::SetNextWindowBgAlpha(0.40f);
        ImGui::Begin("##client_socketfu", nullptr,
                     ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
                     ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoSavedSettings |
                     ImGuiWindowFlags_NoFocusOnAppearing);
        ImGui::Text("SocketFU: %.1f", secs);
        ImGui::End();
    }
}

}
