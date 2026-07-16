#pragma once
#include <cstdint>

struct Keybind {
    int  vk = 0;
    bool listening = false;

    bool Pressed() const;
};

struct Config {
    enum TargetingStyle { TS_DISTANCE = 0, TS_CURSOR = 1, TS_HEALTH = 2 };


    int   menuToggleHotkey = 0x2D;
    int   menuTheme = 0;
    bool  menuBackground = true;
    bool  showBindsOverlay = true;
    bool  notificationCenter = true;
    float notificationDuration = 4.0f;
    bool  titleBarActive = true;
    bool  sideBarBackground = true;
    float colorMenuBackground[4] = { 0.20f, 0.05f, 0.20f, 0.75f };
    float colorTitleActive[4] = { 0.30f, 0.10f, 0.40f, 1.00f };
    float colorSidebar[4] = { 0.25f, 0.05f, 0.30f, 1.00f };
    float colorBase[4] = { 0.80f, 0.20f, 0.60f, 1.00f };
    float colorHover[4] = { 1.00f, 0.40f, 0.80f, 1.00f };
    float colorActive[4] = { 1.00f, 0.20f, 0.70f, 1.00f };
    float colorCheck[4] = { 0.00f, 1.00f, 0.00f, 1.00f };
    float colorText[4] = { 1.00f, 1.00f, 1.00f, 1.00f };


    bool  useSpeed1 = true;
    bool  showCurrentSpeed = false;
    int   speedHackHotkey = 0;
    float speedhackSpeed1 = 1.1f;
    float speedhackSpeed2 = 2.0f;
    int   speedToggleKey = 0;
    bool  noFog = false;
    bool  socketFu = false;
    Keybind socketFuHotkey;
    bool  showSocketFuTimer = false;
    bool  socketFuUseSecondSpeed = false;
    bool  socketFuRestrictMovement = false;
    bool  socketFuNoClip = false;
    bool  autoNoClip = false;
    bool  noclipEnabled = false;
    Keybind noclipHotkey;
    bool  antiIdle = false;
    float cameraZoomScale = 1.0f;
    bool  autoAim = true;
    bool  magnetAim = true;
    bool  magnetRangeExt = true;
    int   targetingStyle = TS_CURSOR;
    float magnetAimRange = 1.8f;
    bool  projectileNoClip = true;
    bool  renderAimInfo = true;
    bool  renderMagnetRange = true;
    bool  renderNormalAimRange = true;
    Keybind aimbotHotkey;


    bool  autoDodge = false;
    bool  dodgeProjectiles = true;
    bool  dodgeHoldToToggle = true;
    Keybind dodgingHotkey;
    bool  dodgeInvisible = false;
    bool  butterWalk = true;
    float dodgeHitboxSize = 0.456f;
    int   dodgeMoveAwayMs = 200;
    bool  dodgeAoeBombs = true;
    bool  dodgeAvoidUnits = true;
    float dodgeUnitAvoidanceScale = 1.0f;
    float dodgeKeepDistance = 0.0f;
    bool  oldDodgeLogic = false;
    bool  enablePoisBags = true;
    bool  playSoundForBags = true;
    bool  bagEgg = true;
    bool  bagBrown = true;
    bool  bagPink = true;
    bool  bagPurple = true;
    bool  bagCyan = true;
    bool  bagDarkBlue = true;
    bool  bagWhite = true;
    bool  bagGold = true;
    bool  bagOrange = true;
    bool  bagRed = true;
    bool  renderProjectiles = false;
    bool  renderAoeDebug = false;
    float aoeDebugRadius = 1.50f;
    bool  aoeDebugCountdown = true;
    bool  projectileBreadcrumbs = false;
    float projectileBreadcrumbLifetime = 0.80f;
    float projectileBreadcrumbThickness = 1.75f;
    bool  renderTiles = false;
    bool  renderUnits = false;
    bool  renderHitbox = false;
    bool  renderGrid = false;
    bool  renderSafetyPath = false;
    bool  interactiveMapEnabled = false;
    bool  mapShowNames = true;
    bool  mapShowPortals = true;
    bool  mapShowRookie = true;
    bool  mapShowAdept = true;
    bool  mapShowVeteran = true;
    float interactiveMapZoom = 1.0f;
    bool  menuSnow = true;
    float menuSnowIntensity = 0.75f;
    bool  menuAnimatedBorder = true;
    bool  menuCrownShimmer = true;
    bool  menuCustomCrosshair = true;
    bool  menuCursorSnowTrail = true;


    bool  enableGlow = false;
    bool  rainbowGlow = false;
    int   glowStyle = 0;

    float glowOutline[4] = { 1.0f, 1.0f, 0.5f, 0.8f };
    float glowColor[4]   = { 0.0f, 1.0f, 0.5f, 0.8f };
    bool  showFpm = false;
    bool  spoofName = false;
    char  spoofNameValue[25] = {};
    bool  spoofGuildName = false;
    char  guildNameValue[25] = {};
    bool  spoofGuildRank = false;
    int   guildRankValue = 0;
    bool  skinChanger = false;
    int   skinId = 0;
    bool  dyeChanger = false;
    int   dyeId = 0;
    bool  accessoryDyeChanger = false;
    int   accessoryDyeId = 0;
    bool  stars = false;
    int   starsValue = 0;
    bool  fameValue = false;
    float fameValueAmount = 0.0f;
    bool  accountFame = false;
    float accountFameValue = 0.0f;
};

extern Config g_cfg;

void Config_Load();
void Config_Save();
const char* Config_Path();
