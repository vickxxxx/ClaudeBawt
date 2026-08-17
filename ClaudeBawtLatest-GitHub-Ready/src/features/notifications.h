#pragma once

#include "imgui.h"

namespace notifications {
    void Tick();
    void Push(const char* title, const char* message,
              ImU32 color = IM_COL32(135, 141, 218, 255),
              float duration = -1.0f);
    void RenderSettings();
}
