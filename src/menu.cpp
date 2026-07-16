#include "menu.h"
#include "config.h"
#include "skin_catalog.h"
#include "features/notifications.h"
#include "imgui.h"

#include <windows.h>
#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdio>

namespace menu {
    namespace {
        bool s_open = false;
        DWORD s_lastToggle = 0;
        int s_activeTab = 1;
        ImFont* s_normalFont = nullptr;
        ImFont* s_boldFont = nullptr;
        ImTextureID s_logoTexture = 0;
        ImVec2 s_logoSize(0.0f, 0.0f);

        struct Snowflake {
            float x;
            float y;
            float speed;
            float drift;
            float phase;
            float size;
            float alpha;
        };
        Snowflake s_snow[120]{};
        bool s_snowReady = false;
        unsigned int s_snowSeed = 0xC1A0DEu;

        struct CursorSnowflake {
            ImVec2 position{};
            ImVec2 velocity{};
            float life = 0.0f;
            float maxLife = 0.0f;
            float size = 1.0f;
        };
        CursorSnowflake s_cursorSnow[96]{};
        int s_nextCursorSnow = 0;

        constexpr ImU32 kWhite = IM_COL32(229, 231, 238, 255);
        constexpr ImU32 kBlack = IM_COL32(20, 22, 27, 255);
        constexpr ImU32 kPurple = IM_COL32(126, 132, 205, 255);
        constexpr ImU32 kPanel = IM_COL32(12, 14, 17, 248);
        constexpr ImU32 kPanelAlt = IM_COL32(17, 20, 24, 250);
        constexpr ImU32 kAccent = IM_COL32(135, 141, 218, 255);
        constexpr ImU32 kAccentDim = IM_COL32(39, 43, 65, 245);
        constexpr ImU32 kMuted = IM_COL32(112, 117, 130, 255);

        float SnowRandom() {
            s_snowSeed ^= s_snowSeed << 13;
            s_snowSeed ^= s_snowSeed >> 17;
            s_snowSeed ^= s_snowSeed << 5;
            return static_cast<float>(s_snowSeed & 0x00FFFFFFu) / 16777215.0f;
        }

        void ResetSnowflake(Snowflake& flake, bool anywhere) {
            flake.x = SnowRandom();
            flake.y = anywhere ? SnowRandom() : -0.03f - SnowRandom() * 0.12f;
            flake.speed = 18.0f + SnowRandom() * 42.0f;
            flake.drift = 7.0f + SnowRandom() * 17.0f;
            flake.phase = SnowRandom() * 6.2831853f;
            flake.size = 0.8f + SnowRandom() * 1.7f;
            flake.alpha = 0.24f + SnowRandom() * 0.48f;
        }

        void DrawMenuSnow(ImDrawList* dl, const ImVec2& pos, const ImVec2& size) {
            if (!g_cfg.menuSnow || size.x <= 1.0f || size.y <= 1.0f) return;
            if (!s_snowReady) {
                for (Snowflake& flake : s_snow) ResetSnowflake(flake, true);
                s_snowReady = true;
            }

            const float dt = std::min(ImGui::GetIO().DeltaTime, 0.05f);
            const int count = std::clamp(static_cast<int>(80.0f * g_cfg.menuSnowIntensity), 12, 120);
            dl->PushClipRect(pos, ImVec2(pos.x + size.x, pos.y + size.y), true);
            for (int i = 0; i < count; ++i) {
                Snowflake& flake = s_snow[i];
                flake.phase += dt * (0.7f + flake.speed * 0.012f);
                flake.y += flake.speed * dt / size.y;
                flake.x += std::sin(flake.phase) * flake.drift * dt / size.x;
                if (flake.y > 1.03f) ResetSnowflake(flake, false);
                if (flake.x < -0.02f) flake.x = 1.02f;
                if (flake.x > 1.02f) flake.x = -0.02f;

                const ImVec2 p(pos.x + flake.x * size.x, pos.y + flake.y * size.y);
                const int alpha = static_cast<int>(255.0f * flake.alpha);
                dl->AddCircleFilled(p, flake.size,
                                    IM_COL32(225, 232, 255, alpha), 8);
                if (flake.size > 1.8f) {
                    dl->AddCircle(p, flake.size + 1.2f,
                                  IM_COL32(151, 166, 225, alpha / 5), 8, 1.0f);
                }
            }
            dl->PopClipRect();
        }

