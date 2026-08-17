#pragma once
#include <d3d11.h>

namespace overlay {
    bool Init(IDXGISwapChain* swap);
    bool Initialized();
    void RenderFrame(IDXGISwapChain* swap);

    HWND Window();
    ID3D11Device* Device();
    ID3D11DeviceContext* Context();

    void InvalidateRenderTarget();
    void Shutdown();
}
