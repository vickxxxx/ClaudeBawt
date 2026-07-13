
#pragma once
#include <d3d11.h>

namespace hooks {


    bool Install();
    void Uninstall();
    bool Installed();

    extern HRESULT (__stdcall* oPresent)(IDXGISwapChain*, UINT, UINT);
}
