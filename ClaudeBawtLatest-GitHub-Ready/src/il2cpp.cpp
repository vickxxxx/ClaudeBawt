#include "il2cpp.h"
#include "log.h"
#include <windows.h>
#include <atomic>
#include <cmath>
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

namespace game {
    static std::atomic<uintptr_t> g_root{0};
    static std::atomic<uintptr_t> g_player{0};
    static std::atomic<uintptr_t> g_application{0};
    static std::atomic<bool> g_rootCaptureInstalled{false};
    static std::atomic<bool> g_gameTickInstalled{false};

    uintptr_t Root() { return g_root.load(std::memory_order_acquire); }
    void CaptureRoot(uintptr_t world) {

        g_root.store(world, std::memory_order_release);
    }
    void ResetRuntimeState() {
        g_application.store(0, std::memory_order_release);
        CaptureRoot(0);
        CapturePlayer(0);
    }


    uintptr_t Player() {
        return g_player.load(std::memory_order_acquire);
    }

    void CapturePlayer(uintptr_t player) {
        g_player.store(player, std::memory_order_release);
    }

    bool CameraMatrix(float out[4][4]) {
        if (!out)
            return false;
        const uintptr_t application =
            g_application.load(std::memory_order_acquire);
        if (application <= 0xFFFF)
            return false;
#if defined(_MSC_VER)
        __try {
#endif
            const uintptr_t manager =
                *reinterpret_cast<const uintptr_t*>(application + 0x80);
            if (manager <= 0xFFFF)
                return false;
            using Fn = void*(__fastcall*)(void*, uintptr_t, uintptr_t);
            const auto fn = reinterpret_cast<Fn>(ga::Rva(ga::rva::CAMERA_MATRIX));
            if (!fn)
                return false;
            fn(out, manager, 0);
            return true;
#if defined(_MSC_VER)
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            return false;
        }
#endif
    }

    void MoveTo(uintptr_t player, float x, float y) {
        if (!player) return;
        struct Vec2 { float x, y; };
        // FKALGHJIADI::FOFMOMDBGGI(float, Vector2) consumes movement input,
        // not an absolute destination. Resolve it through the live runtime so
        // metadata-v31 dump misassociations cannot select a dead code clone.
        using Fn = intptr_t(__fastcall*)(uintptr_t, float, Vec2, uintptr_t);
        static Fn fn = nullptr;
        static bool attempted = false;
        if (!attempted) {
            attempted = true;
            HMODULE module = GetModuleHandleA("GameAssembly.dll");
            using DomainGetFn = void* (*)();
            using AssemblyOpenFn = void* (*)(void*, const char*);
            using AssemblyImageFn = void* (*)(void*);
            using ClassFromNameFn = void* (*)(void*, const char*, const char*);
            using ClassMethodFn = const void* (*)(void*, const char*, int);
            using MethodPointerFn = void* (*)(const void*);
            const auto domainGet = reinterpret_cast<DomainGetFn>(
                GetProcAddress(module, "il2cpp_domain_get"));
            const auto assemblyOpen = reinterpret_cast<AssemblyOpenFn>(
                GetProcAddress(module, "il2cpp_domain_assembly_open"));
            const auto assemblyImage = reinterpret_cast<AssemblyImageFn>(
                GetProcAddress(module, "il2cpp_assembly_get_image"));
            const auto classFromName = reinterpret_cast<ClassFromNameFn>(
                GetProcAddress(module, "il2cpp_class_from_name"));
            const auto classMethod = reinterpret_cast<ClassMethodFn>(
                GetProcAddress(module, "il2cpp_class_get_method_from_name"));
            const auto methodPointer = reinterpret_cast<MethodPointerFn>(
                GetProcAddress(module, "il2cpp_method_get_pointer"));
            if (domainGet && assemblyOpen && assemblyImage && classFromName &&
                classMethod) {
                void* domain = domainGet();
                void* assembly = domain ? assemblyOpen(domain, "Assembly-CSharp") : nullptr;
                void* image = assembly ? assemblyImage(assembly) : nullptr;
                void* klass = image ? classFromName(image, "", "FKALGHJIADI") : nullptr;
                const void* method = klass
                    ? classMethod(klass, "FOFMOMDBGGI", 2)
                    : nullptr;
                void* pointer = method
                    ? (methodPointer ? methodPointer(method)
                                     : *reinterpret_cast<void* const*>(method))
                    : nullptr;
                fn = reinterpret_cast<Fn>(pointer);
                DBLOG("MoveTo: runtime movement method=%p (GA+0x%llX)", pointer,
                      pointer ? static_cast<unsigned long long>(
                          reinterpret_cast<uintptr_t>(pointer) - ga::Base()) : 0ULL);
            }
        }
        if (!fn) return;
        const float px = *reinterpret_cast<const float*>(player + 0x3C);
        const float py = *reinterpret_cast<const float*>(player + 0x40);
        float dx = x - px;
        float dy = -(y - py);

        // FOFMOMDBGGI receives the same camera-relative axes produced by the
        // game's WASD input. Convert both world points to NDC and steer along
        // their on-screen delta so cursor following remains correct under
        // camera rotation and zoom.
        float matrix[4][4]{};
        if (CameraMatrix(matrix)) {
            const auto projectNdc = [&matrix](float worldX, float worldY,
                                               float& outX, float& outY) {
                const float vx = worldX;
                const float vy = -worldY;
                const float w = matrix[0][3] * vx + matrix[1][3] * vy +
                                matrix[3][3];
                if (!std::isfinite(w) || std::fabs(w) < 0.0001f)
                    return false;
                outX = (matrix[0][0] * vx + matrix[1][0] * vy +
                        matrix[3][0]) / w;
                outY = (matrix[0][1] * vx + matrix[1][1] * vy +
                        matrix[3][1]) / w;
                return std::isfinite(outX) && std::isfinite(outY);
            };
            float playerX = 0.0f, playerY = 0.0f;
            float targetX = 0.0f, targetY = 0.0f;
            if (projectNdc(px, py, playerX, playerY) &&
                projectNdc(x, y, targetX, targetY)) {
                const float screenX = targetX - playerX;
                const float screenY = targetY - playerY;
                // The native movement axes are isometric: +X travels
                // top-left and +Y travels top-right. Rotate the desired
                // on-screen direction into that diagonal basis.
                dx = screenY - screenX;
                dy = screenY + screenX;
            }
        }
        const float length = std::sqrt(dx * dx + dy * dy);
        if (std::isfinite(length) && length > 0.001f) {
            dx /= length;
            dy /= length;
        } else {
            dx = 0.0f;
            dy = 0.0f;
        }
        fn(player, 0.0f, Vec2{dx, dy}, 0);
    }


