


#include "features.h"
#include "config.h"
#include "il2cpp.h"
#include "log.h"

#include <windows.h>
#include <algorithm>
#include <cstdint>
#include "MinHook.h"

namespace mods {
namespace {


constexpr uintptr_t kAntiIdleRva   = 0xA91300;
constexpr uintptr_t kCameraZoomRva = 0xE8D260; //0xF387C0;OLD
constexpr float kZoomMin = 0.01f;
constexpr float kZoomMax = 4.0f;


using PassFn = int64_t(__fastcall*)(uintptr_t, uintptr_t, uintptr_t, uintptr_t);
PassFn g_origAntiIdle = nullptr;


using ZoomFn = int64_t(__fastcall*)(uintptr_t, float, uintptr_t, uintptr_t);
ZoomFn g_origZoom = nullptr;


int64_t __fastcall hkAntiIdle(uintptr_t a0, uintptr_t a1, uintptr_t a2, uintptr_t a3) {
    if (g_cfg.antiIdle)
        return 0;
    return g_origAntiIdle ? g_origAntiIdle(a0, a1, a2, a3) : 0;
}


int64_t __fastcall hkCameraZoom(uintptr_t self, float zoom, uintptr_t a2, uintptr_t a3) {
    const float scale = std::min(kZoomMax, std::max(kZoomMin, g_cfg.cameraZoomScale));
    return g_origZoom ? g_origZoom(self, zoom * scale, a2, a3) : 0;
}

}

void Install() {
    // Disabled until both targets and native signatures are verified for this build.
    DBLOG("mods::Install: anti-idle and camera-zoom hooks disabled");
}

void Tick() {}

}
