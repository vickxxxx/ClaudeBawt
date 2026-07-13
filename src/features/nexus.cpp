


#include "features.h"
#include "config.h"
#include "il2cpp.h"
#include "log.h"

#include <windows.h>
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include "imgui.h"

namespace nexus {
namespace {

constexpr uintptr_t kNexusRva = 0x1E98CA0;
using NexusFn = int64_t (__fastcall*)(uintptr_t);

bool g_armed = true;
ULONGLONG g_messageUntil = 0;
char g_message[96]{};

void ShowMessage(const char* prefix, int hp) {
    std::snprintf(g_message, sizeof(g_message), "%s%d hp", prefix, hp);
    g_messageUntil = GetTickCount64() + 10000;
}

void ShowPercentMessage(float percent) {
    std::snprintf(g_message, sizeof(g_message),
                  "Nexusing: Too low of health: %.2f%%", percent);
    g_messageUntil = GetTickCount64() + 10000;
}

void RequestNexus(uintptr_t worldState, int predictedHp, float currentPercent,
                  bool serverSide) {
    if (!worldState)
        return;
    auto fn = reinterpret_cast<NexusFn>(ga::Rva(kNexusRva));
    if (!fn)
        return;


    fn(worldState);
    g_armed = false;
    if (g_cfg.autoNexusDisplay) {
        if (serverSide)
            ShowMessage("Nexus'd: About to be at ", predictedHp);
        else
            ShowPercentMessage(currentPercent);
    }
}

void DrawMessage() {
    if (!g_cfg.autoNexusDisplay || !g_message[0] ||
        GetTickCount64() >= g_messageUntil) {
        if (GetTickCount64() >= g_messageUntil)
            g_message[0] = '\0';
        return;
    }

    ImGui::SetNextWindowBgAlpha(0.45f);
    ImGui::Begin("##client_nexus", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
                 ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoSavedSettings);
    ImGui::TextUnformatted(g_message);
    ImGui::End();
}

}

void Install() {}


void Tick() {
    DrawMessage();
}


void Poll() {
    if (!g_cfg.autoNexus) {
        g_armed = true;
        return;
    }

    uintptr_t root = game::Root();
    uintptr_t player = game::Player();
    if (!root || !player) {
        g_armed = true;
        return;
    }

    int maxHp = *reinterpret_cast<int*>(player + 0x208);
    int hp = *reinterpret_cast<int*>(player + 0x20C);
    if (maxHp <= 0)
        return;


    bool low;
    float percent = std::clamp(g_cfg.autoNexusHpPercent, 0.0f, 99.99f);
    int threshold = std::min(static_cast<int>(g_cfg.autoNexusHpValue), maxHp - 1);
    if (g_cfg.autoNexusUsePercent) {
        low = (100.0f * static_cast<float>(hp) / static_cast<float>(maxHp)) <= percent;
    } else {
        low = hp < threshold;
    }

    static ULONGLONG lastLog = 0;
    ULONGLONG now = GetTickCount64();
    if (now - lastLog > 1000) {
        lastLog = now;
        DBLOG("nexus::Poll usePercent=%d hp=%d maxHp=%d pct=%.2f curPct=%.2f thr=%d low=%d armed=%d",
              (int)g_cfg.autoNexusUsePercent, hp, maxHp, percent,
              100.0f * (float)hp / (float)maxHp, threshold, (int)low, (int)g_armed);
    }

    if (!low) {
        g_armed = true;
        return;
    }

    uintptr_t worldState = *reinterpret_cast<uintptr_t*>(root + 0x28);
    if (g_armed)
        RequestNexus(worldState, hp,
                     100.0f * static_cast<float>(hp) / static_cast<float>(maxHp),
                     false);
}

}
