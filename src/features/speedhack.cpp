


#include "features.h"
#include "config.h"
#include "il2cpp.h"
#include "util.h"
#include "log.h"

#include <windows.h>
#include <algorithm>
#include <atomic>
#include <cstdint>
#include "MinHook.h"

namespace speedhack {
namespace {

using GetTickCountFn = DWORD (WINAPI*)();
using GetTickCount64Fn = ULONGLONG (WINAPI*)();
using QueryPerformanceCounterFn = BOOL (WINAPI*)(LARGE_INTEGER*);

GetTickCountFn oGetTickCount = nullptr;
GetTickCount64Fn oGetTickCount64 = nullptr;
QueryPerformanceCounterFn oQueryPerformanceCounter = nullptr;

SRWLOCK g_clockLock = SRWLOCK_INIT;
double g_multiplier = 1.0;
DWORD g_realTick32 = 0;
DWORD g_fakeTick32 = 0;
ULONGLONG g_realTick64 = 0;
ULONGLONG g_fakeTick64 = 0;
LONGLONG g_realQpc = 0;
LONGLONG g_fakeQpc = 0;
bool g_seeded = false;
bool g_hotkeyHeld = false;

double Sanitize(float value) {
    if (!(value >= 0.0f) || value > 100.0f)
        return 1.0;
    return static_cast<double>(value);
}

void SeedLocked() {
    if (g_seeded || !oGetTickCount || !oGetTickCount64 || !oQueryPerformanceCounter)
        return;
    g_realTick32 = g_fakeTick32 = oGetTickCount();
    g_realTick64 = g_fakeTick64 = oGetTickCount64();
    LARGE_INTEGER qpc{};
    oQueryPerformanceCounter(&qpc);
    g_realQpc = g_fakeQpc = qpc.QuadPart;
    g_seeded = true;
}

void RebaseLocked(double next) {
    SeedLocked();
    if (!g_seeded) {
        g_multiplier = next;
        return;
    }

    DWORD now32 = oGetTickCount();
    ULONGLONG now64 = oGetTickCount64();
    LARGE_INTEGER nowQpc{};
    oQueryPerformanceCounter(&nowQpc);

    g_fakeTick32 += static_cast<DWORD>(
        static_cast<double>(static_cast<DWORD>(now32 - g_realTick32)) * g_multiplier);
    g_fakeTick64 += static_cast<ULONGLONG>(
        static_cast<double>(now64 - g_realTick64) * g_multiplier);
    g_fakeQpc += static_cast<LONGLONG>(
        static_cast<double>(nowQpc.QuadPart - g_realQpc) * g_multiplier);

    g_realTick32 = now32;
    g_realTick64 = now64;
    g_realQpc = nowQpc.QuadPart;
    g_multiplier = next;
}

DWORD WINAPI hkGetTickCount() {
    AcquireSRWLockShared(&g_clockLock);
    if (!g_seeded) {
        ReleaseSRWLockShared(&g_clockLock);
        return oGetTickCount ? oGetTickCount() : 0;
    }
    DWORD now = oGetTickCount();
    DWORD result = g_fakeTick32 + static_cast<DWORD>(
        static_cast<double>(static_cast<DWORD>(now - g_realTick32)) * g_multiplier);
    ReleaseSRWLockShared(&g_clockLock);
    return result;
}

ULONGLONG WINAPI hkGetTickCount64() {
    AcquireSRWLockShared(&g_clockLock);
    if (!g_seeded) {
        ReleaseSRWLockShared(&g_clockLock);
        return oGetTickCount64 ? oGetTickCount64() : 0;
    }
    ULONGLONG now = oGetTickCount64();
    ULONGLONG result = g_fakeTick64 + static_cast<ULONGLONG>(
        static_cast<double>(now - g_realTick64) * g_multiplier);
    ReleaseSRWLockShared(&g_clockLock);
    return result;
}

BOOL WINAPI hkQueryPerformanceCounter(LARGE_INTEGER* out) {
    if (!out)
        return FALSE;
    AcquireSRWLockShared(&g_clockLock);
    if (!g_seeded) {
        ReleaseSRWLockShared(&g_clockLock);
        return oQueryPerformanceCounter ? oQueryPerformanceCounter(out) : FALSE;
    }
    LARGE_INTEGER now{};
    BOOL ok = oQueryPerformanceCounter(&now);
    out->QuadPart = g_fakeQpc + static_cast<LONGLONG>(
        static_cast<double>(now.QuadPart - g_realQpc) * g_multiplier);
    ReleaseSRWLockShared(&g_clockLock);
    return ok;
}

bool KeyDown(int vk) {
    return vk > 0 && (GetAsyncKeyState(vk) & 0x8000) != 0;
}

}

float CurrentSpeed() {
    return static_cast<float>(Sanitize(g_cfg.useSpeed1 ? g_cfg.speedhackSpeed1
                                                       : g_cfg.speedhackSpeed2));
}


bool NeuterAntiSpeedCheck() {
    constexpr uintptr_t kRva = 0x31E310;
    uint8_t* site = static_cast<uint8_t*>(ga::Rva(kRva));
    if (!site) {
        DBLOG("speedhack: anti-check site null (GA not ready?)");
        return false;
    }
    if (site[0] == 0x40 && site[1] == 0x53) {
        const uint8_t patch[2] = { 0xC3, 0x90 };
        util::Patch(site, patch, sizeof(patch));
        DBLOG("speedhack: neutered anti-speed check at GA+0x%llX (40 53 -> C3 90)",
              (unsigned long long)kRva);
        return true;
    }
    if (site[0] == 0xC3 || site[0] == 0xE9) {
        DBLOG("speedhack: anti-check at GA+0x%llX already neutralized (%02X)",
              (unsigned long long)kRva, (int)site[0]);
        return true;
    }
    DBLOG("speedhack: anti-check at GA+0x%llX unexpected prologue (%02X %02X) - "
          "clock scaling NOT installed (would risk a kick)",
          (unsigned long long)kRva, (int)site[0], (int)site[1]);
    return false;
}

void Install() {


    if (!NeuterAntiSpeedCheck())
        return;

    HMODULE kernel = GetModuleHandleW(L"kernel32.dll");
    if (!kernel)
        return;

    void* tick32 = reinterpret_cast<void*>(GetProcAddress(kernel, "GetTickCount"));
    void* tick64 = reinterpret_cast<void*>(GetProcAddress(kernel, "GetTickCount64"));
    void* qpc = reinterpret_cast<void*>(GetProcAddress(kernel, "QueryPerformanceCounter"));

    if (tick32)
        MH_CreateHook(tick32, reinterpret_cast<void*>(&hkGetTickCount),
                      reinterpret_cast<void**>(&oGetTickCount));
    if (tick64)
        MH_CreateHook(tick64, reinterpret_cast<void*>(&hkGetTickCount64),
                      reinterpret_cast<void**>(&oGetTickCount64));
    if (qpc)
        MH_CreateHook(qpc, reinterpret_cast<void*>(&hkQueryPerformanceCounter),
                      reinterpret_cast<void**>(&oQueryPerformanceCounter));

    AcquireSRWLockExclusive(&g_clockLock);
    SeedLocked();
    RebaseLocked(Sanitize(g_cfg.speedhackSpeed1));
    ReleaseSRWLockExclusive(&g_clockLock);
}

void Tick() {


    int vk = g_cfg.speedToggleKey ? g_cfg.speedToggleKey : g_cfg.speedHackHotkey;
    bool down = KeyDown(vk);
    if (down && !g_hotkeyHeld)
        g_cfg.useSpeed1 = !g_cfg.useSpeed1;
    g_hotkeyHeld = down;

    double selected = Sanitize(g_cfg.useSpeed1 ? g_cfg.speedhackSpeed1
                                               : g_cfg.speedhackSpeed2);
    AcquireSRWLockExclusive(&g_clockLock);
    if (selected != g_multiplier)
        RebaseLocked(selected);
    ReleaseSRWLockExclusive(&g_clockLock);
}

}
