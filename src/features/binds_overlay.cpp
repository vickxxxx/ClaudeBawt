#include "features.h"
#include "config.h"
#include "menu.h"

#include "imgui.h"
#include <windows.h>

#include <cstdio>
#include <string>
#include <vector>

namespace binds_overlay {
namespace {

struct Row {
    std::string name;
    std::string state;
};

bool KeyDown(int encodedVk) {
    int vk = encodedVk;
    if (vk < 0) {
        static constexpr int mouseKeys[] = {
            VK_LBUTTON, VK_RBUTTON, VK_MBUTTON, VK_XBUTTON1, VK_XBUTTON2
        };
        const int button = -vk - 1;
        if (button < 0 || button >= static_cast<int>(sizeof(mouseKeys) / sizeof(mouseKeys[0])))
            return false;
        vk = mouseKeys[button];
    }
    return vk > 0 && (GetAsyncKeyState(vk) & 0x8000) != 0;
}

std::string KeyName(int encodedVk) {
    if (!encodedVk) return "--";
    if (encodedVk < 0) {
        static const char* mouseNames[] = {"M1", "M2", "M3", "M4", "M5"};
        const int button = -encodedVk - 1;
        return button >= 0 && button < 5 ? mouseNames[button] : "Mouse";
    }

    char name[64]{};
    UINT scan = MapVirtualKeyA(static_cast<UINT>(encodedVk), MAPVK_VK_TO_VSC) << 16;
    if (encodedVk == VK_LEFT || encodedVk == VK_UP || encodedVk == VK_RIGHT ||
        encodedVk == VK_DOWN || encodedVk == VK_PRIOR || encodedVk == VK_NEXT ||
        encodedVk == VK_END || encodedVk == VK_HOME || encodedVk == VK_INSERT ||
        encodedVk == VK_DELETE || encodedVk == VK_DIVIDE || encodedVk == VK_NUMLOCK)
        scan |= 1u << 24;
    if (GetKeyNameTextA(static_cast<LONG>(scan), name, sizeof(name)) > 0)
        return name;
    std::snprintf(name, sizeof(name), "0x%02X", encodedVk);
    return name;
}

std::string StateText(int vk, const char* mode) {
    return "[" + KeyName(vk) + "] " + mode;
}

void GatherRows(std::vector<Row>& rows) {
    if (g_cfg.noclipHotkey.vk) {
        rows.push_back({"NoClip", StateText(g_cfg.noclipHotkey.vk, "toggle")});
    } else if (noclip::GateActive()) {
        rows.push_back({"NoClip", "[active] auto"});
    }

    if (g_cfg.magnetAim && g_cfg.aimbotHotkey.vk)
        rows.push_back({"Magnet Aim", StateText(g_cfg.aimbotHotkey.vk, "toggle")});

    if (g_cfg.dodgeProjectiles && g_cfg.dodgingHotkey.vk)
        rows.push_back({"Auto Dodge", StateText(g_cfg.dodgingHotkey.vk,
            g_cfg.dodgeHoldToToggle ? "hold" : "toggle")});

    if (g_cfg.socketFu)
        rows.push_back({"SocketFU", StateText(g_cfg.socketFuHotkey.vk, "toggle")});

    if (g_cfg.puppeteerEnabled)
        rows.push_back({"Puppeteer",
                        StateText(g_cfg.puppeteerHotkey.vk, "toggle")});

    if (g_cfg.fameBot)
        rows.push_back({"Fame Bot",
                        StateText(g_cfg.fameBotHotkey.vk, "toggle")});

    const int speedKey = g_cfg.speedToggleKey ? g_cfg.speedToggleKey
                                               : g_cfg.speedHackHotkey;
    if (!g_cfg.useSpeed1 && speedKey)
        rows.push_back({"Speed 2", StateText(speedKey, "toggle")});

}

} // namespace

void Tick() {
    if (!g_cfg.showBindsOverlay)
        return;

    std::vector<Row> rows;
    rows.reserve(8);
    GatherRows(rows);
    if (rows.empty() && !menu::IsOpen())
        return;

    const float width = 218.0f;
    const float headerHeight = 27.0f;
    const float rowHeight = 20.0f;
    const size_t visibleRows = rows.empty() ? 1 : rows.size();
    const float height = headerHeight + rowHeight * static_cast<float>(visibleRows) + 5.0f;

    ImGui::SetNextWindowPos(ImVec2(14.0f, 105.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(width, height), ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration |
        ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoFocusOnAppearing;
    if (!ImGui::Begin("##binds_overlay", nullptr, flags)) {
        ImGui::End();
        ImGui::PopStyleVar();
        return;
    }

    const ImVec2 p = ImGui::GetWindowPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImGui::SetCursorScreenPos(p);
    ImGui::InvisibleButton("##binds_drag_handle", ImVec2(width, headerHeight));
    const bool dragging = ImGui::IsItemActive() &&
                          ImGui::IsMouseDragging(ImGuiMouseButton_Left);
    dl->AddRectFilled(p, ImVec2(p.x + width, p.y + height),
                      IM_COL32(9, 9, 13, 218), 3.0f);
    dl->AddRectFilled(p, ImVec2(p.x + width, p.y + 2.0f),
                      IM_COL32(152, 91, 224, 255), 3.0f,
                      ImDrawFlags_RoundCornersTop);
    dl->AddRectFilled(ImVec2(p.x + 8.0f, p.y + 8.0f),
                      ImVec2(p.x + 18.0f, p.y + 18.0f),
                      IM_COL32(114, 73, 201, 255), 2.0f);
    dl->AddRect(ImVec2(p.x + 10.0f, p.y + 10.0f),
                ImVec2(p.x + 16.0f, p.y + 16.0f),
                IM_COL32(225, 211, 255, 255), 1.0f);
    dl->AddText(ImVec2(p.x + 24.0f, p.y + 6.0f),
                IM_COL32(242, 240, 248, 255), "Binds");
    dl->AddLine(ImVec2(p.x + 7.0f, p.y + headerHeight),
                ImVec2(p.x + width - 7.0f, p.y + headerHeight),
                IM_COL32(255, 255, 255, 24), 1.0f);

    if (rows.empty()) {
        dl->AddText(ImVec2(p.x + 10.0f, p.y + headerHeight + 3.0f),
                    IM_COL32(135, 132, 145, 220), "No active binds");
    } else {
        for (size_t i = 0; i < rows.size(); ++i) {
            const float y = p.y + headerHeight + 3.0f + rowHeight * static_cast<float>(i);
            dl->AddText(ImVec2(p.x + 10.0f, y), IM_COL32(218, 215, 225, 255),
                        rows[i].name.c_str());
            const ImVec2 stateSize = ImGui::CalcTextSize(rows[i].state.c_str());
            dl->AddText(ImVec2(p.x + width - 10.0f - stateSize.x, y),
                        IM_COL32(169, 126, 232, 255), rows[i].state.c_str());
        }
    }

    if (dragging) {
        const ImVec2 delta = ImGui::GetIO().MouseDelta;
        ImGui::SetWindowPos(ImVec2(p.x + delta.x, p.y + delta.y),
                            ImGuiCond_Always);
    }

    ImGui::End();
    ImGui::PopStyleVar();
}

} // namespace binds_overlay
