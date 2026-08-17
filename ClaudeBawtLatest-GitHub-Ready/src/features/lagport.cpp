


#include "features.h"
#include "config.h"

#include <windows.h>

namespace lagport {
namespace {

bool KeyDown(int vk) {
    return vk > 0 && (GetAsyncKeyState(vk) & 0x8000) != 0;
}

}

bool FreezeActive() {
    return g_cfg.lagPort && KeyDown(g_cfg.lagPortHotkey.vk);
}

void Install() {}
void Tick() {}

}
