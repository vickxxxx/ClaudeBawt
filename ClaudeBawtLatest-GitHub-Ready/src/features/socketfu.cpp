#include "features.h"
#include "config.h"
#include "il2cpp.h"
#include "log.h"

#include <windows.h>
#include <algorithm>
#include <atomic>
#include <cstdint>
#include "imgui.h"
#include "MinHook.h"

namespace socketfu {
namespace {

using SpeedFn = float(__fastcall*)(uintptr_t, uintptr_t);
SpeedFn g_origSpeed = nullptr;

// JOEMEFDPIIP::AIPAABAADIN() processes exactly one item from the game's
// Queue<IEnumerable<byte>> and dequeues it after scheduling the send.
using QueuePumpFn = void(__fastcall*)(uintptr_t, uintptr_t);
QueuePumpFn g_origQueuePump = nullptr;

using DomainGetFn = void* (*)();
using DomainAssemblyOpenFn = void* (*)(void*, const char*);
using AssemblyGetImageFn = void* (*)(void*);
using ClassFromNameFn = void* (*)(void*, const char*, const char*);
using ClassGetMethodFn = const void* (*)(void*, const char*, int);
using MethodGetPointerFn = void* (*)(const void*);

std::atomic<bool> g_active{false};
bool g_prev = false;
bool g_hotkeyWasDown = false;
ULONGLONG g_sinceMs = 0;
std::atomic<uint64_t> g_pumpCalls{0};
std::atomic<uint64_t> g_blockedPumps{0};

void* ResolveQueuePump() {
    HMODULE game = GetModuleHandleA("GameAssembly.dll");
    if (!game)
        return nullptr;

    const auto domainGet = reinterpret_cast<DomainGetFn>(
        GetProcAddress(game, "il2cpp_domain_get"));
    const auto assemblyOpen = reinterpret_cast<DomainAssemblyOpenFn>(
        GetProcAddress(game, "il2cpp_domain_assembly_open"));
    const auto assemblyGetImage = reinterpret_cast<AssemblyGetImageFn>(
        GetProcAddress(game, "il2cpp_assembly_get_image"));
    const auto classFromName = reinterpret_cast<ClassFromNameFn>(
        GetProcAddress(game, "il2cpp_class_from_name"));
    const auto classGetMethod = reinterpret_cast<ClassGetMethodFn>(
        GetProcAddress(game, "il2cpp_class_get_method_from_name"));
    const auto methodGetPointer = reinterpret_cast<MethodGetPointerFn>(
        GetProcAddress(game, "il2cpp_method_get_pointer"));
    if (!domainGet || !assemblyOpen || !assemblyGetImage || !classFromName ||
        !classGetMethod)
        return nullptr;

    void* domain = domainGet();
    void* assembly = domain ? assemblyOpen(domain, "Assembly-CSharp") : nullptr;
    void* image = assembly ? assemblyGetImage(assembly) : nullptr;
    void* klass = image ? classFromName(image, "", "JOEMEFDPIIP") : nullptr;
    const void* method = klass
        ? classGetMethod(klass, "AIPAABAADIN", 0)
        : nullptr;
    if (!method)
        return nullptr;
    return methodGetPointer
        ? methodGetPointer(method)
        : *reinterpret_cast<void* const*>(method);
}

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
    float result = g_origSpeed ? g_origSpeed(self, methodInfo) : 0.0f;
    if (g_active.load(std::memory_order_acquire)) {
        // The slider scales the client clock. Cancel that factor from the
        // player's movement calculation so it changes game time/shot cadence,
        // not movement speed.
        const float clockFactor =
            std::clamp(g_cfg.socketFuSpeedFactor, 1.0f, 3.0f);
        if (clockFactor > 1.0f)
            result /= clockFactor;
    }
    return result;
}

void __fastcall hkQueuePump(uintptr_t self, uintptr_t methodInfo) {
    // Do not dequeue, serialize again, retain managed objects ourselves, or
    // block this thread. The game's own queue keeps every encrypted frame in
    // exact order while local movement and inbound processing continue.
    const uint64_t call = g_pumpCalls.fetch_add(1, std::memory_order_relaxed) + 1;
    if (call == 1)
        DBLOG("socketfu: live queue pump reached self=%p", (void*)self);
    if (g_active.load(std::memory_order_acquire)) {
        g_blockedPumps.fetch_add(1, std::memory_order_relaxed);
        return;
    }

    if (g_origQueuePump)
        g_origQueuePump(self, methodInfo);
}

} // namespace

