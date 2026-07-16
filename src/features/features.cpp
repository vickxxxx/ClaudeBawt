#include "features.h"
#include "il2cpp.h"
#include "log.h"
#include "MinHook.h"

namespace features {

void InstallAll() {
    DBLOG("InstallAll: streamlined menu");
    const MH_STATUS init = MH_Initialize();
    DBLOG("InstallAll: MH_Initialize=%d", (int)init);

    game::InstallRootCapture();
    game::InstallGameTick();

    aim::Install();
    dodge::Install();
    speedhack::Install();
    loot::Install();
    glow::Install();
    fame::Install();
    noclip::Install();
    socketfu::Install();
    render_projectiles::Install();

    const MH_STATUS enable = MH_EnableHook(MH_ALL_HOOKS);
    DBLOG("InstallAll: MH_EnableHook=%d", (int)enable);
}

void Tick() {
    static bool firstTick = true;
#define FEATURE_TICK(label, call) \
    do { if (firstTick) DBLOG("Tick#1: " label); call; } while (0)

    FEATURE_TICK("aim", aim::Tick());
    FEATURE_TICK("dodge", dodge::Tick());
    FEATURE_TICK("speedhack", speedhack::Tick());
    FEATURE_TICK("loot", loot::Tick());
    FEATURE_TICK("glow", glow::Tick());
    FEATURE_TICK("fame", fame::Tick());
    FEATURE_TICK("noclip", noclip::Tick());
    FEATURE_TICK("hud", hud::Tick());
    FEATURE_TICK("socketfu", socketfu::Tick());
    FEATURE_TICK("projectiles/aoe", render_projectiles::Tick());
    FEATURE_TICK("tiles", render_tiles::Tick());
    FEATURE_TICK("hitbox", render_hitbox::Tick());
    FEATURE_TICK("safety", render_safety::Tick());
    FEATURE_TICK("units/grid", render_units_grid::Tick());
    FEATURE_TICK("binds overlay", binds_overlay::Tick());
    FEATURE_TICK("notifications", notifications::Tick());
    FEATURE_TICK("interactive map", interactive_map::Tick());

#undef FEATURE_TICK
    if (firstTick) {
        DBLOG("Tick#1: complete");
        firstTick = false;
    }
}

void GameTick() {
    noclip::Poll();
    fame::Poll();
}

}
