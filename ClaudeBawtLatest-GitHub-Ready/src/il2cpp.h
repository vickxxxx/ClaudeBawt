


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
        constexpr uintptr_t SHOT_UPDATE          = 0x14B19E0; // 2026-08-17 FKALGHJIADI::CDJLLHJOCNM(float)
        constexpr uintptr_t MOVE_TO              = 0x14B77E0; // 2026-08-17 FKALGHJIADI::FOFMOMDBGGI(float, Vector2)
        constexpr uintptr_t MOVEMENT_UPDATE      = 0x14B4E90; // 2026-08-17 FKALGHJIADI::DKLEJEABOOO(...)
        constexpr uintptr_t MOVE_SPEED           = 0x14B8810; // 2026-08-17 FKALGHJIADI::GGNFAIKKIEO()
        constexpr uintptr_t SOCKETFU_MOVE_SPEED  = 0x14B79E0; // 2026-08-17 FKALGHJIADI::GAFGPNKFMOJ()
        constexpr uintptr_t SOCKET_SEND          = 0x1D0C410; // 2026-08-17 SocketManager::SendMessage
        constexpr uintptr_t SOCKET_QUEUE_PUMP    = 0x7962B0;  // 2026-08-17 JOEMEFDPIIP::AIPAABAADIN()
        constexpr uintptr_t MOVEMENT_FLAG_PATCH  = 0x1D042FC; // 2026-08-17 exact 0F B6 5E 41 patch site
        constexpr uintptr_t WORLD_CONTEXT_UPDATE = 0;         // unused; no verified August hook
        constexpr uintptr_t APPLICATION_UPDATE   = 0xA04BC0;  // 2026-08-17 ApplicationManager::Update
        constexpr uintptr_t CAMERA_MATRIX        = 0x9662D0;  // 2026-08-17 CameraManager::GetCameraMatrix
        constexpr uintptr_t PROJECTILE_POSITION  = 0xB0E0E0;  // 2026-08-17 HBEAKBIHANL::GIBLKPDHLBG(...)
        constexpr uintptr_t AIM_POINT_PATCH      = 0x14BFB51; // 2026-08-17 exact 27-byte xmm9/xmm10 site
        constexpr uintptr_t GAME_TICK            = 0x144C280; // 2026-08-17 UnityThread::Update()
        constexpr uintptr_t Fixed_Update         = 0;         // unused; no verified August hook
        constexpr uintptr_t COLLISION_RESOLVE    = 0;         // ambiguous August native callback; intentionally disabled
    }


    namespace off {
        constexpr int OBJECT_X              = 0x3C;
        constexpr int OBJECT_Y              = 0x40;
        constexpr int OBJECT_MAX_HEALTH     = 0x208;
        constexpr int OBJECT_HEALTH         = 0x20C;
        constexpr int OBJECT_STATUS         = 0x18;
        constexpr int OBJECT_INVULNERABLE   = 0x215;
        // 2026-08-17 LKHPPBEGNOM::COHCKAPOLCA (System.Int32[]).
        // 0x248 is now a double and interpreting it as an object pointer crashes.
        constexpr int OBJECT_EFFECTS        = 0x250;
        constexpr int OBJECT_TYPE           = 0x30;
        constexpr int STATUS_CAN_TARGET     = 0x6E1; // ObjectProperties::isEnemy (2026-08-17)
        constexpr int STATUS_FULL_OCCUPY    = 0x6E9; // ObjectProperties::fullOccupy
        constexpr int STATUS_GROUND_PROTECT = 0x6F4; // ObjectProperties::protectFromGroundDamage


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
