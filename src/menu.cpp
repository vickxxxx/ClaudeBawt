#include "menu.h"
#include "config.h"
#include "skin_catalog.h"
#include "imgui.h"

#include <windows.h>
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace menu {
    namespace {
        bool s_open = false;
        DWORD s_lastToggle = 0;
        int s_activeTab = 1;
        ImFont* s_normalFont = nullptr;
        ImFont* s_boldFont = nullptr;

        constexpr ImU32 kWhite = IM_COL32(245, 242, 245, 255);
        constexpr ImU32 kBlack = IM_COL32(25, 17, 25, 255);
        constexpr ImU32 kPurple = IM_COL32(194, 47, 174, 255);

        void PixelRect(ImDrawList* dl, ImVec2 p, float x, float y, float w, float h, ImU32 color) {
            dl->AddRectFilled(ImVec2(p.x + x, p.y + y), ImVec2(p.x + x + w, p.y + y + h), color);
        }

        void DrawIcon(ImDrawList* dl, ImVec2 p, int icon) {
            const float s = 1.0f;
            if (icon == 0) {
                dl->AddLine(ImVec2(p.x+5,p.y+5), ImVec2(p.x+25,p.y+25), kBlack, 7);
                dl->AddLine(ImVec2(p.x+25,p.y+5), ImVec2(p.x+7,p.y+25), kBlack, 7);
                dl->AddLine(ImVec2(p.x+5,p.y+5), ImVec2(p.x+25,p.y+25), IM_COL32(100,100,105,255), 3);
                dl->AddLine(ImVec2(p.x+25,p.y+5), ImVec2(p.x+7,p.y+25), kPurple, 3);
            } else if (icon == 1) {
                dl->AddTriangleFilled(ImVec2(p.x+16,p.y+3), ImVec2(p.x+3,p.y+12), ImVec2(p.x+29,p.y+12), kWhite);
                PixelRect(dl,p,5,12,22,4,kBlack); PixelRect(dl,p,7,14,4,12,kWhite);
                PixelRect(dl,p,14,14,4,12,kWhite); PixelRect(dl,p,21,14,4,12,kWhite);
                PixelRect(dl,p,4,26,24,4,kWhite);
            } else if (icon == 2) {
                dl->AddRect(ImVec2(p.x+3,p.y+3), ImVec2(p.x+12,p.y+12), kWhite, 2.0f, ImDrawFlags_None, 4.0f);
                dl->AddRect(ImVec2(p.x+20,p.y+3), ImVec2(p.x+29,p.y+12), kWhite, 2.0f, ImDrawFlags_None, 4.0f);
                dl->AddRect(ImVec2(p.x+3,p.y+20), ImVec2(p.x+12,p.y+29), kWhite, 2.0f, ImDrawFlags_None, 4.0f);
                dl->AddRect(ImVec2(p.x+20,p.y+20), ImVec2(p.x+29,p.y+29), kWhite, 2.0f, ImDrawFlags_None, 4.0f);
                dl->AddRectFilled(ImVec2(p.x+13,p.y+13), ImVec2(p.x+19,p.y+19), kPurple);
            } else if (icon == 3) {
                dl->AddTriangleFilled(ImVec2(p.x+4,p.y+3), ImVec2(p.x+5,p.y+27), ImVec2(p.x+12,p.y+19), kWhite);
                dl->AddLine(ImVec2(p.x+5,p.y+3), ImVec2(p.x+5,p.y+27), kBlack, 2);
                dl->AddText(ImVec2(p.x+14,p.y+4), kPurple, "TP");
            } else if (icon == 4) {
                const char* keys[] = {"W","A","S","D"};
                const ImVec2 pos[] = {{11,2},{2,16},{11,16},{20,16}};
                for (int i=0;i<4;++i) {
                    dl->AddRectFilled(ImVec2(p.x+pos[i].x,p.y+pos[i].y), ImVec2(p.x+pos[i].x+9,p.y+pos[i].y+12), kWhite, 1);
                    dl->AddText(ImVec2(p.x+pos[i].x+1,p.y+pos[i].y-1), kBlack, keys[i]);
                }
            } else if (icon == 5) {
                dl->AddRectFilled(ImVec2(p.x+5,p.y+5), ImVec2(p.x+21,p.y+27), kWhite);
                dl->AddRect(ImVec2(p.x+5,p.y+5), ImVec2(p.x+21,p.y+27), kBlack, 0.0f, ImDrawFlags_None, 3.0f);
                dl->AddLine(ImVec2(p.x+11,p.y+16), ImVec2(p.x+29,p.y+8), IM_COL32(62,210,79,255), 4);
                dl->AddTriangleFilled(ImVec2(p.x+29,p.y+8), ImVec2(p.x+20,p.y+7), ImVec2(p.x+27,p.y+16), IM_COL32(62,210,79,255));
            } else if (icon == 6 || icon == 7) {
                dl->AddCircleFilled(ImVec2(p.x+16,p.y+16), 13, kWhite, 12);
                dl->AddCircleFilled(ImVec2(p.x+16,p.y+16), 7, kBlack, 12);
                dl->AddCircleFilled(ImVec2(p.x+16,p.y+16), 4, kPurple, 12);
                for (int i=0;i<8;++i) {
                    const float a = i * 0.785398f;
                    ImVec2 c(p.x+16+std::cos(a)*13, p.y+16+std::sin(a)*13);
                    dl->AddRectFilled(ImVec2(c.x-3,c.y-3), ImVec2(c.x+3,c.y+3), kWhite);
                }
            } else {
                dl->AddCircleFilled(ImVec2(p.x+18,p.y+18), 10, kBlack, 10);
                dl->AddTriangleFilled(ImVec2(p.x+8,p.y+13), ImVec2(p.x+2,p.y+8), ImVec2(p.x+12,p.y+7), kBlack);
                dl->AddTriangleFilled(ImVec2(p.x+20,p.y+8), ImVec2(p.x+24,p.y+2), ImVec2(p.x+26,p.y+11), kBlack);
                PixelRect(dl,p,17,27,3,4,kBlack); PixelRect(dl,p,23,27,3,4,kBlack);
            }
            (void)s;
        }

        bool SidebarButton(const char* label, int tab, float width) {
            ImGui::PushID(tab);
            const ImVec2 pos = ImGui::GetCursorScreenPos();
            const ImVec2 size(width, 50.0f);
            const bool pressed = ImGui::InvisibleButton("##side", size);
            const bool hovered = ImGui::IsItemHovered();
            ImDrawList* dl = ImGui::GetWindowDrawList();
            ImU32 bg = s_activeTab == tab ? IM_COL32(239, 83, 188, 255)
                       : hovered ? IM_COL32(220, 65, 174, 255) : IM_COL32(205, 52, 160, 255);
            dl->AddRectFilled(pos, ImVec2(pos.x+size.x,pos.y+size.y), bg);
            ImFont* font = s_boldFont ? s_boldFont : ImGui::GetFont();
            dl->AddText(font, font->LegacySize, ImVec2(pos.x+17,pos.y+15), kWhite, label);
            if (pressed) s_activeTab = tab;
            ImGui::PopID();
            return pressed;
        }

        ImVec4 Color(const float (&v)[4]) { return ImVec4(v[0], v[1], v[2], v[3]); }

        const char* KeyName(int vk) {
            static char name[64];
            if (!vk) return "None";
            if (vk < 0) {
                static const char* mouse[] = { "LMB", "RMB", "MMB", "Mouse 4", "Mouse 5" };
                const int index = -vk - 1;
                return index >= 0 && index < 5 ? mouse[index] : "Mouse";
            }
            UINT scan = MapVirtualKeyA(static_cast<UINT>(vk), MAPVK_VK_TO_VSC) << 16;
            if (vk == VK_LEFT || vk == VK_UP || vk == VK_RIGHT || vk == VK_DOWN ||
                vk == VK_PRIOR || vk == VK_NEXT || vk == VK_END || vk == VK_HOME ||
                vk == VK_INSERT || vk == VK_DELETE || vk == VK_DIVIDE || vk == VK_NUMLOCK)
                scan |= 1u << 24;
            if (GetKeyNameTextA(static_cast<LONG>(scan), name, sizeof(name)) > 0) return name;
            std::snprintf(name, sizeof(name), "0x%02X", vk);
            return name;
        }

        void HotkeyWidget(const char* id, Keybind& bind, float width = 100.0f) {
            ImGui::PushID(id);
            ImGui::SetNextItemWidth(width);
            const char* label = bind.listening ? "..." : KeyName(bind.vk);
            if (ImGui::Button(label, ImVec2(width, 0.0f))) bind.listening = true;

            if (bind.listening) {
                if (ImGui::IsKeyPressed(ImGuiKey_Escape) || ImGui::IsKeyPressed(ImGuiKey_Backspace)) {
                    bind.vk = 0;
                    bind.listening = false;
                } else {
                    for (int mb = 0; mb < 5 && bind.listening; ++mb) {
                        if (ImGui::IsMouseClicked(static_cast<ImGuiMouseButton>(mb))) {
                            bind.vk = -(mb + 1);
                            bind.listening = false;
                        }
                    }
                    for (int vk = 0x08; vk <= 0xFE && bind.listening; ++vk) {
                        if (vk == VK_LBUTTON || vk == VK_RBUTTON || vk == VK_MBUTTON ||
                            vk == VK_XBUTTON1 || vk == VK_XBUTTON2) continue;
                        if (GetAsyncKeyState(vk) & 1) {
                            bind.vk = vk;
                            bind.listening = false;
                        }
                    }
                }
            }
            ImGui::PopID();
        }

        void LabelHotkey(const char* label, const char* id, Keybind& bind) {
            ImGui::TextUnformatted(label);
            ImGui::SameLine(145.0f);
            HotkeyWidget(id, bind);
        }

        void ModsTab() {
            ImGui::TextUnformatted("Mods");
            ImGui::Separator();
            ImGui::Checkbox("Use Speed 1 (Unticked Uses Speed 2)", &g_cfg.useSpeed1);
            ImGui::Checkbox("Show Current Speed", &g_cfg.showCurrentSpeed);
            ImGui::SliderFloat("Speedhack Speed 1", &g_cfg.speedhackSpeed1, 0.0f, 5.0f, "%.2f");
            ImGui::SliderFloat("Speedhack Speed 2", &g_cfg.speedhackSpeed2, 0.0f, 5.0f, "%.2f");
            ImGui::Checkbox("No Fog", &g_cfg.noFog);
            ImGui::Checkbox("SocketFu", &g_cfg.socketFu);
            ImGui::Checkbox("Show SocketFu Timer", &g_cfg.showSocketFuTimer);
            ImGui::Checkbox("Use Second Speed in SocketFu", &g_cfg.socketFuUseSecondSpeed);
            ImGui::Checkbox("Restrict My Movement Speed in SocketFu", &g_cfg.socketFuRestrictMovement);
            ImGui::Checkbox("NoClip When Socketed", &g_cfg.socketFuNoClip);
            ImGui::Checkbox("Auto NoClip (walk into walls)", &g_cfg.autoNoClip);
            ImGui::Checkbox("Show Current Fame Per Minute", &g_cfg.showFpm);
            ImGui::SliderFloat("Camera Zoom Scale", &g_cfg.cameraZoomScale, 0.25f, 5.0f, "%.2f");
            ImGui::Checkbox("Anti-Idle", &g_cfg.antiIdle);
        }

        void NexusTab() {
            ImGui::TextUnformatted("Auto Nexus Settings");
            ImGui::Separator();
            ImGui::TextWrapped("This is all experimental, auto nexus might fail, YOU MAY DIE!");
            ImGui::TextWrapped("This will detect and respond when server side hits happen.");
            ImGui::TextWrapped("This will NOT save you from server side hit death.");
            ImGui::TextWrapped("Not advised to set to low values because of server side hits.");
            ImGui::Spacing();
            ImGui::Checkbox("Auto Nexus Enabled", &g_cfg.autoNexus);
            ImGui::SliderFloat("Health Value", &g_cfg.autoNexusHpValue, 0.0f, 1000.0f, "%.0f");
            ImGui::Checkbox("Use Health Percent Instead of Value", &g_cfg.autoNexusUsePercent);
            ImGui::SliderFloat("Health Percent", &g_cfg.autoNexusHpPercent, 0.0f, 99.99f, "%.2f");
            ImGui::Checkbox("Display When Nexus'd", &g_cfg.autoNexusDisplay);
        }

        void AimTab() {
            ImGui::TextUnformatted("Auto Aim Settings");
            ImGui::Separator();
            LabelHotkey("Toggle Key", "aimbotHotkey", g_cfg.aimbotHotkey);
            ImGui::Checkbox("Auto Aim  ", &g_cfg.autoAim);
            static const char* styles[] = { "Distance", "Cursor", "Health" };
            ImGui::Combo("Targeting Style", &g_cfg.targetingStyle, styles, 3);
            ImGui::Checkbox("Magnet Aim", &g_cfg.magnetAim);
            ImGui::Checkbox("Magnet Aim Range Extension", &g_cfg.magnetRangeExt);
            ImGui::SliderFloat("\nMagnet Aim Range (Ctrl + Click to type)",
                               &g_cfg.magnetAimRange, 1.0f, 2.25f, "%.3f");
            ImGui::Checkbox("Projectile No Clip", &g_cfg.projectileNoClip);
            ImGui::Checkbox("Render Aim Info", &g_cfg.renderAimInfo);
        }

        void DodgeTab() {
            ImGui::TextUnformatted("Auto Dodge Settings");
            ImGui::Separator();
            ImGui::Checkbox("Dodge Projectiles", &g_cfg.dodgeProjectiles);
            ImGui::Checkbox("Hold To Toggle\t", &g_cfg.dodgeHoldToToggle);
            ImGui::SameLine();
            HotkeyWidget("dodgingHotkey", g_cfg.dodgingHotkey);
            ImGui::Checkbox("Dodge Invisible", &g_cfg.dodgeInvisible);
            ImGui::Checkbox("Butter Walk (more likely to take SERVER side hits)", &g_cfg.butterWalk);
            ImGui::SliderFloat("Hit Box Size", &g_cfg.dodgeHitboxSize, 0.451f, 0.501f, "%.3f");
            ImGui::SliderInt("Move Away Buffer (ms)", &g_cfg.dodgeMoveAwayMs, 0, 1000);
            ImGui::Checkbox("Dodge AoE/Bombs", &g_cfg.dodgeAoeBombs);
            ImGui::Checkbox("Avoid Units", &g_cfg.dodgeAvoidUnits);
            ImGui::SliderFloat("Unit Avoidance Scale", &g_cfg.dodgeUnitAvoidanceScale, 0.0f, 1.5f, "%.2f");
            ImGui::SliderFloat("Keep Distance From Enemies (tiles)", &g_cfg.dodgeKeepDistance, 0.0f, 8.0f, "%.1f");
            ImGui::Checkbox("Old Dodge Logic (don't use this)", &g_cfg.oldDodgeLogic);
        }

        void FollowTab() {
            ImGui::TextUnformatted("Follow / Teleport Settings");
            ImGui::Separator();
            LabelHotkey("Set Anchor (capture)", "tpCaptureHotkey", g_cfg.tpCaptureHotkey);
            LabelHotkey("Teleport to Anchor", "tpReturnHotkey", g_cfg.tpReturnHotkey);
            ImGui::Spacing();
            ImGui::Checkbox("Teleport if Out of Range", &g_cfg.teleportIfOutOfRange);
            ImGui::SliderFloat("Teleport Max Distance", &g_cfg.dogeTeleportMax, 0.0f, 1.5f, "%.2f");
            ImGui::TextWrapped("With Teleport if Out of Range on, you snap back to the "
                               "anchor whenever you drift past Max Distance tiles from it.");
            ImGui::Checkbox("Nexus When Lost", &g_cfg.nexusWhenLost);
        }

        void RenderTab() {
            ImGui::TextUnformatted("Render Options Settings");
            ImGui::Separator();
            if (ImGui::BeginTabBar("##RenderNestedTab")) {
                if (ImGui::BeginTabItem("POI/Bags")) {
                    ImGui::Checkbox("Enable POIs + Bags", &g_cfg.enablePoisBags);
                    ImGui::Checkbox("Play sound.wav For Enabled Bags", &g_cfg.playSoundForBags);
                    ImGui::TextUnformatted("== Bags ==");
                    ImGui::Checkbox("Egg", &g_cfg.bagEgg); ImGui::SameLine();
                    ImGui::Checkbox("Brown", &g_cfg.bagBrown); ImGui::SameLine();
                    ImGui::Checkbox("Pink", &g_cfg.bagPink); ImGui::SameLine();
                    ImGui::Checkbox("Purple", &g_cfg.bagPurple);
                    ImGui::Checkbox("Cyan", &g_cfg.bagCyan); ImGui::SameLine();
                    ImGui::Checkbox("Dark Blue", &g_cfg.bagDarkBlue); ImGui::SameLine();
                    ImGui::Checkbox("White", &g_cfg.bagWhite);
                    ImGui::Checkbox("Gold", &g_cfg.bagGold); ImGui::SameLine();
                    ImGui::Checkbox("Orange", &g_cfg.bagOrange); ImGui::SameLine();
                    ImGui::Checkbox("Red", &g_cfg.bagRed);
                    ImGui::EndTabItem();
                }
                if (ImGui::BeginTabItem("Other")) {
                    ImGui::Checkbox("Render Projectiles", &g_cfg.renderProjectiles);
                    ImGui::Checkbox("Render AOE Debug", &g_cfg.renderAoeDebug);
                    ImGui::Checkbox("Render Tiles", &g_cfg.renderTiles);
                    ImGui::Checkbox("Render Units", &g_cfg.renderUnits);
                    ImGui::Checkbox("Render Hitbox", &g_cfg.renderHitbox);
                    ImGui::Checkbox("Render Grid", &g_cfg.renderGrid);
                    ImGui::Checkbox("Render Safety Path", &g_cfg.renderSafetyPath);
                    ImGui::EndTabItem();
                }
                if (ImGui::BeginTabItem("Menu Theme")) {
                    ImGui::TextUnformatted("Customize Theme Colors:");
                    ImGui::Checkbox("Menu Background", &g_cfg.menuBackground);
                    ImGui::Checkbox("Title Bar Active", &g_cfg.titleBarActive);
                    ImGui::Checkbox("Side Bar Background", &g_cfg.sideBarBackground);
                    ImGui::ColorEdit4("Base", g_cfg.colorBase);
                    ImGui::ColorEdit4("Hover", g_cfg.colorHover);
                    ImGui::ColorEdit4("Active", g_cfg.colorActive);
                    ImGui::ColorEdit4("Check", g_cfg.colorCheck);
                    ImGui::ColorEdit4("Text Color", g_cfg.colorText);
                    ImGui::EndTabItem();
                }
                ImGui::EndTabBar();
            }
        }

        void SpooferTab() {
            ImGui::TextUnformatted("Spoofing Settings");
            ImGui::Separator();
            ImGui::TextUnformatted("Identity");
            ImGui::Checkbox("Name", &g_cfg.spoofName);
            ImGui::InputText("New Name", g_cfg.spoofNameValue,
                             sizeof(g_cfg.spoofNameValue));
            ImGui::TextDisabled("Client-side display spoof; longer names are truncated.");
            ImGui::Checkbox("Guild Name", &g_cfg.spoofGuildName);
            ImGui::InputText("New Guild Name", g_cfg.guildNameValue,
                             sizeof(g_cfg.guildNameValue));
            ImGui::TextDisabled("Only works while in a guild; capped to your real name length.");
            ImGui::Checkbox("Guild Rank", &g_cfg.spoofGuildRank);
            if (g_cfg.spoofGuildRank) {
                const char* ranks[] = { "Initiate", "Member", "Officer",
                                        "Leader", "Founder" };
                ImGui::Combo("Rank", &g_cfg.guildRankValue, ranks, 5);
            }
            ImGui::Checkbox("Stars", &g_cfg.stars);
            if (g_cfg.stars)
                ImGui::SliderInt("Stars Value", &g_cfg.starsValue, 0, 100);
            ImGui::Separator();
            ImGui::TextUnformatted("Appearance");
            ImGui::Checkbox("Skin Changer", &g_cfg.skinChanger);
            ImGui::InputInt("Skin ID", &g_cfg.skinId);
            skin_catalog::Render(g_cfg.skinId);
            ImGui::Checkbox("Dye Changer", &g_cfg.dyeChanger);
            ImGui::InputInt("Dye/Cloth ID", &g_cfg.dyeId);
            ImGui::Checkbox("Accessory Dye Changer", &g_cfg.accessoryDyeChanger);
            ImGui::InputInt("Accessory Dye/Cloth ID", &g_cfg.accessoryDyeId);
            ImGui::TextDisabled("IDs from realm.wiki; client-side only.");
            ImGui::Separator();
            ImGui::Checkbox("Enable Glow", &g_cfg.enableGlow);
            ImGui::ColorEdit4("Outline", g_cfg.glowOutline);
            ImGui::ColorEdit4("Glow", g_cfg.glowColor);
            ImGui::Checkbox("Rainbow Glow", &g_cfg.rainbowGlow);
            ImGui::Checkbox("Fame Value", &g_cfg.fameValue);
            if (g_cfg.fameValue) ImGui::InputFloat("##FameValue", &g_cfg.fameValueAmount);
            ImGui::Checkbox("Account Fame", &g_cfg.accountFame);
            if (g_cfg.accountFame) ImGui::InputFloat("Account Fame Value", &g_cfg.accountFameValue);
        }

        void MiscTab() {
            static Keybind menuBind;
            static int syncedVk = -1;
            if (!menuBind.listening && syncedVk != g_cfg.menuToggleHotkey) {
                menuBind.vk = g_cfg.menuToggleHotkey;
                syncedVk = g_cfg.menuToggleHotkey;
            }

            ImGui::TextUnformatted("Debug / Misc Settings");
            ImGui::Separator();
            ImGui::TextUnformatted("Menu Toggle Key");
            ImGui::SameLine(145.0f);
            HotkeyWidget("menuToggleHotkey", menuBind);
            g_cfg.menuToggleHotkey = menuBind.vk > 0 ? menuBind.vk : 0;
            syncedVk = g_cfg.menuToggleHotkey;
            ImGui::SliderFloat("Doge Teleport Max", &g_cfg.dogeTeleportMax, 0.0f, 1.5f, "%.2f");
            ImGui::TextDisabled("Config: %s", Config_Path());
        }
    }

    void Toggle() {
        const DWORD now = GetTickCount();
        if (now - s_lastToggle < 120) return;
        s_lastToggle = now;
        s_open = !s_open;
    }

    void SetOpen(bool open) { s_open = open; }
    bool IsOpen() { return s_open; }
    void SetFonts(ImFont* normal, ImFont* bold) {
        s_normalFont = normal;
        s_boldFont = bold;
    }

    void ApplyTheme(int) {
        ImGui::StyleColorsDark();
        ImGuiStyle& style = ImGui::GetStyle();
        style.WindowRounding = 7.0f;
        style.ChildRounding = 0.0f;
        style.FrameRounding = 0.0f;
        style.GrabRounding = 0.0f;
        style.WindowPadding = ImVec2(0.0f, 0.0f);
        style.FramePadding = ImVec2(6.0f, 4.0f);
        style.ItemSpacing = ImVec2(7.0f, 5.0f);

        ImVec4* c = style.Colors;
        c[ImGuiCol_Text] = Color(g_cfg.colorText);
        c[ImGuiCol_WindowBg] = ImVec4(0.15f, 0.06f, 0.14f, 0.96f);
        c[ImGuiCol_ChildBg] = ImVec4(0.20f, 0.09f, 0.18f, 0.72f);
        c[ImGuiCol_TitleBg] = ImVec4(0.35f, 0.08f, 0.38f, 1.0f);
        c[ImGuiCol_TitleBgActive] = ImVec4(0.42f, 0.09f, 0.48f, 1.0f);
        c[ImGuiCol_FrameBg] = ImVec4(0.68f, 0.08f, 0.48f, 0.95f);
        c[ImGuiCol_FrameBgHovered] = ImVec4(0.86f, 0.18f, 0.65f, 1.0f);
        c[ImGuiCol_FrameBgActive] = ImVec4(0.95f, 0.28f, 0.72f, 1.0f);
        c[ImGuiCol_Button] = ImVec4(0.80f, 0.13f, 0.59f, 1.0f);
        c[ImGuiCol_ButtonHovered] = ImVec4(0.92f, 0.25f, 0.71f, 1.0f);
        c[ImGuiCol_ButtonActive] = ImVec4(0.72f, 0.08f, 0.50f, 1.0f);
        c[ImGuiCol_CheckMark] = ImVec4(0.18f, 0.05f, 0.20f, 1.0f);
        c[ImGuiCol_SliderGrab] = ImVec4(0.13f, 0.03f, 0.16f, 1.0f);
        c[ImGuiCol_SliderGrabActive] = ImVec4(0.28f, 0.05f, 0.30f, 1.0f);
        c[ImGuiCol_Header] = Color(g_cfg.colorBase);
        c[ImGuiCol_HeaderHovered] = Color(g_cfg.colorHover);
        c[ImGuiCol_HeaderActive] = Color(g_cfg.colorActive);
        c[ImGuiCol_Tab] = Color(g_cfg.colorSidebar);
        c[ImGuiCol_TabHovered] = Color(g_cfg.colorHover);
        c[ImGuiCol_TabSelected] = Color(g_cfg.colorActive);
    }

    void Render() {
        if (!s_open) return;
        ApplyTheme(g_cfg.menuTheme);

        const ImVec2 display = ImGui::GetIO().DisplaySize;
        const float scale = std::max(0.65f, std::min(display.x / 1920.0f, display.y / 1080.0f));
        ImGui::SetNextWindowSize(ImVec2(1120.0f * scale, 610.0f * scale), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSizeConstraints(ImVec2(760.0f * scale, 430.0f * scale), display);

        const ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar;
        if (!ImGui::Begin("##Client", &s_open, flags)) {
            ImGui::End();
            return;
        }

        const float winW = ImGui::GetWindowWidth();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImVec2 wp = ImGui::GetWindowPos();
        dl->AddRectFilled(wp, ImVec2(wp.x+winW,wp.y+22), IM_COL32(91,28,105,255), 7.0f, ImDrawFlags_RoundCornersTop);
        dl->AddText(ImVec2(wp.x+7,wp.y+3), kWhite, "Version Private Build");
        dl->AddText(ImVec2(wp.x+winW-18,wp.y+2), kWhite, "X");

        ImGui::SetCursorPos(ImVec2(14, 31));
        ImGui::SetCursorPos(ImVec2(17, 35));
        if (s_boldFont) ImGui::PushFont(s_boldFont);
        ImGui::SetWindowFontScale(1.65f);
        ImGui::TextUnformatted("Client");
        ImGui::SetWindowFontScale(1.0f);
        if (s_boldFont) ImGui::PopFont();
        ImGui::SetCursorPos(ImVec2(winW-137, 37));
        if (ImGui::Button("Save Config", ImVec2(124, 28))) Config_Save();

        const float sidebarW = 188.0f;
        ImGui::SetCursorPos(ImVec2(15, 82));
        ImGui::BeginChild("##sidebar", ImVec2(sidebarW, -12), false,
                          ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        const char* labels[] = {"Mods","Auto Nexus","Auto Aim","Lag Port","Auto Dodge","Follow","Render","Spoofing","Debug / Misc"};
        for (int i=0;i<9;++i) {
            SidebarButton(labels[i], i, sidebarW-17);
            ImGui::Dummy(ImVec2(0,5));
        }
        ImGui::EndChild();

        ImGui::SetCursorPos(ImVec2(sidebarW+29, 83));
        ImGui::BeginChild("##content", ImVec2(-18, -15), false);
        switch (s_activeTab) {
            case 0: ModsTab(); break;
            case 1: NexusTab(); break;
            case 2: AimTab(); break;
            case 3:
                ImGui::TextUnformatted("Lag Port Settings");
                ImGui::Separator();
                ImGui::Checkbox("Lag Port", &g_cfg.lagPort);
                LabelHotkey("Lag Port Key (hold)", "lagPortHotkey", g_cfg.lagPortHotkey);
                ImGui::TextWrapped("Holding the key freezes the client. Will lag client, "
                                   "don't use near projectiles otherwise you may die.");
                break;
            case 4: DodgeTab(); break;
            case 5: FollowTab(); break;
            case 6: RenderTab(); break;
            case 7: SpooferTab(); break;
            case 8: MiscTab(); break;
        }
        ImGui::EndChild();
        ImGui::End();
    }
}
