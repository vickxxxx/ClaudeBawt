#include "config.h"

#include <windows.h>
#include <shlobj.h>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>

Config g_cfg;

namespace {
    char s_path[MAX_PATH]{};

    void BuildPath() {
        if (s_path[0]) return;


        if (SUCCEEDED(SHGetFolderPathA(nullptr, CSIDL_PERSONAL, nullptr,
                                      SHGFP_TYPE_CURRENT, s_path))) {
            std::strncat(s_path, "\\rotmgv2.cf", MAX_PATH - std::strlen(s_path) - 1);
            return;
        }

        GetModuleFileNameA(nullptr, s_path, MAX_PATH);
        char* slash = std::strrchr(s_path, '\\');
        if (slash) slash[1] = '\0';
        std::strncat(s_path, "rotmgv2.cf", MAX_PATH - std::strlen(s_path) - 1);
    }

    int ReadInt(const char* key, int value) {
        return GetPrivateProfileIntA("Client", key, value, Config_Path());
    }

    float ReadFloat(const char* key, float value) {
        char fallback[32], text[64];
        std::snprintf(fallback, sizeof(fallback), "%.9g", value);
        GetPrivateProfileStringA("Client", key, fallback, text, sizeof(text), Config_Path());
        char* end = nullptr;
        const float parsed = std::strtof(text, &end);
        return (end && end != text) ? parsed : value;
    }

    void WriteInt(const char* key, int value) {
        char text[32];
        std::snprintf(text, sizeof(text), "%d", value);
        WritePrivateProfileStringA("Client", key, text, Config_Path());
    }

    void WriteFloat(const char* key, float value) {
        char text[32];
        std::snprintf(text, sizeof(text), "%.9g", value);
        WritePrivateProfileStringA("Client", key, text, Config_Path());
    }

    void ReadString(const char* key, char* value, size_t capacity) {
        if (!value || !capacity)
            return;
        char fallback[256]{};
        std::strncpy(fallback, value, sizeof(fallback) - 1);
        GetPrivateProfileStringA("Client", key, fallback, value,
                                 static_cast<DWORD>(capacity), Config_Path());
        value[capacity - 1] = '\0';
    }

    void WriteString(const char* key, const char* value) {
        WritePrivateProfileStringA("Client", key, value ? value : "",
                                   Config_Path());
    }

#define CFG_BOOL(name) g_cfg.name = ReadInt(#name, g_cfg.name ? 1 : 0) != 0
#define CFG_INT(name) g_cfg.name = ReadInt(#name, g_cfg.name)
#define CFG_FLOAT(name) g_cfg.name = ReadFloat(#name, g_cfg.name)
#define SAVE_BOOL(name) WriteInt(#name, g_cfg.name ? 1 : 0)
#define SAVE_INT(name) WriteInt(#name, g_cfg.name)
#define SAVE_FLOAT(name) WriteFloat(#name, g_cfg.name)

    void LoadColor(const char* key, float (&color)[4]) {
        char component[64];
        for (int i = 0; i < 4; ++i) {
            std::snprintf(component, sizeof(component), "%s%d", key, i);
            color[i] = std::clamp(ReadFloat(component, color[i]), 0.0f, 1.0f);
        }
    }

    void SaveColor(const char* key, const float (&color)[4]) {
        char component[64];
        for (int i = 0; i < 4; ++i) {
            std::snprintf(component, sizeof(component), "%s%d", key, i);
            WriteFloat(component, color[i]);
        }
    }
}

const char* Config_Path() {
    BuildPath();
    return s_path;
}

