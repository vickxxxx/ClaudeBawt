


#include "features.h"
#include "config.h"
#include "il2cpp.h"
#include "util.h"
#include "log.h"

#include <windows.h>
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstring>
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

}

float CurrentSpeed() {
    return g_cfg.socketFu
        ? static_cast<float>(Sanitize(g_cfg.socketFuSpeedFactor))
        : 1.0f;
}

// SpeedHackDetector::Update(). The original client disables this detector
// before installing its rebased clock hooks. This RVA was migrated from the
// old build's actual function start at 0x31F330 by matching the complete
// native method body and
// confirming it against dump.cs.
bool NeuterAntiSpeedCheck() {
    constexpr uintptr_t kRva = 0x31D2B0;
    uint8_t* site = static_cast<uint8_t*>(ga::Rva(kRva));
    if (!site) {
        DBLOG("speedhack: detector site null (GA not ready?)");
        return false;
    }
    static constexpr uint8_t kExpectedPrologue[] = {
        0x48, 0x89, 0x5C, 0x24, 0x08, 0x57, 0x48, 0x83, 0xEC, 0x20
    };
    if (std::memcmp(site, kExpectedPrologue, sizeof(kExpectedPrologue)) == 0) {
        const uint8_t patch[2] = {0xC3, 0x90};
        util::Patch(site, patch, sizeof(patch));
        DBLOG("speedhack: disabled SpeedHackDetector.Update at GA+0x%llX",
              static_cast<unsigned long long>(kRva));
        return true;
    }
    if (site[0] == 0xC3 || site[0] == 0xE9) {
        DBLOG("speedhack: detector already disabled at GA+0x%llX (%02X)",
              static_cast<unsigned long long>(kRva), (int)site[0]);
        return true;
    }
    DBLOG("speedhack: detector GA+0x%llX signature mismatch (%02X %02X); "
          "clock hooks not installed",
          static_cast<unsigned long long>(kRva), (int)site[0], (int)site[1]);
    return false;
}

void Install() {
    // Match the original logic: never scale clocks while the detector is live.
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

    DBLOG("speedhack: original rebased-clock hooks created (SocketFU-scoped)");

    AcquireSRWLockExclusive(&g_clockLock);
    SeedLocked();
    RebaseLocked(1.0);
    ReleaseSRWLockExclusive(&g_clockLock);
}

void Tick() {
    const double selected = g_cfg.socketFu
        ? Sanitize(g_cfg.socketFuSpeedFactor)
        : 1.0;
    AcquireSRWLockExclusive(&g_clockLock);
    if (selected != g_multiplier)
        RebaseLocked(selected);
    ReleaseSRWLockExclusive(&g_clockLock);
}

}
