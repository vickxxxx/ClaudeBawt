


#pragma once
#include <cstdint>

namespace ga {

    uintptr_t Base();
    bool Init();
    bool Ready();
    void Reset();

    inline void* Rva(uintptr_t rva) {
        uintptr_t b = Base();
        return b ? reinterpret_cast<void*>(b + rva) : nullptr;
    }


    namespace rva {
        constexpr uintptr_t SHOT_UPDATE          = 0x35CA30;
        constexpr uintptr_t MOVE_TO              = 0x366440;
        constexpr uintptr_t MOVEMENT_UPDATE      = 0x35E160;
        constexpr uintptr_t MOVE_SPEED           = 0x362770;
        constexpr uintptr_t MOVEMENT_FLAG_PATCH  = 0xC2180C;
        constexpr uintptr_t WORLD_CONTEXT_UPDATE = 0xEC82E0;
        constexpr uintptr_t AIM_POINT_PATCH      = 0x36A611;
        constexpr uintptr_t GAME_TICK            = 0x10E7430;
        constexpr uintptr_t COLLISION_RESOLVE    = 0x365130;
    }


    namespace off {
        constexpr int OBJECT_X              = 0x3C;
        constexpr int OBJECT_Y              = 0x40;
        constexpr int OBJECT_HEALTH         = 0x20C;
        constexpr int OBJECT_STATUS         = 0x18;
        constexpr int OBJECT_INVULNERABLE   = 0x215;
        constexpr int OBJECT_EFFECTS        = 0x248;
        constexpr int OBJECT_TYPE           = 0x30;
        constexpr int STATUS_CAN_TARGET     = 1745;


        constexpr int WORLD_GAME_TIME          = 0x90;
        constexpr int WORLD_TILE_GRID          = 0x68;
        constexpr int WORLD_MAP_WIDTH          = 0x10C;
        constexpr int WORLD_MAP_HEIGHT         = 0x110;
        constexpr int WORLD_OBJECT_MANAGER     = 0xC0;
        constexpr int WORLD_PROJECTILE_MANAGER = 0xC8;
    }
}

namespace game {


    uintptr_t Root();
    void      CaptureRoot(uintptr_t world);
    void      ResetRuntimeState();

    uintptr_t Player();


    void MoveTo(uintptr_t player, float x, float y);


    void InstallRootCapture();
    bool RootCaptureInstalled();


    void InstallGameTick();
    bool GameTickInstalled();
}
