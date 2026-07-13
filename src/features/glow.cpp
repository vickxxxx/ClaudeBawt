


#include "features.h"
#include "config.h"
#include "il2cpp.h"

#include <cstdint>

namespace glow {
namespace {

float g_hue = 0.0f;


void HueToRgb(float h, float& r, float& g, float& b) {
    const float six = h * 6.0f;
    const int sector = static_cast<int>(six);
    const float f = six - static_cast<float>(sector);
    switch (sector) {
        case 0:  r = 1.0f;     g = f;        b = 0.0f;     break;
        case 1:  r = 1.0f - f; g = 1.0f;     b = 0.0f;     break;
        case 2:  r = 0.0f;     g = 1.0f;     b = f;        break;
        case 3:  r = 0.0f;     g = 1.0f - f; b = 1.0f;     break;
        case 4:  r = f;        g = 0.0f;     b = 1.0f;     break;
        default: r = 1.0f;     g = 0.0f;     b = 1.0f - f; break;
    }
}

float* Material() {
    const uintptr_t player = game::Player();
    if (!player)
        return nullptr;
    const uintptr_t visual = *reinterpret_cast<uintptr_t*>(player + 0x10);
    if (!visual)
        return nullptr;
    return *reinterpret_cast<float**>(visual + 0x60);
}

}

void Install() {}

void Tick() {
    if (!g_cfg.enableGlow)
        return;
    float* m = Material();
    if (!m)
        return;

    if (g_cfg.rainbowGlow) {
        g_hue += 0.005f;
        if (g_hue >= 1.0f)
            g_hue -= 1.0f;
        float r, g, b;
        HueToRgb(g_hue, r, g, b);


        m[29] = r; m[30] = g; m[31] = b; m[32] = g_cfg.glowColor[3];
        m[25] = r; m[26] = g; m[27] = b; m[28] = g_cfg.glowOutline[3];
    } else {
        m[29] = g_cfg.glowColor[0];  m[30] = g_cfg.glowColor[1];
        m[31] = g_cfg.glowColor[2];  m[32] = g_cfg.glowColor[3];
        m[25] = g_cfg.glowOutline[0]; m[26] = g_cfg.glowOutline[1];
        m[27] = g_cfg.glowOutline[2]; m[28] = g_cfg.glowOutline[3];
    }
}

}