    using WorldFn = void(__fastcall*)(uintptr_t, uintptr_t, uintptr_t);
    static WorldFn oWorldFn = nullptr;
    static void __fastcall hkWorldFn(uintptr_t self, uintptr_t packet,
                                     uintptr_t methodInfo) {
        static bool once = false;
        const bool first = !once;
        if (first) {
            once = true;
            DBLOG("hkWorldFn: first call, self=%p packet=%p before orig=%p",
                  (void*)self, (void*)packet, (void*)oWorldFn);
        }
        CaptureRoot(self);
        if (oWorldFn)
            oWorldFn(self, packet, methodInfo);
        if (first) DBLOG("hkWorldFn: first call returned from orig");
    }

    using ApplicationUpdateFn = void(__fastcall*)(uintptr_t, uintptr_t);
    static ApplicationUpdateFn oApplicationUpdate = nullptr;

    static void __fastcall hkApplicationUpdate(uintptr_t self,
                                                uintptr_t methodInfo) {
        if (self > 0xFFFF) {
            g_application.store(self, std::memory_order_release);
            const uintptr_t world = *reinterpret_cast<uintptr_t*>(self + 0xC0);
            CaptureRoot(world > 0xFFFF ? world : 0);
            if (world > 0xFFFF) {
                const uintptr_t player =
                    *reinterpret_cast<uintptr_t*>(world + 0x48);
                CapturePlayer(player > 0xFFFF ? player : 0);
            }
        }
        if (oApplicationUpdate)
            oApplicationUpdate(self, methodInfo);
    }

    void InstallRootCapture() {
        CaptureRoot(0);
        void* target = ga::Rva(ga::rva::APPLICATION_UPDATE);
        DBLOG("InstallRootCapture: ApplicationManager::Update target=%p (GA+0x%llX)",
              target, (unsigned long long)ga::rva::APPLICATION_UPDATE);
        if (!target)
            return;
        const MH_STATUS status = MH_CreateHook(
            target, reinterpret_cast<void*>(&hkApplicationUpdate),
            reinterpret_cast<void**>(&oApplicationUpdate));
        DBLOG("InstallRootCapture: MH_CreateHook=%d orig=%p", (int)status,
              (void*)oApplicationUpdate);
        if (status == MH_OK)
            g_rootCaptureInstalled.store(true, std::memory_order_release);
    }

    bool RootCaptureInstalled() {
        return g_rootCaptureInstalled.load(std::memory_order_acquire);
    }


    using GameTickFn = void(__fastcall*)(uintptr_t, uintptr_t);
    static GameTickFn oGameTick = nullptr;
    static void __fastcall hkGameTick(uintptr_t self, uintptr_t methodInfo) {
        static bool once = false;
        if (!once) { once = true; DBLOG("hkGameTick: first call self=%p", (void*)self); }
        features::GameTick();
        // The original NoClip holds the surrounding world simulation while
        // its movement predicates are overridden.
        if (noclip::GateActive())
            return;
        if (oGameTick)
            oGameTick(self, methodInfo);
    }

    void InstallGameTick() {
        if (g_gameTickInstalled.load(std::memory_order_acquire))
            return;

        if (!ga::rva::GAME_TICK) {
            DBLOG("InstallGameTick: disabled (no verified RVA for this build)");
            return;
        }

        void* target = ga::Rva(ga::rva::GAME_TICK);
        DBLOG("InstallGameTick: target=%p (GA+0x%llX)", target,
              (unsigned long long)ga::rva::GAME_TICK);
        if (!target)
            return;

        const MH_STATUS status = MH_CreateHook(target, (void*)&hkGameTick,
                                               (void**)&oGameTick);
        DBLOG("InstallGameTick: MH_CreateHook=%d orig=%p", (int)status, (void*)oGameTick);
        if (status == MH_OK)
            g_gameTickInstalled.store(true, std::memory_order_release);
    }

    bool GameTickInstalled() {
        return g_gameTickInstalled.load(std::memory_order_acquire);
    }
}
