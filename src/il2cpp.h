


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
        constexpr uintptr_t SHOT_UPDATE          = 0x14F62B0; // 2026-07-17 native signature: CDJLLHJOCNM(float)
        constexpr uintptr_t MOVE_TO              = 0x14FC890; // 2026-07-17 native signature: FOFMOMDBGGI(float, Vector2)
        constexpr uintptr_t MOVEMENT_UPDATE      = 0x14F9720; // 2026-07-17 native body match: DKLEJEABOOO(...)
        constexpr uintptr_t MOVE_SPEED           = 0x14FE090; // 2026-07-17 native signature: GGNFAIKKIEO()
        constexpr uintptr_t SOCKETFU_MOVE_SPEED  = 0x14FCA00; // 2026-07-17 native signature: GAFGPNKFMOJ()
        constexpr uintptr_t SOCKET_SEND          = 0x1D8CF00; // 2026-07-17 native body + preceding-neighbor match
        constexpr uintptr_t SOCKET_QUEUE_PUMP    = 0x198A330; // 2026-07-17 native body + JOEMEFDPIIP neighbor-cluster match
        constexpr uintptr_t MOVEMENT_FLAG_PATCH  = 0x1D84C1C; // 2026-07-17 unique native signature: movzx ebx,[rsi+41h]
        constexpr uintptr_t WORLD_CONTEXT_UPDATE = 0x1AE8180; //0xEC82E0;OLD    0x1AE8180 50/50 might be 0x1AE40E0
        constexpr uintptr_t APPLICATION_UPDATE   = 0x79F4E0; // 2026-07-17 native body + preceding-neighbor match
        constexpr uintptr_t CAMERA_MATRIX        = 0x170D9A0; // 2026-07-17 native signature: CameraManager::GetCameraMatrix
        constexpr uintptr_t PROJECTILE_POSITION  = 0x197F020; // 2026-07-17 native signature: Projectile::GIBLKPDHLBG(...)
        constexpr uintptr_t AIM_POINT_PATCH      = 0x1506C61; // 2026-07-17 unique 27-byte xmm9/xmm10 world-point site
        constexpr uintptr_t GAME_TICK            = 0x1F31660; // 2026-07-17 native body + UnityThread .cctor layout
        constexpr uintptr_t Fixed_Update         = 0x6D3D90; //Updated
        constexpr uintptr_t COLLISION_RESOLVE    = 0x7D3880; // old 0xA47A90, full native-body match
    }


    namespace off {
        constexpr int OBJECT_X              = 0x3C;
        constexpr int OBJECT_Y              = 0x40;
        constexpr int OBJECT_MAX_HEALTH     = 0x208;
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
    void      CapturePlayer(uintptr_t player);

    bool CameraMatrix(float out[4][4]);


    void MoveTo(uintptr_t player, float x, float y);


    void InstallRootCapture();
    bool RootCaptureInstalled();


    void InstallGameTick();
    bool GameTickInstalled();
}