        ImVec2 BorderPoint(const ImVec2& pos, const ImVec2& size, float distance) {
            const float perimeter = 2.0f * (size.x + size.y);
            distance = std::fmod(distance + perimeter, perimeter);
            if (distance < size.x) return ImVec2(pos.x + distance, pos.y);
            distance -= size.x;
            if (distance < size.y) return ImVec2(pos.x + size.x, pos.y + distance);
            distance -= size.y;
            if (distance < size.x) return ImVec2(pos.x + size.x - distance, pos.y + size.y);
            distance -= size.x;
            return ImVec2(pos.x, pos.y + size.y - distance);
        }

        void DrawAnimatedBorder(ImDrawList* dl, const ImVec2& pos, const ImVec2& size) {
            if (!g_cfg.menuAnimatedBorder) return;
            const float perimeter = 2.0f * (size.x + size.y);
            const float head = std::fmod(static_cast<float>(ImGui::GetTime()) * 145.0f,
                                         perimeter);
            constexpr int segments = 38;
            constexpr float spacing = 4.5f;
            for (int i = segments - 1; i >= 0; --i) {
                const float fade = 1.0f - static_cast<float>(i) / segments;
                const ImVec2 a = BorderPoint(pos, size, head - i * spacing);
                const ImVec2 b = BorderPoint(pos, size, head - (i - 1) * spacing);
                if (std::fabs(a.x - b.x) > size.x * 0.5f ||
                    std::fabs(a.y - b.y) > size.y * 0.5f) continue;
                dl->AddLine(a, b,
                            IM_COL32(112, 120, 235, static_cast<int>(125.0f * fade)),
                            4.0f);
                dl->AddLine(a, b,
                            IM_COL32(213, 218, 255, static_cast<int>(230.0f * fade)),
                            1.35f);
            }
        }

        void DrawCrownShimmer(ImDrawList* dl, const ImVec2& min,
                              const ImVec2& max) {
            if (!g_cfg.menuCrownShimmer || !s_logoTexture) return;
            const float width = max.x - min.x;
            const float cycle = std::fmod(static_cast<float>(ImGui::GetTime()) * 0.72f,
                                          2.25f);
            if (cycle > 1.0f) return;
            const float center = min.x - width * 0.35f + cycle * width * 1.7f;
            const float bands[] = { 0.30f, 0.18f, 0.09f };
            const int alpha[] = { 36, 72, 150 };
            for (int i = 0; i < 3; ++i) {
                const float half = width * bands[i] * 0.5f;
                dl->PushClipRect(ImVec2(center - half, min.y),
                                 ImVec2(center + half, max.y), true);
                dl->AddImage(s_logoTexture, min, max, ImVec2(0, 0), ImVec2(1, 1),
                             IM_COL32(255, 249, 205, alpha[i]));
                dl->PopClipRect();
            }
        }

        void SpawnCursorSnow(const ImVec2& mouse, const ImVec2& delta) {
            const float movement = std::sqrt(delta.x * delta.x + delta.y * delta.y);
            const int spawnCount = std::clamp(static_cast<int>(movement * 0.45f), 1, 6);
            for (int i = 0; i < spawnCount; ++i) {
                CursorSnowflake& flake = s_cursorSnow[s_nextCursorSnow];
                s_nextCursorSnow = (s_nextCursorSnow + 1) % 96;
                flake.position = ImVec2(mouse.x + (SnowRandom() - 0.5f) * 7.0f,
                                        mouse.y + (SnowRandom() - 0.5f) * 7.0f);
                flake.velocity = ImVec2(-delta.x * (1.0f + SnowRandom()) +
                                            (SnowRandom() - 0.5f) * 22.0f,
                                        -delta.y * 0.35f + 16.0f + SnowRandom() * 30.0f);
                flake.maxLife = 0.35f + SnowRandom() * 0.48f;
                flake.life = flake.maxLife;
                flake.size = 0.7f + SnowRandom() * 1.6f;
            }
        }

