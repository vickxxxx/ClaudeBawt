#pragma once

namespace skin_catalog {
    void EnsureLoading();
    void PumpTextures();
    void Render(int& selectedSkinId);
    void Shutdown();
}
