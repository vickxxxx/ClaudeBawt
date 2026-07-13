


#include "features.h"
#include "config.h"
#include "il2cpp.h"
#include "util.h"
#include "log.h"

#include <windows.h>
#include <atomic>
#include <cstdint>
#include <cstring>
#include "MinHook.h"

namespace noclip {
namespace {


constexpr uint32_t kPatchEngage = 0x405EB60Fu;
constexpr uint32_t kPatchRestoreDefault = 0x415EB60Fu;
constexpr ULONGLONG kOffWallGraceMs = 250;

using CollisionFn = void(__fastcall*)(uintptr_t, uint32_t);
CollisionFn g_orig = nullptr;


constexpr uintptr_t kCanMoveRva = 0x1EAB6A0;
constexpr uintptr_t kIsSolidRva = 0x1EAA410;
using PredFn = int64_t(__fastcall*)(uintptr_t, uintptr_t, uintptr_t, uintptr_t);
PredFn g_origCanMove = nullptr;
PredFn g_origIsSolid = nullptr;

std::atomic<bool> g_gate{false};
bool      g_engaged = false;
uint32_t  g_originalDword = kPatchRestoreDefault;
bool      g_haveOriginal = false;
bool      g_onWall = false;
ULONGLONG g_offWallSince = 0;


int     g_dbgType = -1;
uint8_t g_dbgObj1698 = 0;
uint8_t g_dbgB27 = 0;
bool    g_dbgB25 = false;
int     g_dbgDmg = 0;
bool    g_dbgHadTile = false;
bool    g_manual = false;


bool TileIsWall(float x, float y) {
    if (x < 0.0f || y < 0.0f)
        return false;

    uintptr_t world = game::Root();
    if (!world)
        return false;
    uintptr_t ws = *reinterpret_cast<uintptr_t*>(world + 40);
    if (!ws)
        return false;

    uintptr_t tiles =
        *reinterpret_cast<uintptr_t*>(ws + ga::off::WORLD_TILE_GRID);
    if (!tiles)
        return false;

    const int height =
        *reinterpret_cast<int*>(ws + ga::off::WORLD_MAP_HEIGHT);
    const int width =
        *reinterpret_cast<int*>(ws + ga::off::WORLD_MAP_WIDTH);
    const int ix = static_cast<int>(x);
    const int iy = static_cast<int>(y);
    if (iy >= height || ix >= width)
        return false;


    uintptr_t tile = *reinterpret_cast<uintptr_t*>(
        static_cast<uintptr_t>(8 * (ix + height * iy)) + tiles + 32);
    if (!tile)
        return true;

    g_dbgHadTile = true;
    const uintptr_t obj  = *reinterpret_cast<uintptr_t*>(tile + 72);
    const int       type = *reinterpret_cast<int*>(tile + 68);

    bool b25 = false;
    int  dmg = 0;
    uint8_t b27 = 0;
    uint8_t b34 = 0;
    uint8_t obj1698 = 0;
    bool haveObj = false;

    if (obj) {
        uintptr_t inner = *reinterpret_cast<uintptr_t*>(obj + 24);
        if (inner) {
            obj1698 = *reinterpret_cast<uint8_t*>(inner + 1698);
            b34 = *reinterpret_cast<uint8_t*>(inner + 1764);
            haveObj = true;
        }
    }

    const uintptr_t props = *reinterpret_cast<uintptr_t*>(tile + 80);
    if (props) {
        b27 = *reinterpret_cast<uint8_t*>(props + 260);
        dmg = *reinterpret_cast<int*>(props + 268);
    }


    if (dmg > 0) {
        if (type == 37 || (haveObj && !b34))
            b25 = true;
    }

    g_dbgType = type; g_dbgObj1698 = obj1698; g_dbgB27 = b27;
    g_dbgB25 = b25; g_dbgDmg = dmg;


    return (b27 != 0) || b25;
}

void Engage() {
    if (g_engaged)
        return;
    void* site = ga::Rva(ga::rva::MOVEMENT_FLAG_PATCH);
    if (site) {
        if (!g_haveOriginal) {
            std::memcpy(&g_originalDword, site, sizeof(g_originalDword));
            g_haveOriginal = true;
        }
        uint32_t v = kPatchEngage;
        util::Patch(site, &v, sizeof(v));
    }
    g_gate.store(true, std::memory_order_release);
    g_engaged = true;
    DBLOG("noclip: ENGAGE (patch %p)", site);
}

void Disengage() {
    if (!g_engaged)
        return;
    void* site = ga::Rva(ga::rva::MOVEMENT_FLAG_PATCH);
    if (site) {
        uint32_t v = g_haveOriginal ? g_originalDword : kPatchRestoreDefault;
        util::Patch(site, &v, sizeof(v));
    }
    g_gate.store(false, std::memory_order_release);
    g_engaged = false;
    g_offWallSince = 0;
    DBLOG("noclip: DISENGAGE");
}


void __fastcall hkCollision(uintptr_t a1, uint32_t a2) {
    static bool once = false;
    if (!once) { once = true; DBLOG("hkCollision: first call"); }


    if (!g_gate.load(std::memory_order_acquire)) {
        if (g_orig)
            g_orig(a1, a2);
    }
}


int64_t __fastcall hkCanMove(uintptr_t a1, uintptr_t a2, uintptr_t a3, uintptr_t a4) {
    static bool once = false;
    if (!once) { once = true; DBLOG("hkCanMove: first call"); }
    const int64_t r = g_origCanMove ? g_origCanMove(a1, a2, a3, a4) : 0;
    if (!(static_cast<uint8_t>(r)) && g_cfg.autoNoClip)
        g_onWall = true;
    if (g_gate.load(std::memory_order_acquire))
        return 1;
    return r;
}


int64_t __fastcall hkIsSolid(uintptr_t a1, uintptr_t a2, uintptr_t a3, uintptr_t a4) {
    const int64_t r = g_origIsSolid ? g_origIsSolid(a1, a2, a3, a4) : 0;
    if (g_gate.load(std::memory_order_acquire))
        return 0;
    return r;
}

}


void NoteMoveTarget(float x, float y) {
    if (!g_cfg.autoNoClip)
        return;
    const bool wall = TileIsWall(x, y);
    if (wall)
        g_onWall = true;

    static ULONGLONG lastLog = 0;
    ULONGLONG now = GetTickCount64();
    if (now - lastLog > 1000) {
        lastLog = now;
        DBLOG("noclip target=(%.2f,%.2f) wall=%d gate=%d eng=%d | tile=%d type=%d obj1698=%d b27=%d b25=%d dmg=%d",
              x, y, (int)wall, (int)g_gate.load(std::memory_order_acquire),
              (int)g_engaged, (int)g_dbgHadTile, g_dbgType, (int)g_dbgObj1698,
              (int)g_dbgB27, (int)g_dbgB25, g_dbgDmg);
    }
}

bool GateActive() {
    return g_gate.load(std::memory_order_acquire);
}


void SetManual(bool on) {
    if (on == g_manual)
        return;
    g_manual = on;
    if (on)
        Engage();
    else
        Disengage();
}

void Install() {
    void* target = ga::Rva(ga::rva::COLLISION_RESOLVE);
    DBLOG("noclip::Install: collision target=%p (GA+0x%llX)", target,
          (unsigned long long)ga::rva::COLLISION_RESOLVE);
    if (!target)
        return;
    const MH_STATUS st = MH_CreateHook(target, reinterpret_cast<void*>(&hkCollision),
                                       reinterpret_cast<void**>(&g_orig));
    DBLOG("noclip::Install: MH_CreateHook=%d orig=%p", (int)st, (void*)g_orig);


    void* canMove = ga::Rva(kCanMoveRva);
    if (canMove) {
        const MH_STATUS s = MH_CreateHook(canMove, reinterpret_cast<void*>(&hkCanMove),
                                          reinterpret_cast<void**>(&g_origCanMove));
        DBLOG("noclip::Install: can-move target=%p (GA+0x%llX) MH=%d",
              canMove, (unsigned long long)kCanMoveRva, (int)s);
    }
    void* isSolid = ga::Rva(kIsSolidRva);
    if (isSolid) {
        const MH_STATUS s = MH_CreateHook(isSolid, reinterpret_cast<void*>(&hkIsSolid),
                                          reinterpret_cast<void**>(&g_origIsSolid));
        DBLOG("noclip::Install: is-solid target=%p (GA+0x%llX) MH=%d",
              isSolid, (unsigned long long)kIsSolidRva, (int)s);
    }
}

void Tick() {

}


void Poll() {
    if (g_manual) {
        g_onWall = false;
        return;
    }
    if (!g_cfg.autoNoClip) {
        if (g_engaged)
            Disengage();
        g_onWall = false;
        return;
    }

    const ULONGLONG now = GetTickCount64();
    if (g_onWall) {
        if (!g_engaged)
            Engage();
        g_offWallSince = 0;
    } else if (g_engaged) {
        if (g_offWallSince == 0)
            g_offWallSince = now + kOffWallGraceMs;
        else if (now >= g_offWallSince)
            Disengage();
    }
    g_onWall = false;
}

}