        void DrawMenuCrosshair(const ImVec2& menuPos, const ImVec2& menuSize,
                               bool hovered) {
            if (!hovered || !g_cfg.menuCustomCrosshair) return;
            ImGui::SetMouseCursor(ImGuiMouseCursor_None);

            ImGuiIO& io = ImGui::GetIO();
            const float movement = std::sqrt(io.MouseDelta.x * io.MouseDelta.x +
                                             io.MouseDelta.y * io.MouseDelta.y);
            if (g_cfg.menuCursorSnowTrail && movement > 0.7f)
                SpawnCursorSnow(io.MousePos, io.MouseDelta);

            ImDrawList* draw = ImGui::GetForegroundDrawList();
            if (!draw) return;
            const float dt = std::min(io.DeltaTime, 0.05f);
            draw->PushClipRect(menuPos,
                               ImVec2(menuPos.x + menuSize.x, menuPos.y + menuSize.y),
                               true);
            for (CursorSnowflake& flake : s_cursorSnow) {
                if (flake.life <= 0.0f) continue;
                flake.life -= dt;
                if (flake.life <= 0.0f) continue;
                flake.velocity.y += 25.0f * dt;
                flake.position.x += flake.velocity.x * dt;
                flake.position.y += flake.velocity.y * dt;
                const float fade = std::clamp(flake.life / flake.maxLife, 0.0f, 1.0f);
                draw->AddCircleFilled(flake.position, flake.size,
                                      IM_COL32(227, 235, 255,
                                               static_cast<int>(190.0f * fade)), 8);
            }

            const ImVec2 p = io.MousePos;
            const ImU32 shadow = IM_COL32(8, 10, 14, 230);
            const ImU32 accent = IM_COL32(188, 197, 255, 255);
            const ImU32 centre = IM_COL32(255, 255, 255, 255);
            const float gap = 3.5f;
            const float length = 8.5f;
            draw->AddLine(ImVec2(p.x - gap - length, p.y), ImVec2(p.x - gap, p.y),
                          shadow, 4.0f);
            draw->AddLine(ImVec2(p.x + gap, p.y), ImVec2(p.x + gap + length, p.y),
                          shadow, 4.0f);
            draw->AddLine(ImVec2(p.x, p.y - gap - length), ImVec2(p.x, p.y - gap),
                          shadow, 4.0f);
            draw->AddLine(ImVec2(p.x, p.y + gap), ImVec2(p.x, p.y + gap + length),
                          shadow, 4.0f);
            draw->AddLine(ImVec2(p.x - gap - length, p.y), ImVec2(p.x - gap, p.y),
                          accent, 1.5f);
            draw->AddLine(ImVec2(p.x + gap, p.y), ImVec2(p.x + gap + length, p.y),
                          accent, 1.5f);
            draw->AddLine(ImVec2(p.x, p.y - gap - length), ImVec2(p.x, p.y - gap),
                          accent, 1.5f);
            draw->AddLine(ImVec2(p.x, p.y + gap), ImVec2(p.x, p.y + gap + length),
                          accent, 1.5f);
            draw->AddCircleFilled(p, 1.6f, centre, 8);
            draw->PopClipRect();
        }

        void PixelRect(ImDrawList* dl, ImVec2 p, float x, float y, float w, float h, ImU32 color) {
            dl->AddRectFilled(ImVec2(p.x + x, p.y + y), ImVec2(p.x + x + w, p.y + y + h), color);
        }

