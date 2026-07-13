#include "features.h"
#include "il2cpp.h"
#include "log.h"
#include "MinHook.h"

namespace features {
    void InstallAll() {
        DBLOG("InstallAll: MH_Initialize");
        MH_Initialize();
        DBLOG("InstallAll: InstallRootCapture");
        game::InstallRootCapture();
        DBLOG("InstallAll: InstallGameTick");
        game::InstallGameTick();
        DBLOG("InstallAll: aim::Install");
        aim::Install();
        DBLOG("InstallAll: dodge::Install");
        dodge::Install();
        DBLOG("InstallAll: nexus::Install");
        nexus::Install();
        DBLOG("InstallAll: speedhack::Install");
        speedhack::Install();
        DBLOG("InstallAll: loot::Install");
        loot::Install();
        DBLOG("InstallAll: glow::Install");
        glow::Install();
        DBLOG("InstallAll: fame::Install");
        fame::Install();
        DBLOG("InstallAll: noclip::Install");
        noclip::Install();
        DBLOG("InstallAll: mods::Install");
        mods::Install();
        DBLOG("InstallAll: socketfu::Install");
        socketfu::Install();
        DBLOG("InstallAll: lagport::Install");
        lagport::Install();
        DBLOG("InstallAll: render_projectiles::Install");
        render_projectiles::Install();
        DBLOG("InstallAll: MH_EnableHook(ALL)");
        MH_STATUS st = MH_EnableHook(MH_ALL_HOOKS);
        DBLOG("InstallAll: MH_EnableHook returned %d", (int)st);
    }
    void Tick() {


        static bool firstTick = true;
        if (firstTick) DBLOG("Tick#1: aim");
        aim::Tick();
        if (firstTick) DBLOG("Tick#1: dodge");
        dodge::Tick();
        if (firstTick) DBLOG("Tick#1: nexus");
        nexus::Tick();
        if (firstTick) DBLOG("Tick#1: speedhack");
        speedhack::Tick();
        if (firstTick) DBLOG("Tick#1: loot");
        loot::Tick();
        if (firstTick) DBLOG("Tick#1: glow");
        glow::Tick();
        if (firstTick) DBLOG("Tick#1: fame");
        fame::Tick();
        if (firstTick) DBLOG("Tick#1: noclip");
        noclip::Tick();
        if (firstTick) DBLOG("Tick#1: mods");
        mods::Tick();
        if (firstTick) DBLOG("Tick#1: hud");
        hud::Tick();
        if (firstTick) DBLOG("Tick#1: socketfu");
        socketfu::Tick();
        if (firstTick) DBLOG("Tick#1: lagport");
        lagport::Tick();
        if (firstTick) DBLOG("Tick#1: render projectiles/aoe");
        render_projectiles::Tick();
        if (firstTick) DBLOG("Tick#1: render tiles");
        render_tiles::Tick();
        if (firstTick) DBLOG("Tick#1: render hitbox");
        render_hitbox::Tick();
        if (firstTick) DBLOG("Tick#1: render safety");
        render_safety::Tick();
        if (firstTick) DBLOG("Tick#1: render units/grid");
        render_units_grid::Tick();
        if (firstTick) { DBLOG("Tick#1: complete"); firstTick = false; }
    }


    void GameTick() {
        nexus::Poll();
        noclip::Poll();
        fame::Poll();
        teleport::Poll();
    }
}
