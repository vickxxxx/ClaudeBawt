


#include "features.h"
#include "config.h"
#include "il2cpp.h"
#include "log.h"

#include <windows.h>
#include <intrin.h>
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <deque>
#include <mutex>
#include <vector>
#include "imgui.h"
#include "MinHook.h"

namespace socketfu {
namespace {

using SpeedFn = float(__fastcall*)(uintptr_t, uintptr_t);
SpeedFn g_orig = nullptr;

using SendFn = void(__fastcall*)(uintptr_t, uintptr_t, uintptr_t);
SendFn g_origSend = nullptr;
using GcHandleNewFn = uint32_t(__cdecl*)(void*, bool);
using GcHandleTargetFn = void*(__cdecl*)(uint32_t);
using GcHandleFreeFn = void(__cdecl*)(uint32_t);
using DomainGetFn = void*(__cdecl*)();
using ThreadAttachFn = void*(__cdecl*)(void*);
GcHandleNewFn g_gcNew = nullptr;
GcHandleTargetFn g_gcTarget = nullptr;
GcHandleFreeFn g_gcFree = nullptr;
DomainGetFn g_domainGet = nullptr;
ThreadAttachFn g_threadAttach = nullptr;
thread_local void* g_attachedThread = nullptr;

struct HeldShot {
    uintptr_t socket = 0;
    uintptr_t methodInfo = 0;
    uint32_t handle = 0;
};

std::mutex g_shotMutex;
std::deque<HeldShot> g_heldShots;
constexpr size_t kMaxHeldShots = 4096;
constexpr size_t kReleaseBatchSize = 4;

std::atomic<bool> g_active{false};
constexpr uint8_t kShotPacketId = 30;                 // HJNFJAHAOOE
constexpr uintptr_t kShotPacketCallerRva = 0x7D8FCB; // instruction after SendMessage call
bool      g_prev = false;
bool      g_hotkeyWasDown = false;
ULONGLONG g_sinceMs = 0;

bool HotkeyDown(const Keybind& bind) {
    int vk = bind.vk;
    if (vk < 0) {
        static constexpr int mouseKeys[] = {
            VK_LBUTTON, VK_RBUTTON, VK_MBUTTON, VK_XBUTTON1, VK_XBUTTON2
        };
        const int button = -vk - 1;
        if (button < 0 || button >= 5)
            return false;
        vk = mouseKeys[button];
    }
    return vk > 0 && (GetAsyncKeyState(vk) & 0x8000) != 0;
}


float __fastcall hkMoveSpeed(uintptr_t self, uintptr_t methodInfo) {
    float r = g_orig ? g_orig(self, methodInfo) : 0.0f;
    if (g_active.load(std::memory_order_acquire) &&
        g_cfg.socketFuRestrictMovement) {
        const float mult = speedhack::CurrentSpeed();
        if (mult > 1.5f)
            r = (1.5f / mult) * r;
    }
    return r;
}

void FlushHeldShots(size_t maxShots) {
    std::vector<HeldShot> shots;
    {
        std::lock_guard<std::mutex> lock(g_shotMutex);
        const size_t count = (std::min)(maxShots, g_heldShots.size());
        shots.reserve(count);
        for (size_t i = 0; i < count; ++i) {
            shots.push_back(g_heldShots.front());
            g_heldShots.pop_front();
        }
    }

    size_t sent = 0;
    for (const HeldShot& held : shots) {
        void* shot = g_gcTarget && held.handle
            ? g_gcTarget(held.handle)
            : nullptr;
        if (shot && g_origSend) {
            g_origSend(held.socket, reinterpret_cast<uintptr_t>(shot),
                       held.methodInfo);
            ++sent;
        }
        if (g_gcFree && held.handle)
            g_gcFree(held.handle);
    }

    if (!shots.empty())
        DBLOG("socketfu: released %llu/%llu held shots",
              static_cast<unsigned long long>(sent),
              static_cast<unsigned long long>(shots.size()));
}

void __fastcall hkSendMessage(uintptr_t self, uintptr_t packet,
                              uintptr_t methodInfo) {
    if (g_active.load(std::memory_order_acquire)) {
        if (packet > 0xFFFF) {
            const auto caller = reinterpret_cast<uintptr_t>(_ReturnAddress());
            const uintptr_t base = ga::Base();
            const uintptr_t callerRva = base && caller >= base ? caller - base : 0;
            const uint8_t packetId = *reinterpret_cast<const uint8_t*>(packet + 0x18);
            if (packetId == kShotPacketId &&
                callerRva == kShotPacketCallerRva && g_gcNew) {
                // SendMessage runs on the socket worker, which is not always
                // registered with IL2CPP's GC. Handle APIs crash on an
                // unregistered native thread, so attach it once first.
                if (!g_attachedThread && g_domainGet && g_threadAttach) {
                    if (void* domain = g_domainGet())
                        g_attachedThread = g_threadAttach(domain);
                }
                if (!g_attachedThread) {
                    if (g_origSend)
                        g_origSend(self, packet, methodInfo);
                    return;
                }
                const uint32_t handle =
                    g_gcNew(reinterpret_cast<void*>(packet), false);
                if (handle) {
                    std::lock_guard<std::mutex> lock(g_shotMutex);
                    if (g_heldShots.size() < kMaxHeldShots) {
                        g_heldShots.push_back({self, methodInfo, handle});
                        return;
                    }
                    if (g_gcFree)
                        g_gcFree(handle);
                }
            }
        }
    } else {
        // Drain a small ordered batch on each normal send. Replaying the whole
        // queue synchronously here can re-enter the socket pipeline dozens of
        // times and crash the client.
        FlushHeldShots(kReleaseBatchSize);
    }
    if (g_origSend)
        g_origSend(self, packet, methodInfo);
}

}

void Install() {
    if (HMODULE gaModule = GetModuleHandleA("GameAssembly.dll")) {
        g_gcNew = reinterpret_cast<GcHandleNewFn>(
            GetProcAddress(gaModule, "il2cpp_gchandle_new"));
        g_gcTarget = reinterpret_cast<GcHandleTargetFn>(
            GetProcAddress(gaModule, "il2cpp_gchandle_get_target"));
        g_gcFree = reinterpret_cast<GcHandleFreeFn>(
            GetProcAddress(gaModule, "il2cpp_gchandle_free"));
        g_domainGet = reinterpret_cast<DomainGetFn>(
            GetProcAddress(gaModule, "il2cpp_domain_get"));
        g_threadAttach = reinterpret_cast<ThreadAttachFn>(
            GetProcAddress(gaModule, "il2cpp_thread_attach"));
    }

    void* target = ga::Rva(ga::rva::SOCKETFU_MOVE_SPEED);
    DBLOG("socketfu::Install: move-speed target=%p (GA+0x%llX)", target,
          (unsigned long long)ga::rva::SOCKETFU_MOVE_SPEED);
    if (target) {
        const MH_STATUS st = MH_CreateHook(target, reinterpret_cast<void*>(&hkMoveSpeed),
                                           reinterpret_cast<void**>(&g_orig));
        DBLOG("socketfu::Install: MH_CreateHook=%d", (int)st);
    }

    void* sendTarget = ga::Rva(ga::rva::SOCKET_SEND);
    DBLOG("socketfu::Install: send target=%p (GA+0x%llX)",
          sendTarget, (unsigned long long)ga::rva::SOCKET_SEND);
    if (sendTarget) {
        const MH_STATUS st = MH_CreateHook(
            sendTarget, reinterpret_cast<void*>(&hkSendMessage),
            reinterpret_cast<void**>(&g_origSend));
        DBLOG("socketfu::Install: send MH_CreateHook=%d", (int)st);
    }
}

void Tick() {
    const bool hotkeyDown = HotkeyDown(g_cfg.socketFuHotkey);
    if (!g_cfg.socketFuHotkey.listening && hotkeyDown && !g_hotkeyWasDown)
        g_cfg.socketFu = !g_cfg.socketFu;
    g_hotkeyWasDown = hotkeyDown;
    const bool active = g_cfg.socketFu;


    if (active && !g_prev) {
        g_sinceMs = GetTickCount64();
        if (g_cfg.socketFuUseSecondSpeed)
            g_cfg.useSpeed1 = false;
        DBLOG("socketfu: ENGAGE");
    } else if (!active && g_prev) {
        size_t held = 0;
        {
            std::lock_guard<std::mutex> lock(g_shotMutex);
            held = g_heldShots.size();
        }
        DBLOG("socketfu: DISENGAGE (%llu held shots awaiting release)",
              static_cast<unsigned long long>(held));
    }
    g_prev = active;
    g_active.store(active, std::memory_order_release);


    // Only PlayerShoot packets are held. Movement, heartbeat/ACK, abilities,
    // chat and other protocol traffic continue normally.

    if (active && g_cfg.showSocketFuTimer) {
        const double secs = static_cast<double>(GetTickCount64() - g_sinceMs) / 1000.0;
        ImGui::SetNextWindowBgAlpha(0.40f);
        ImGui::Begin("##client_socketfu", nullptr,
                     ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
                     ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoSavedSettings |
                     ImGuiWindowFlags_NoFocusOnAppearing);
        ImGui::Text("SocketFU: %.1f", secs);
        ImGui::End();
    }
}

}