void Config_Load() {
    BuildPath();
    if (GetFileAttributesA(s_path) == INVALID_FILE_ATTRIBUTES) return;

    CFG_INT(menuToggleHotkey);
    CFG_INT(menuTheme);
    CFG_BOOL(menuBackground);
    CFG_BOOL(showBindsOverlay);
    CFG_BOOL(notificationCenter); CFG_FLOAT(notificationDuration);
    CFG_BOOL(titleBarActive);
    CFG_BOOL(sideBarBackground);
    LoadColor("colorMenuBackground", g_cfg.colorMenuBackground);
    LoadColor("colorTitleActive", g_cfg.colorTitleActive);
    LoadColor("colorSidebar", g_cfg.colorSidebar);
    LoadColor("colorBase", g_cfg.colorBase);
    LoadColor("colorHover", g_cfg.colorHover);
    LoadColor("colorActive", g_cfg.colorActive);
    LoadColor("colorCheck", g_cfg.colorCheck);
    LoadColor("colorText", g_cfg.colorText);

    CFG_BOOL(useSpeed1); CFG_BOOL(showCurrentSpeed); CFG_INT(speedHackHotkey);
    CFG_FLOAT(speedhackSpeed1); CFG_FLOAT(speedhackSpeed2); CFG_INT(speedToggleKey);
    CFG_BOOL(noFog); CFG_BOOL(socketFu); CFG_BOOL(showSocketFuTimer);
    CFG_FLOAT(socketFuSpeedFactor);
    g_cfg.socketFuHotkey.vk = ReadInt("socketFuHotkey", g_cfg.socketFuHotkey.vk);
    CFG_BOOL(socketFuUseSecondSpeed); CFG_BOOL(socketFuRestrictMovement);
    CFG_BOOL(socketFuNoClip); CFG_BOOL(autoNoClip); CFG_BOOL(noclipEnabled);
    g_cfg.noclipHotkey.vk = ReadInt("noclipHotkey", g_cfg.noclipHotkey.vk);
    CFG_BOOL(antiIdle);
    CFG_FLOAT(cameraZoomScale);
    CFG_BOOL(autoAim); CFG_BOOL(magnetAim); CFG_BOOL(magnetRangeExt);
    CFG_INT(targetingStyle); CFG_FLOAT(magnetAimRange);
    CFG_BOOL(projectileNoClip); CFG_BOOL(renderAimInfo);
    CFG_BOOL(renderMagnetRange); CFG_BOOL(renderNormalAimRange);
    g_cfg.aimbotHotkey.vk = ReadInt("aimbotHotkey", g_cfg.aimbotHotkey.vk);

    CFG_BOOL(autoDodge); CFG_BOOL(dodgeProjectiles); CFG_BOOL(dodgeHoldToToggle);
    g_cfg.dodgingHotkey.vk = ReadInt("dodgingHotkey", g_cfg.dodgingHotkey.vk);
    CFG_BOOL(dodgeInvisible); CFG_BOOL(butterWalk); CFG_FLOAT(dodgeHitboxSize);
    CFG_INT(dodgeMoveAwayMs); CFG_BOOL(dodgeAoeBombs); CFG_BOOL(dodgeAvoidUnits);
    CFG_FLOAT(dodgeUnitAvoidanceScale); CFG_FLOAT(dodgeKeepDistance);
    CFG_BOOL(oldDodgeLogic);
    CFG_BOOL(enablePoisBags); CFG_BOOL(playSoundForBags);
    CFG_BOOL(bagEgg); CFG_BOOL(bagBrown); CFG_BOOL(bagPink); CFG_BOOL(bagPurple);
    CFG_BOOL(bagCyan); CFG_BOOL(bagDarkBlue); CFG_BOOL(bagWhite); CFG_BOOL(bagGold);
    CFG_BOOL(bagOrange); CFG_BOOL(bagRed);
    CFG_BOOL(renderProjectiles); CFG_BOOL(renderAoeDebug);
    CFG_FLOAT(aoeDebugRadius); CFG_BOOL(aoeDebugCountdown);
    CFG_BOOL(projectileBreadcrumbs); CFG_FLOAT(projectileBreadcrumbLifetime);
    CFG_FLOAT(projectileBreadcrumbThickness); CFG_BOOL(renderTiles);
    CFG_BOOL(renderUnits); CFG_BOOL(renderHitbox); CFG_BOOL(renderGrid);
    CFG_BOOL(renderSafetyPath);
    CFG_BOOL(interactiveMapEnabled); CFG_BOOL(mapShowNames);
    CFG_BOOL(mapShowPortals); CFG_BOOL(mapShowRookie); CFG_BOOL(mapShowAdept);
    CFG_BOOL(mapShowVeteran); CFG_FLOAT(interactiveMapZoom);
    CFG_BOOL(menuSnow); CFG_FLOAT(menuSnowIntensity);
    CFG_BOOL(menuAnimatedBorder); CFG_BOOL(menuCrownShimmer);
    CFG_BOOL(menuCustomCrosshair); CFG_BOOL(menuCursorSnowTrail);

    CFG_BOOL(enableGlow); CFG_BOOL(rainbowGlow); CFG_INT(glowStyle);
    LoadColor("glowOutline", g_cfg.glowOutline);
    LoadColor("glowColor", g_cfg.glowColor);
    CFG_BOOL(showFpm); CFG_BOOL(spoofName);
    ReadString("spoofNameValue", g_cfg.spoofNameValue,
               sizeof(g_cfg.spoofNameValue));
    CFG_BOOL(spoofGuildName);
    ReadString("guildNameValue", g_cfg.guildNameValue,
               sizeof(g_cfg.guildNameValue));
    CFG_BOOL(spoofGuildRank); CFG_INT(guildRankValue);
    CFG_BOOL(skinChanger); CFG_INT(skinId);
    CFG_BOOL(dyeChanger); CFG_INT(dyeId);
    CFG_BOOL(accessoryDyeChanger); CFG_INT(accessoryDyeId);
    CFG_BOOL(stars); CFG_INT(starsValue);
    CFG_BOOL(fameValue); CFG_FLOAT(fameValueAmount);
    CFG_BOOL(accountFame); CFG_FLOAT(accountFameValue);

    g_cfg.menuToggleHotkey = std::clamp(g_cfg.menuToggleHotkey, 0, 0xFF);
    g_cfg.targetingStyle = std::clamp(g_cfg.targetingStyle, 0, 2);
    g_cfg.magnetAimRange = std::clamp(g_cfg.magnetAimRange, 1.0f, 2.25f);
    g_cfg.socketFuSpeedFactor = std::clamp(g_cfg.socketFuSpeedFactor, 1.0f, 3.0f);
    g_cfg.starsValue = std::clamp(g_cfg.starsValue, 0, 100);
    g_cfg.guildRankValue = std::clamp(g_cfg.guildRankValue, 0, 4);
    g_cfg.notificationDuration = std::clamp(g_cfg.notificationDuration, 1.5f, 10.0f);
    g_cfg.interactiveMapZoom = std::clamp(g_cfg.interactiveMapZoom, 1.0f, 4.0f);
    g_cfg.menuSnowIntensity = std::clamp(g_cfg.menuSnowIntensity, 0.15f, 1.5f);
    g_cfg.projectileBreadcrumbLifetime =
        std::clamp(g_cfg.projectileBreadcrumbLifetime, 0.15f, 2.0f);
    g_cfg.projectileBreadcrumbThickness =
        std::clamp(g_cfg.projectileBreadcrumbThickness, 0.5f, 4.0f);
    g_cfg.aoeDebugRadius = std::clamp(g_cfg.aoeDebugRadius, 0.25f, 4.0f);
    if (g_cfg.noclipHotkey.vk > 0xFF) g_cfg.noclipHotkey.vk = 0;
}