        void DrawIcon(ImDrawList* dl, ImVec2 p, int icon) {
            const float s = 1.0f;
            if (icon == 0) { // crossed tools
                dl->AddLine(ImVec2(p.x+5,p.y+5), ImVec2(p.x+25,p.y+25), kBlack, 7);
                dl->AddLine(ImVec2(p.x+25,p.y+5), ImVec2(p.x+7,p.y+25), kBlack, 7);
                dl->AddLine(ImVec2(p.x+5,p.y+5), ImVec2(p.x+25,p.y+25), IM_COL32(100,100,105,255), 3);
                dl->AddLine(ImVec2(p.x+25,p.y+5), ImVec2(p.x+7,p.y+25), kPurple, 3);
            } else if (icon == 1) { // nexus temple
                dl->AddTriangleFilled(ImVec2(p.x+16,p.y+3), ImVec2(p.x+3,p.y+12), ImVec2(p.x+29,p.y+12), kWhite);
                PixelRect(dl,p,5,12,22,4,kBlack); PixelRect(dl,p,7,14,4,12,kWhite);
                PixelRect(dl,p,14,14,4,12,kWhite); PixelRect(dl,p,21,14,4,12,kWhite);
                PixelRect(dl,p,4,26,24,4,kWhite);
            } else if (icon == 2) { // aim reticle
                dl->AddRect(ImVec2(p.x+3,p.y+3), ImVec2(p.x+12,p.y+12), kWhite, 2.0f, ImDrawFlags_None, 4.0f);
                dl->AddRect(ImVec2(p.x+20,p.y+3), ImVec2(p.x+29,p.y+12), kWhite, 2.0f, ImDrawFlags_None, 4.0f);
                dl->AddRect(ImVec2(p.x+3,p.y+20), ImVec2(p.x+12,p.y+29), kWhite, 2.0f, ImDrawFlags_None, 4.0f);
                dl->AddRect(ImVec2(p.x+20,p.y+20), ImVec2(p.x+29,p.y+29), kWhite, 2.0f, ImDrawFlags_None, 4.0f);
                dl->AddRectFilled(ImVec2(p.x+13,p.y+13), ImVec2(p.x+19,p.y+19), kPurple);
            } else if (icon == 3) { // cursor + TP
                dl->AddTriangleFilled(ImVec2(p.x+4,p.y+3), ImVec2(p.x+5,p.y+27), ImVec2(p.x+12,p.y+19), kWhite);
                dl->AddLine(ImVec2(p.x+5,p.y+3), ImVec2(p.x+5,p.y+27), kBlack, 2);
                dl->AddText(ImVec2(p.x+14,p.y+4), kPurple, "TP");
            } else if (icon == 4) { // WASD
                const char* keys[] = {"W","A","S","D"};
                const ImVec2 pos[] = {{11,2},{2,16},{11,16},{20,16}};
                for (int i=0;i<4;++i) {
                    dl->AddRectFilled(ImVec2(p.x+pos[i].x,p.y+pos[i].y), ImVec2(p.x+pos[i].x+9,p.y+pos[i].y+12), kWhite, 1);
                    dl->AddText(ImVec2(p.x+pos[i].x+1,p.y+pos[i].y-1), kBlack, keys[i]);
                }
            } else if (icon == 5) { // follow: original-style door/arrow, not generated sprite #6
                dl->AddRectFilled(ImVec2(p.x+5,p.y+5), ImVec2(p.x+21,p.y+27), kWhite);
                dl->AddRect(ImVec2(p.x+5,p.y+5), ImVec2(p.x+21,p.y+27), kBlack, 0.0f, ImDrawFlags_None, 3.0f);
                dl->AddLine(ImVec2(p.x+11,p.y+16), ImVec2(p.x+29,p.y+8), IM_COL32(62,210,79,255), 4);
                dl->AddTriangleFilled(ImVec2(p.x+29,p.y+8), ImVec2(p.x+20,p.y+7), ImVec2(p.x+27,p.y+16), IM_COL32(62,210,79,255));
            } else if (icon == 6 || icon == 7) { // gears
                dl->AddCircleFilled(ImVec2(p.x+16,p.y+16), 13, kWhite, 12);
                dl->AddCircleFilled(ImVec2(p.x+16,p.y+16), 7, kBlack, 12);
                dl->AddCircleFilled(ImVec2(p.x+16,p.y+16), 4, kPurple, 12);
                for (int i=0;i<8;++i) {
                    const float a = i * 0.785398f;
                    ImVec2 c(p.x+16+std::cos(a)*13, p.y+16+std::sin(a)*13);
                    dl->AddRectFilled(ImVec2(c.x-3,c.y-3), ImVec2(c.x+3,c.y+3), kWhite);
                }
            } else { // raven silhouette
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
            const ImVec2 size(width, 32.0f);
            const bool pressed = ImGui::InvisibleButton("##side", size);
            const bool hovered = ImGui::IsItemHovered();
            ImDrawList* dl = ImGui::GetWindowDrawList();
            const bool active = s_activeTab == tab;
            ImU32 bg = active ? IM_COL32(33, 37, 49, 255)
                       : hovered ? IM_COL32(25, 28, 34, 235) : IM_COL32(0, 0, 0, 0);
            dl->AddRectFilled(pos, ImVec2(pos.x+size.x,pos.y+size.y), bg, 4.0f);
            if (active) {
                dl->AddRectFilled(ImVec2(pos.x, pos.y+6), ImVec2(pos.x+3, pos.y+size.y-6), kAccent, 2.0f);
                dl->AddRect(ImVec2(pos.x, pos.y), ImVec2(pos.x+size.x,pos.y+size.y),
                            IM_COL32(135, 141, 218, 30), 4.0f, ImDrawFlags_None, 1.0f);
            }
            ImFont* font = s_boldFont ? s_boldFont : ImGui::GetFont();
            const float fontSize = font->LegacySize;
            const ImVec2 textSize = font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, label);
            dl->AddCircleFilled(ImVec2(pos.x + 18.0f, pos.y + size.y * 0.5f),
                                active ? 3.5f : 2.5f,
                                active ? kAccent : IM_COL32(75, 80, 92, 255), 12);
            const float textX = pos.x + 31.0f;
            const float textY = pos.y + (size.y - textSize.y) * 0.5f;
            dl->AddText(font, fontSize, ImVec2(textX, textY),
                        active ? kWhite : IM_COL32(160, 164, 176, 255), label);
            if (pressed) s_activeTab = tab;
            ImGui::PopID();
            return pressed;
        }

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

