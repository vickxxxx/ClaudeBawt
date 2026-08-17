#include "notifications.h"

#include "features.h"
#include "config.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace notifications {
namespace {

struct Notice {
    uint64_t id = 0;
    std::string title;
    std::string message;
    ImU32 color = IM_COL32(135, 141, 218, 255);
    double created = 0.0;
    float duration = 4.0f;
};

struct ModuleStates {
    bool initialized = false;
    bool noClip = false;
    bool autoAim = false;
    bool magnetAim = false;
    bool autoDodge = false;
    bool socketFu = false;
    bool interactiveMap = false;
    bool projectileBreadcrumbs = false;
};

std::vector<Notice> g_active;
std::vector<Notice> g_history;
ModuleStates g_states;
uint64_t g_nextId = 1;

constexpr ImU32 kEnabled = IM_COL32(91, 207, 151, 255);
constexpr ImU32 kDisabled = IM_COL32(121, 127, 143, 255);
constexpr ImU32 kInfo = IM_COL32(135, 141, 218, 255);

void Track(bool& previous, bool current, const char* title) {
    if (previous == current) return;
    previous = current;
    Push(title, current ? "Enabled" : "Disabled",
         current ? kEnabled : kDisabled);
}

void SnapshotStates() {
    g_states.noClip = g_cfg.noclipEnabled;
    g_states.autoAim = g_cfg.autoAim;
    g_states.magnetAim = g_cfg.magnetAim;
    g_states.autoDodge = g_cfg.dodgeProjectiles;
    g_states.socketFu = g_cfg.socketFu;
    g_states.interactiveMap = g_cfg.interactiveMapEnabled;
    g_states.projectileBreadcrumbs = g_cfg.projectileBreadcrumbs;
}

void RenderToasts(double now) {
    g_active.erase(std::remove_if(g_active.begin(), g_active.end(),
        [now](const Notice& notice) {
            return now - notice.created >= notice.duration;
        }), g_active.end());

    if (!g_cfg.notificationCenter || g_active.empty()) return;

    const ImVec2 display = ImGui::GetIO().DisplaySize;
    float y = 76.0f;
    for (auto it = g_active.rbegin(); it != g_active.rend(); ++it) {
        const Notice& notice = *it;
        const float age = static_cast<float>(now - notice.created);
        const float remaining = std::max(0.0f, notice.duration - age);
        float alpha = 1.0f;
        if (age < 0.18f) alpha = std::clamp(age / 0.18f, 0.0f, 1.0f);
        if (remaining < 0.35f)
            alpha *= std::clamp(remaining / 0.35f, 0.0f, 1.0f);

        char windowName[64]{};
        std::snprintf(windowName, sizeof(windowName),
                      "##claude_notice_%llu",
                      static_cast<unsigned long long>(notice.id));
        ImGui::SetNextWindowPos(ImVec2(display.x - 18.0f, y),
                                ImGuiCond_Always, ImVec2(1.0f, 0.0f));
        ImGui::SetNextWindowSize(ImVec2(300.0f, 68.0f), ImGuiCond_Always);
        ImGui::SetNextWindowBgAlpha(0.96f * alpha);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 6.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(16.0f, 9.0f));
        ImGui::PushStyleColor(ImGuiCol_WindowBg,
                              ImVec4(0.045f, 0.052f, 0.064f, 0.96f * alpha));
        ImGui::PushStyleColor(ImGuiCol_Border,
                              ImVec4(0.22f, 0.24f, 0.30f, 0.85f * alpha));
        const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration |
            ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoSavedSettings |
            ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav;
        if (ImGui::Begin(windowName, nullptr, flags)) {
            const ImVec2 p = ImGui::GetWindowPos();
            ImDrawList* draw = ImGui::GetWindowDrawList();
            const ImU32 accent = (notice.color & ~IM_COL32_A_MASK) |
                (static_cast<ImU32>(255.0f * alpha) << IM_COL32_A_SHIFT);
            draw->AddRectFilled(ImVec2(p.x, p.y + 7.0f),
                                ImVec2(p.x + 3.0f, p.y + 61.0f),
                                accent, 2.0f);
            ImGui::TextUnformatted(notice.title.c_str());
            ImGui::TextColored(ImVec4(0.62f, 0.65f, 0.72f, alpha),
                               "%s", notice.message.c_str());
            const float progress = std::clamp(remaining / notice.duration,
                                              0.0f, 1.0f);
            draw->AddRectFilled(ImVec2(p.x, p.y + 66.0f),
                                ImVec2(p.x + 300.0f * progress, p.y + 68.0f),
                                accent);
        }
        ImGui::End();
        ImGui::PopStyleColor(2);
        ImGui::PopStyleVar(2);
        y += 76.0f;
    }
}

} // namespace

void Push(const char* title, const char* message, ImU32 color, float duration) {
    Notice notice{};
    notice.id = g_nextId++;
    notice.title = title ? title : "ClaudeBawt";
    notice.message = message ? message : "";
    notice.color = color;
    notice.created = ImGui::GetCurrentContext() ? ImGui::GetTime() : 0.0;
    notice.duration = duration > 0.0f
        ? duration
        : std::clamp(g_cfg.notificationDuration, 1.5f, 10.0f);
    g_history.push_back(notice);
    if (g_history.size() > 50)
        g_history.erase(g_history.begin(),
                        g_history.begin() + (g_history.size() - 50));
    if (g_cfg.notificationCenter)
        g_active.push_back(notice);
}

void Tick() {
    if (!ImGui::GetCurrentContext()) return;

    if (!g_states.initialized) {
        SnapshotStates();
        g_states.initialized = true;
        Push("ClaudeBawt", "Notification centre ready", kInfo, 3.0f);
    } else {
        Track(g_states.noClip, g_cfg.noclipEnabled, "NoClip");
        Track(g_states.autoAim, g_cfg.autoAim, "Auto Aim");
        Track(g_states.magnetAim, g_cfg.magnetAim, "Magnet Aim");
        Track(g_states.autoDodge, g_cfg.dodgeProjectiles, "Auto Dodge");
        Track(g_states.socketFu, g_cfg.socketFu, "SocketFu");
        Track(g_states.interactiveMap, g_cfg.interactiveMapEnabled,
              "Interactive Map");
        Track(g_states.projectileBreadcrumbs, g_cfg.projectileBreadcrumbs,
              "Projectile Breadcrumbs");
    }

    RenderToasts(ImGui::GetTime());
}

void RenderSettings() {
    ImGui::TextUnformatted("Notification Centre");
    ImGui::Separator();
    ImGui::Checkbox("Enable Notifications", &g_cfg.notificationCenter);
    ImGui::SliderFloat("Notification Duration", &g_cfg.notificationDuration,
                       1.5f, 10.0f, "%.1f sec");
    if (ImGui::Button("Test Notification"))
        Push("ClaudeBawt", "This is a test notification", kInfo);
    ImGui::SameLine();
    if (ImGui::Button("Clear History")) {
        g_history.clear();
        g_active.clear();
    }

    ImGui::TextDisabled("Recent events (%d/50)",
                        static_cast<int>(g_history.size()));
    ImGui::BeginChild("##notification_history", ImVec2(0.0f, 145.0f), true);
    if (g_history.empty()) {
        ImGui::TextDisabled("No notifications yet.");
    } else {
        for (auto it = g_history.rbegin(); it != g_history.rend(); ++it) {
            ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(it->color),
                               "%s", it->title.c_str());
            ImGui::SameLine();
            ImGui::TextDisabled("- %s", it->message.c_str());
        }
    }
    ImGui::EndChild();
}

} // namespace notifications