void Config_Save() {
    BuildPath();
    WritePrivateProfileStringA("Client", nullptr, nullptr, s_path);

    SAVE_INT(menuToggleHotkey); SAVE_INT(menuTheme);
    SAVE_BOOL(menuBackground); SAVE_BOOL(showBindsOverlay);
    SAVE_BOOL(notificationCenter); SAVE_FLOAT(notificationDuration);
    SAVE_BOOL(titleBarActive); SAVE_BOOL(sideBarBackground);
    SaveColor("colorMenuBackground", g_cfg.colorMenuBackground);
    SaveColor("colorTitleActive", g_cfg.colorTitleActive);
    SaveColor("colorSidebar", g_cfg.colorSidebar);
    SaveColor("colorBase", g_cfg.colorBase); SaveColor("colorHover", g_cfg.colorHover);
    SaveColor("colorActive", g_cfg.colorActive); SaveColor("colorCheck", g_cfg.colorCheck);
    SaveColor("colorText", g_cfg.colorText);

    SAVE_BOOL(useSpeed1); SAVE_BOOL(showCurrentSpeed); SAVE_INT(speedHackHotkey);
    SAVE_FLOAT(speedhackSpeed1); SAVE_FLOAT(speedhackSpeed2); SAVE_INT(speedToggleKey);
    SAVE_BOOL(noFog); SAVE_BOOL(socketFu); SAVE_BOOL(showSocketFuTimer);
    SAVE_FLOAT(socketFuSpeedFactor);
    WriteInt("socketFuHotkey", g_cfg.socketFuHotkey.vk);
    SAVE_BOOL(socketFuUseSecondSpeed); SAVE_BOOL(socketFuRestrictMovement);
    SAVE_BOOL(socketFuNoClip); SAVE_BOOL(autoNoClip); SAVE_BOOL(noclipEnabled);
    WriteInt("noclipHotkey", g_cfg.noclipHotkey.vk);
    SAVE_BOOL(antiIdle);
    SAVE_FLOAT(cameraZoomScale);
    SAVE_BOOL(autoAim); SAVE_BOOL(magnetAim); SAVE_BOOL(magnetRangeExt);
    SAVE_INT(targetingStyle); SAVE_FLOAT(magnetAimRange);
    SAVE_BOOL(projectileNoClip); SAVE_BOOL(renderAimInfo);
    SAVE_BOOL(renderMagnetRange); SAVE_BOOL(renderNormalAimRange);
    WriteInt("aimbotHotkey", g_cfg.aimbotHotkey.vk);

    SAVE_BOOL(autoDodge); SAVE_BOOL(dodgeProjectiles); SAVE_BOOL(dodgeHoldToToggle);
    WriteInt("dodgingHotkey", g_cfg.dodgingHotkey.vk);
    SAVE_BOOL(dodgeInvisible); SAVE_BOOL(butterWalk); SAVE_FLOAT(dodgeHitboxSize);
    SAVE_INT(dodgeMoveAwayMs); SAVE_BOOL(dodgeAoeBombs); SAVE_BOOL(dodgeAvoidUnits);
    SAVE_FLOAT(dodgeUnitAvoidanceScale); SAVE_FLOAT(dodgeKeepDistance);
    SAVE_BOOL(oldDodgeLogic);
    SAVE_BOOL(enablePoisBags); SAVE_BOOL(playSoundForBags);
    SAVE_BOOL(bagEgg); SAVE_BOOL(bagBrown); SAVE_BOOL(bagPink); SAVE_BOOL(bagPurple);
    SAVE_BOOL(bagCyan); SAVE_BOOL(bagDarkBlue); SAVE_BOOL(bagWhite); SAVE_BOOL(bagGold);
    SAVE_BOOL(bagOrange); SAVE_BOOL(bagRed);
    SAVE_BOOL(renderProjectiles); SAVE_BOOL(renderAoeDebug);
    SAVE_FLOAT(aoeDebugRadius); SAVE_BOOL(aoeDebugCountdown);
    SAVE_BOOL(projectileBreadcrumbs); SAVE_FLOAT(projectileBreadcrumbLifetime);
    SAVE_FLOAT(projectileBreadcrumbThickness); SAVE_BOOL(renderTiles);
    SAVE_BOOL(renderUnits); SAVE_BOOL(renderHitbox); SAVE_BOOL(renderGrid);
    SAVE_BOOL(renderSafetyPath);
    SAVE_BOOL(interactiveMapEnabled); SAVE_BOOL(mapShowNames);
    SAVE_BOOL(mapShowPortals); SAVE_BOOL(mapShowRookie); SAVE_BOOL(mapShowAdept);
    SAVE_BOOL(mapShowVeteran); SAVE_FLOAT(interactiveMapZoom);
    SAVE_BOOL(menuSnow); SAVE_FLOAT(menuSnowIntensity);
    SAVE_BOOL(menuAnimatedBorder); SAVE_BOOL(menuCrownShimmer);
    SAVE_BOOL(menuCustomCrosshair); SAVE_BOOL(menuCursorSnowTrail);

    SAVE_BOOL(enableGlow); SAVE_BOOL(rainbowGlow); SAVE_INT(glowStyle);
    SaveColor("glowOutline", g_cfg.glowOutline);
    SaveColor("glowColor", g_cfg.glowColor);
    SAVE_BOOL(showFpm); SAVE_BOOL(spoofName);
    WriteString("spoofNameValue", g_cfg.spoofNameValue);
    SAVE_BOOL(spoofGuildName);
    WriteString("guildNameValue", g_cfg.guildNameValue);
    SAVE_BOOL(spoofGuildRank); SAVE_INT(guildRankValue);
    SAVE_BOOL(skinChanger); SAVE_INT(skinId);
    SAVE_BOOL(dyeChanger); SAVE_INT(dyeId);
    SAVE_BOOL(accessoryDyeChanger); SAVE_INT(accessoryDyeId);
    SAVE_BOOL(stars); SAVE_INT(starsValue);
    SAVE_BOOL(fameValue); SAVE_FLOAT(fameValueAmount);
    SAVE_BOOL(accountFame); SAVE_FLOAT(accountFameValue);
    WritePrivateProfileStringA(nullptr, nullptr, nullptr, s_path);
}