        void SocketFuTab() {
            ImGui::TextUnformatted("SocketFU Settings");
            ImGui::Separator();
            ImGui::Checkbox("SocketFu", &g_cfg.socketFu);
            LabelHotkey("Toggle Key", "socketFuHotkey", g_cfg.socketFuHotkey);
            ImGui::TextDisabled("Keeps the world and local movement running normally.");
            ImGui::Checkbox("Show SocketFu Timer", &g_cfg.showSocketFuTimer);
            ImGui::Checkbox("Use Second Speed in SocketFu", &g_cfg.socketFuUseSecondSpeed);
            ImGui::Checkbox("Restrict My Movement Speed in SocketFu", &g_cfg.socketFuRestrictMovement);
            ImGui::TextDisabled("SocketFU remains installed; the old Mods page has been removed.");
        }

        void AimTab() {
            ImGui::TextUnformatted("Auto Aim Settings");
            ImGui::Separator();
            LabelHotkey("Magnet Aim Toggle", "aimbotHotkey", g_cfg.aimbotHotkey);
            ImGui::Checkbox("Auto Aim  ", &g_cfg.autoAim);
            static const char* styles[] = { "Distance", "Cursor", "Health" };
            ImGui::Combo("Targeting Style", &g_cfg.targetingStyle, styles, 3);
            ImGui::Checkbox("Magnet Aim", &g_cfg.magnetAim);
            ImGui::Checkbox("Magnet Aim Range Extension", &g_cfg.magnetRangeExt);
            ImGui::SliderFloat("\nMagnet Aim Range (Ctrl + Click to type)",
                               &g_cfg.magnetAimRange, 1.0f, 2.25f, "%.3f");
            ImGui::Checkbox("Show Magnet Aim Range Circle", &g_cfg.renderMagnetRange);
            ImGui::Checkbox("Show Normal Aim Range Circle", &g_cfg.renderNormalAimRange);
            ImGui::TextDisabled("Normal range follows the equipped weapon's projectile reach.");
            ImGui::Checkbox("Projectile No Clip", &g_cfg.projectileNoClip);
            ImGui::Checkbox("Render Aim Info", &g_cfg.renderAimInfo);
        }

