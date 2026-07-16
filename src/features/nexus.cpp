


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

ULONGLONG g_messageUntil = 0;
char g_message[96]{};

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
    // Disabled until the current EscapePacket sender and object offsets are verified.
    return;
}

}
