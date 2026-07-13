#include "il2cpp.h"
#include "log.h"
#include <windows.h>
#include <atomic>
#include "MinHook.h"

namespace ga {
    static std::atomic<uintptr_t> g_base{0};

    uintptr_t Base() {
        uintptr_t base = g_base.load(std::memory_order_acquire);
        if (!base) {
            base = reinterpret_cast<uintptr_t>(GetModuleHandleA("GameAssembly.dll"));
            if (base)
                g_base.store(base, std::memory_order_release);
        }
        return base;
    }

    bool Init() { return Base() != 0; }
    bool Ready() { return g_base.load(std::memory_order_acquire) != 0; }
    void Reset() { g_base.store(0, std::memory_order_release); }
}


namespace features { void GameTick(); }
namespace noclip   { bool GateActive(); }
namespace lagport  { bool FreezeActive(); }

namespace game {
    static std::atomic<uintptr_t> g_root{0};
    static std::atomic<bool> g_rootCaptureInstalled{false};
    static std::atomic<bool> g_gameTickInstalled{false};

    uintptr_t Root() { return g_root.load(std::memory_order_acquire); }
    void CaptureRoot(uintptr_t world) {

        g_root.store(world, std::memory_order_release);
    }
    void ResetRuntimeState() { CaptureRoot(0); }


    uintptr_t Player() {
        const uintptr_t root = Root();
        if (!root) return 0;
        uintptr_t a = *reinterpret_cast<uintptr_t*>(root + 0x28);
        if (!a) return 0;
        return *reinterpret_cast<uintptr_t*>(a + 0x48);
    }

    void MoveTo(uintptr_t player, float x, float y) {
        if (!player) return;
        struct Vec2 { float x, y; };
        using Fn = intptr_t(__fastcall*)(uintptr_t, const Vec2*);
        auto fn = reinterpret_cast<Fn>(ga::Rva(ga::rva::MOVE_TO));
        if (!fn) return;
        const Vec2 position{x, y};
        fn(player, &position);
    }


    static uintptr_t (__fastcall* oWorldFn)(uintptr_t) = nullptr;
    static uintptr_t __fastcall hkWorldFn(uintptr_t a1) {
        static bool once = false;
        const bool first = !once;
        if (first) { once = true; DBLOG("hkWorldFn: first call, root=%p, before orig=%p", (void*)a1, (void*)oWorldFn); }
        CaptureRoot(a1);
        uintptr_t r = oWorldFn ? oWorldFn(a1) : 0;
        if (first) DBLOG("hkWorldFn: first call returned from orig");
        return r;
    }

    void InstallRootCapture() {
        if (g_rootCaptureInstalled.load(std::memory_order_acquire))
            return;

        void* target = ga::Rva(ga::rva::WORLD_CONTEXT_UPDATE);
        DBLOG("InstallRootCapture: target=%p (GA+0x%llX)", target,
              (unsigned long long)ga::rva::WORLD_CONTEXT_UPDATE);
        if (!target)
            return;

        const MH_STATUS status = MH_CreateHook(target, (void*)&hkWorldFn, (void**)&oWorldFn);
        DBLOG("InstallRootCapture: MH_CreateHook=%d orig=%p", (int)status, (void*)oWorldFn);
        if (status == MH_OK)
            g_rootCaptureInstalled.store(true, std::memory_order_release);
    }

    bool RootCaptureInstalled() {
        return g_rootCaptureInstalled.load(std::memory_order_acquire);
    }


    static uintptr_t (__fastcall* oGameTick)(uintptr_t) = nullptr;
    static uintptr_t __fastcall hkGameTick(uintptr_t a1) {
        static bool once = false;
        if (!once) { once = true; DBLOG("hkGameTick: first call a1=%p", (void*)a1); }
        features::GameTick();
        if (noclip::GateActive() || lagport::FreezeActive())
            return 0;
        return oGameTick ? oGameTick(a1) : 0;
    }

    void InstallGameTick() {
        if (g_gameTickInstalled.load(std::memory_order_acquire))
            return;

        void* target = ga::Rva(ga::rva::GAME_TICK);
        DBLOG("InstallGameTick: target=%p (GA+0x%llX)", target,
              (unsigned long long)ga::rva::GAME_TICK);
        if (!target)
            return;

        const MH_STATUS status = MH_CreateHook(target, (void*)&hkGameTick, (void**)&oGameTick);
        DBLOG("InstallGameTick: MH_CreateHook=%d orig=%p", (int)status, (void*)oGameTick);
        if (status == MH_OK)
            g_gameTickInstalled.store(true, std::memory_order_release);
    }

    bool GameTickInstalled() {
        return g_gameTickInstalled.load(std::memory_order_acquire);
    }
}