void Install() {
    void* speedTarget = ga::Rva(ga::rva::SOCKETFU_MOVE_SPEED);
    DBLOG("socketfu::Install: move-speed target=%p (GA+0x%llX)", speedTarget,
          static_cast<unsigned long long>(ga::rva::SOCKETFU_MOVE_SPEED));
    if (speedTarget) {
        const MH_STATUS status = MH_CreateHook(
            speedTarget, reinterpret_cast<void*>(&hkMoveSpeed),
            reinterpret_cast<void**>(&g_origSpeed));
        DBLOG("socketfu::Install: move-speed MH_CreateHook=%d", (int)status);
    }

    void* pumpTarget = ResolveQueuePump();
    if (!pumpTarget)
        pumpTarget = ga::Rva(ga::rva::SOCKET_QUEUE_PUMP);
    const uintptr_t pumpRva = pumpTarget
        ? reinterpret_cast<uintptr_t>(pumpTarget) - ga::Base()
        : 0;
    DBLOG("socketfu::Install: queue-pump target=%p (runtime GA+0x%llX)",
          pumpTarget, static_cast<unsigned long long>(pumpRva));
    if (pumpTarget) {
        const MH_STATUS status = MH_CreateHook(
            pumpTarget, reinterpret_cast<void*>(&hkQueuePump),
            reinterpret_cast<void**>(&g_origQueuePump));
        DBLOG("socketfu::Install: queue-pump MH_CreateHook=%d", (int)status);
    }
}

void Tick() {
    const bool hotkeyDown = HotkeyDown(g_cfg.socketFuHotkey);
    if (!g_cfg.socketFuHotkey.listening && hotkeyDown && !g_hotkeyWasDown)
        g_cfg.socketFu = !g_cfg.socketFu;
    g_hotkeyWasDown = hotkeyDown;

    const bool active = g_cfg.socketFu;
    g_active.store(active, std::memory_order_release);

    if (active && !g_prev) {
        g_sinceMs = GetTickCount64();
        g_pumpCalls.store(0, std::memory_order_relaxed);
        g_blockedPumps.store(0, std::memory_order_relaxed);
        DBLOG("socketfu: ENGAGE (outbound queue paused)");
    } else if (!active && g_prev) {
        DBLOG("socketfu: DISENGAGE (normal ordered drain resumed; calls=%llu blocked=%llu)",
              static_cast<unsigned long long>(
                  g_pumpCalls.load(std::memory_order_relaxed)),
              static_cast<unsigned long long>(
                  g_blockedPumps.load(std::memory_order_relaxed)));
    }
    g_prev = active;

    if (active && g_cfg.showSocketFuTimer) {
        const double seconds =
            static_cast<double>(GetTickCount64() - g_sinceMs) / 1000.0;
        ImGui::SetNextWindowBgAlpha(0.40f);
        ImGui::Begin("##client_socketfu", nullptr,
                     ImGuiWindowFlags_NoDecoration |
                         ImGuiWindowFlags_AlwaysAutoResize |
                         ImGuiWindowFlags_NoInputs |
                         ImGuiWindowFlags_NoSavedSettings |
                         ImGuiWindowFlags_NoFocusOnAppearing);
        ImGui::Text("SocketFU: %.1f", seconds);
        ImGui::End();
    }
}

} // namespace socketfu