        void DodgeTab() {
            ImGui::TextUnformatted("Auto Dodge Settings");
            ImGui::Separator();
            ImGui::Checkbox("Dodge Projectiles", &g_cfg.dodgeProjectiles);
            ImGui::Checkbox("Hold Key To Suspend\t", &g_cfg.dodgeHoldToToggle);
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

        void RenderTab() {
            ImGui::TextUnformatted("Render Options Settings");
            ImGui::Separator();
            ImGui::Checkbox("Interactive Realm Map", &g_cfg.interactiveMapEnabled);
            ImGui::TextDisabled("Layered biome/portal map by ErelDev, used with permission.");
            ImGui::Spacing();
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
                    ImGui::Checkbox("Projectile Breadcrumbs", &g_cfg.projectileBreadcrumbs);
                    if (g_cfg.projectileBreadcrumbs) {
                        ImGui::SliderFloat("Breadcrumb Lifetime",
                                           &g_cfg.projectileBreadcrumbLifetime,
                                           0.15f, 2.0f, "%.2f sec");
                        ImGui::SliderFloat("Breadcrumb Thickness",
                                           &g_cfg.projectileBreadcrumbThickness,
                                           0.5f, 4.0f, "%.2f");
                    }
                    ImGui::Checkbox("Render AOE Debug", &g_cfg.renderAoeDebug);
                    if (g_cfg.renderAoeDebug) {
                        ImGui::SliderFloat("AOE Preview Radius", &g_cfg.aoeDebugRadius,
                                           0.25f, 4.0f, "%.2f tiles");
                        ImGui::Checkbox("AOE Countdown", &g_cfg.aoeDebugCountdown);
                        ImGui::TextDisabled("Preview radius is diagnostic and manually adjustable.");
                    }
                    ImGui::Checkbox("Render Tiles", &g_cfg.renderTiles);
                    ImGui::Checkbox("Render Units", &g_cfg.renderUnits);
                    ImGui::Checkbox("Render Hitbox", &g_cfg.renderHitbox);
                    ImGui::Checkbox("Render Grid", &g_cfg.renderGrid);
                    ImGui::Checkbox("Render Safety Path", &g_cfg.renderSafetyPath);
                    ImGui::EndTabItem();
                }
                if (ImGui::BeginTabItem("Menu Theme")) {
                    ImGui::TextUnformatted("Customize Theme Colors:");
                    ImGui::Checkbox("Animated Snow", &g_cfg.menuSnow);
                    if (g_cfg.menuSnow)
                        ImGui::SliderFloat("Snow Amount", &g_cfg.menuSnowIntensity,
                                           0.15f, 1.5f, "%.2f");
                    ImGui::Checkbox("Animated Accent Border", &g_cfg.menuAnimatedBorder);
                    ImGui::Checkbox("Crown Shimmer", &g_cfg.menuCrownShimmer);
                    ImGui::Checkbox("Custom Menu Crosshair", &g_cfg.menuCustomCrosshair);
                    if (g_cfg.menuCustomCrosshair)
                        ImGui::Checkbox("Crosshair Snow Trail", &g_cfg.menuCursorSnowTrail);
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

        void KeybindsTab() {
            static Keybind menuBind;
            static int syncedVk = -1;
            if (!menuBind.listening && syncedVk != g_cfg.menuToggleHotkey) {
                menuBind.vk = g_cfg.menuToggleHotkey;
                syncedVk = g_cfg.menuToggleHotkey;
            }

            ImGui::TextUnformatted("Hotkeys / Keybinds");
            ImGui::Separator();
            ImGui::Checkbox("Show Binds Overlay", &g_cfg.showBindsOverlay);
            ImGui::TextDisabled("Drag the Binds header to move the overlay.");
            ImGui::Spacing();
            LabelHotkey("Menu Toggle", "menuToggleHotkey", menuBind);
            g_cfg.menuToggleHotkey = menuBind.vk > 0 ? menuBind.vk : 0;
            syncedVk = g_cfg.menuToggleHotkey;
            LabelHotkey("NoClip Toggle", "noclipHotkey", g_cfg.noclipHotkey);
            ImGui::SameLine();
            ImGui::Checkbox("Enabled##noclip", &g_cfg.noclipEnabled);
            LabelHotkey("Magnet Aim Toggle", "aimbotHotkeyAll", g_cfg.aimbotHotkey);
            LabelHotkey("SocketFU Toggle", "socketFuHotkeyAll", g_cfg.socketFuHotkey);
            ImGui::Spacing();
            ImGui::TextWrapped("NoClip stays enabled until its key is pressed again. "
                               "Press Backspace or Escape while selecting a bind to clear it.");
        }

        void MiscTab() {
            ImGui::TextUnformatted("Debug / Misc Settings");
            ImGui::Separator();
            ImGui::TextDisabled("Config: %s", Config_Path());
            ImGui::Spacing();
            notifications::RenderSettings();
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
    void SetLogo(void* texture, float width, float height) {
        s_logoTexture = reinterpret_cast<ImTextureID>(texture);
        s_logoSize = ImVec2(width, height);
    }

    void ApplyTheme(int) {
        ImGui::StyleColorsDark();
        ImGuiStyle& style = ImGui::GetStyle();
        style.WindowRounding = 7.0f;
        style.ChildRounding = 5.0f;
        style.FrameRounding = 4.0f;
        style.GrabRounding = 8.0f;
        style.PopupRounding = 5.0f;
        style.ScrollbarRounding = 8.0f;
        style.TabRounding = 4.0f;
        style.WindowPadding = ImVec2(0.0f, 0.0f);
        style.FramePadding = ImVec2(9.0f, 5.0f);
        style.ItemSpacing = ImVec2(8.0f, 8.0f);
        style.ItemInnerSpacing = ImVec2(8.0f, 5.0f);
        style.ScrollbarSize = 10.0f;

        ImVec4* c = style.Colors;
        c[ImGuiCol_Text] = ImVec4(0.88f, 0.89f, 0.93f, 1.00f);
        c[ImGuiCol_TextDisabled] = ImVec4(0.43f, 0.45f, 0.51f, 1.00f);
        c[ImGuiCol_WindowBg] = ImVec4(0.045f, 0.052f, 0.064f, 0.98f);
        c[ImGuiCol_ChildBg] = ImVec4(0.065f, 0.074f, 0.090f, 0.72f);
        c[ImGuiCol_PopupBg] = ImVec4(0.06f, 0.07f, 0.085f, 0.99f);
        c[ImGuiCol_Border] = ImVec4(0.20f, 0.22f, 0.28f, 0.55f);
        c[ImGuiCol_FrameBg] = ImVec4(0.085f, 0.095f, 0.115f, 1.00f);
        c[ImGuiCol_FrameBgHovered] = ImVec4(0.13f, 0.14f, 0.18f, 1.00f);
        c[ImGuiCol_FrameBgActive] = ImVec4(0.17f, 0.18f, 0.24f, 1.00f);
        c[ImGuiCol_Button] = ImVec4(0.12f, 0.13f, 0.17f, 1.00f);
        c[ImGuiCol_ButtonHovered] = ImVec4(0.22f, 0.23f, 0.33f, 1.00f);
        c[ImGuiCol_ButtonActive] = ImVec4(0.31f, 0.32f, 0.48f, 1.00f);
        c[ImGuiCol_CheckMark] = ImVec4(0.55f, 0.58f, 0.95f, 1.00f);
        c[ImGuiCol_SliderGrab] = ImVec4(0.48f, 0.51f, 0.82f, 1.00f);
        c[ImGuiCol_SliderGrabActive] = ImVec4(0.64f, 0.67f, 1.00f, 1.00f);
        c[ImGuiCol_Header] = ImVec4(0.15f, 0.16f, 0.22f, 1.00f);
        c[ImGuiCol_HeaderHovered] = ImVec4(0.23f, 0.24f, 0.35f, 1.00f);
        c[ImGuiCol_HeaderActive] = ImVec4(0.30f, 0.31f, 0.47f, 1.00f);
        c[ImGuiCol_Separator] = ImVec4(0.21f, 0.23f, 0.29f, 0.75f);
        c[ImGuiCol_Tab] = ImVec4(0.09f, 0.10f, 0.13f, 1.00f);
        c[ImGuiCol_TabHovered] = ImVec4(0.23f, 0.24f, 0.36f, 1.00f);
        c[ImGuiCol_TabSelected] = ImVec4(0.30f, 0.31f, 0.47f, 1.00f);
        c[ImGuiCol_ScrollbarBg] = ImVec4(0.04f, 0.045f, 0.055f, 0.70f);
        c[ImGuiCol_ScrollbarGrab] = ImVec4(0.20f, 0.21f, 0.27f, 0.90f);
        c[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.34f, 0.35f, 0.50f, 1.00f);
    }

    void Render() {
        if (!s_open) return;
        ApplyTheme(g_cfg.menuTheme);

        const ImVec2 display = ImGui::GetIO().DisplaySize;
        const float scale = std::max(0.65f, std::min(display.x / 1920.0f, display.y / 1080.0f));
        ImGui::SetNextWindowSize(ImVec2(940.0f * scale, 575.0f * scale), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSizeConstraints(ImVec2(790.0f * scale, 455.0f * scale), display);

        const ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar;
        if (!ImGui::Begin("##ClaudeBawt", &s_open, flags)) {
            ImGui::End();
            return;
        }

        const float winW = ImGui::GetWindowWidth();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImVec2 wp = ImGui::GetWindowPos();
        const float winH = ImGui::GetWindowHeight();
        constexpr float sidebarW = 190.0f;
        dl->AddRectFilled(wp, ImVec2(wp.x+winW, wp.y+winH), kPanel, 7.0f);
        dl->AddRect(wp, ImVec2(wp.x+winW, wp.y+winH), IM_COL32(74, 79, 95, 150), 7.0f);
        dl->AddRectFilled(wp, ImVec2(wp.x+sidebarW, wp.y+winH),
                          IM_COL32(15, 17, 21, 252), 7.0f,
                          ImDrawFlags_RoundCornersLeft);
        dl->AddLine(ImVec2(wp.x+sidebarW, wp.y), ImVec2(wp.x+sidebarW, wp.y+winH),
                    IM_COL32(48, 52, 64, 220), 1.0f);
        dl->AddLine(ImVec2(wp.x+sidebarW, wp.y+64), ImVec2(wp.x+winW, wp.y+64),
                    IM_COL32(42, 46, 57, 200), 1.0f);
        dl->AddRectFilled(ImVec2(wp.x+sidebarW+18, wp.y+78),
                          ImVec2(wp.x+winW-18, wp.y+winH-18), kPanelAlt, 5.0f);
        dl->AddRect(ImVec2(wp.x+sidebarW+18, wp.y+78),
                    ImVec2(wp.x+winW-18, wp.y+winH-18),
                    IM_COL32(48, 52, 65, 210), 5.0f);
        dl->AddRectFilled(ImVec2(wp.x+sidebarW+18, wp.y+78),
                          ImVec2(wp.x+sidebarW+21, wp.y+112), kAccent, 2.0f);
        DrawMenuSnow(dl, wp, ImVec2(winW, winH));
        DrawAnimatedBorder(dl, wp, ImVec2(winW, winH));
        ImGui::SetCursorPos(ImVec2(winW-33, 11));
        if (ImGui::InvisibleButton("##close_menu", ImVec2(24, 24))) {
            s_open = false;
        }
        const bool closeHovered = ImGui::IsItemHovered();
        dl->AddCircleFilled(ImVec2(wp.x+winW-21, wp.y+23), 8.0f,
                            closeHovered ? IM_COL32(135, 141, 218, 110) : IM_COL32(70, 75, 88, 100), 16);
        dl->AddText(ImVec2(wp.x+winW-25, wp.y+14), closeHovered ? kWhite : kMuted, "x");

        const float logoH = 27.0f;
        const float logoW = s_logoSize.y > 0.0f ? logoH * (s_logoSize.x / s_logoSize.y) : logoH;
        const ImVec2 logoMin(wp.x+18, wp.y+19);
        const ImVec2 logoMax(wp.x+18+logoW, wp.y+19+logoH);
        if (s_logoTexture) {
            dl->AddImage(s_logoTexture, logoMin, logoMax);
            DrawCrownShimmer(dl, logoMin, logoMax);
        } else {
            dl->AddRectFilled(logoMin, logoMax, IM_COL32(255, 208, 0, 255), 4.0f);
        }

        ImGui::SetCursorPos(ImVec2(52, 22));
        if (s_boldFont) ImGui::PushFont(s_boldFont);
        ImGui::SetWindowFontScale(1.10f);
        ImGui::TextUnformatted("ClaudeBawt");
        ImGui::SetWindowFontScale(1.0f);
        if (s_boldFont) ImGui::PopFont();
        dl->AddCircleFilled(ImVec2(wp.x+162, wp.y+27), 2.0f, kAccent, 8);
        const ImVec2 profileMin(wp.x+17, wp.y+winH-54);
        const ImVec2 profileMax(wp.x+47, wp.y+winH-24);
        dl->AddRectFilled(profileMin, profileMax, IM_COL32(31, 34, 42, 255), 6.0f);
        if (s_logoTexture) {
            dl->AddImageRounded(s_logoTexture, profileMin, profileMax,
                                ImVec2(0, 0), ImVec2(1, 1), IM_COL32_WHITE, 6.0f);
        } else {
            dl->AddText(ImVec2(profileMin.x+10, profileMin.y+6), kAccent, "C");
        }
        dl->AddRect(profileMin, profileMax, IM_COL32(83, 89, 108, 230),
                    6.0f, ImDrawFlags_None, 1.0f);
        dl->AddText(ImVec2(wp.x+57, wp.y+winH-52), kWhite, "exo/claude");
        dl->AddText(ImVec2(wp.x+57, wp.y+winH-34), kMuted, "Developer");
        ImGui::SetCursorPos(ImVec2(sidebarW+24, 20));
        if (ImGui::Button("Save Config", ImVec2(118, 27))) {
            Config_Save();
            notifications::Push("Config", "Settings saved",
                                IM_COL32(91, 207, 151, 255));
        }

        ImGui::SetCursorPos(ImVec2(14, 78));
        ImGui::BeginChild("##sidebar", ImVec2(sidebarW-28, -82), false,
                          ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        const char* labels[] = {"SocketFU","Auto Aim","Auto Dodge","Render","Spoofing","Keybinds","Debug / Misc"};
        for (int i=0;i<7;++i) {
            SidebarButton(labels[i], i, sidebarW-41);
            ImGui::Dummy(ImVec2(0,3));
        }
        ImGui::EndChild();

        ImGui::SetCursorPos(ImVec2(sidebarW+32, 91));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(20.0f, 18.0f));
        ImGui::BeginChild("##content", ImVec2(-50, -112), false);
        switch (s_activeTab) {
            case 0: SocketFuTab(); break;
            case 1: AimTab(); break;
            case 2: DodgeTab(); break;
            case 3: RenderTab(); break;
            case 4: SpooferTab(); break;
            case 5: KeybindsTab(); break;
            case 6: MiscTab(); break;
        }
        ImGui::EndChild();
        ImGui::PopStyleVar();
        const bool menuHovered = ImGui::IsWindowHovered(
            ImGuiHoveredFlags_RootAndChildWindows |
            ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
        DrawMenuCrosshair(wp, ImVec2(winW, winH), menuHovered);
        ImGui::End();
    }
}
