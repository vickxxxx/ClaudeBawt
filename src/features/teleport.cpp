


#include "features.h"
#include "config.h"
#include "il2cpp.h"

#include <windows.h>

namespace teleport {
namespace {

bool  g_haveAnchor = false;
float g_anchorX = 0.0f;
float g_anchorY = 0.0f;
bool  g_capturePrev = false;
bool  g_returnPrev = false;

bool KeyDown(int vk) {
    return vk > 0 && (GetAsyncKeyState(vk) & 0x8000) != 0;
}


void WritePosition(uintptr_t player, float tx, float ty) {
    *reinterpret_cast<float*>(player + 0x3C)  = tx;
    *reinterpret_cast<float*>(player + 0x40)  = ty;
    *reinterpret_cast<float*>(player + 0x68)  = tx;
    *reinterpret_cast<float*>(player + 0x6C)  = -ty;
    *reinterpret_cast<float*>(player + 0x278) = tx;
    *reinterpret_cast<float*>(player + 0x27C) = ty;
    *reinterpret_cast<float*>(player + 0x280) = tx;
    *reinterpret_cast<float*>(player + 0x284) = ty;
    *reinterpret_cast<float*>(player + 0x5F0) = tx;
    *reinterpret_cast<float*>(player + 0x5F4) = -ty;
}

}

void Install() {}
void Tick() {}


void Poll() {
    const uintptr_t player = game::Player();
    if (!player)
        return;

    const float px = *reinterpret_cast<float*>(player + 0x3C);
    const float py = *reinterpret_cast<float*>(player + 0x40);

    const bool capture = KeyDown(g_cfg.tpCaptureHotkey.vk);
    const bool ret = KeyDown(g_cfg.tpReturnHotkey.vk);

    if (capture && !g_capturePrev) {
        g_anchorX = px;
        g_anchorY = py;
        g_haveAnchor = true;
    }
    g_capturePrev = capture;

    if (g_haveAnchor) {
        if (ret && !g_returnPrev) {
            WritePosition(player, g_anchorX, g_anchorY);
        } else if (g_cfg.teleportIfOutOfRange && g_cfg.dogeTeleportMax > 0.0f) {
            const float dx = px - g_anchorX;
            const float dy = py - g_anchorY;
            const float maxd = g_cfg.dogeTeleportMax;
            if (dx * dx + dy * dy > maxd * maxd)
                WritePosition(player, g_anchorX, g_anchorY);
        }
    }
    g_returnPrev = ret;
}

}
