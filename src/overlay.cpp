#include "overlay.h"
#include "menu.h"
#include "config.h"
#include "log.h"
#include "features/features.h"
#include "skin_catalog.h"

#include "imgui.h"
#include "imgui_impl_dx11.h"
#include "imgui_impl_win32.h"

#include <windows.h>
#include <cstdio>
#include <cstring>

extern int g_viewW, g_viewH;

namespace overlay {
    namespace {
        bool                    s_init = false;
        HWND                    s_hwnd = nullptr;
        ID3D11Device*           s_device = nullptr;
        ID3D11DeviceContext*    s_ctx = nullptr;
        ID3D11RenderTargetView* s_rtv = nullptr;
        ID3D11Texture2D*        s_backBuffer = nullptr;
        IDXGISwapChain*         s_swap = nullptr;

        void ReleaseRenderTarget() {
            if (s_rtv) { s_rtv->Release(); s_rtv = nullptr; }
            if (s_backBuffer) { s_backBuffer->Release(); s_backBuffer = nullptr; }
        }

        bool EnsureRenderTarget(IDXGISwapChain* swap) {
            if (!swap || !s_device) return false;
            ID3D11Texture2D* back = nullptr;
            if (FAILED(swap->GetBuffer(0, IID_PPV_ARGS(&back))) || !back) return false;

            if (s_backBuffer == back && s_rtv) {
                back->Release();
                return true;
            }

            ReleaseRenderTarget();
            s_backBuffer = back;
            if (FAILED(s_device->CreateRenderTargetView(back, nullptr, &s_rtv))) {
                ReleaseRenderTarget();
                return false;
            }
            return true;
        }

        void ReleaseDevice() {
            ReleaseRenderTarget();
            if (s_ctx) { s_ctx->Release(); s_ctx = nullptr; }
            if (s_device) { s_device->Release(); s_device = nullptr; }
            s_swap = nullptr;
            s_hwnd = nullptr;
        }
    }

    bool Init(IDXGISwapChain* swap) {
        if (s_init) return true;
        if (!swap) return false;

        if (FAILED(swap->GetDevice(IID_PPV_ARGS(&s_device))) || !s_device) return false;
        s_device->GetImmediateContext(&s_ctx);
        if (!s_ctx) { ReleaseDevice(); return false; }

        DXGI_SWAP_CHAIN_DESC desc{};
        if (FAILED(swap->GetDesc(&desc)) || !desc.OutputWindow) {
            ReleaseDevice();
            return false;
        }

        s_hwnd = desc.OutputWindow;
        s_swap = swap;

        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

        const UINT dpi = GetDpiForWindow(s_hwnd);
        const float dpiScale = dpi ? static_cast<float>(dpi) / 96.0f : 1.0f;
        const float fontSize = 16.0f * dpiScale;
        ImFontConfig fontCfg{};
        fontCfg.SizePixels = fontSize;
        fontCfg.OversampleH = 1;
        fontCfg.OversampleV = 1;
        fontCfg.PixelSnapH = true;

        char dllPath[MAX_PATH]{};
        char boldPath[MAX_PATH]{};
        HMODULE self = nullptr;
        GetModuleHandleExA(
            GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCSTR>(&Init), &self);
        GetModuleFileNameA(self, dllPath, MAX_PATH);
        char* slash = std::strrchr(dllPath, '\\');
        if (slash) *slash = '\0';
        std::snprintf(boldPath, sizeof(boldPath), "%s\\font\\PixelOperator-Bold.ttf", dllPath);

        ImFont* bold = io.Fonts->AddFontFromFileTTF(boldPath, fontSize, &fontCfg);
        if (!bold) bold = io.Fonts->AddFontDefault(&fontCfg);
        io.FontDefault = bold;
        menu::SetFonts(bold, bold);

        if (!ImGui_ImplWin32_Init(s_hwnd) || !ImGui_ImplDX11_Init(s_device, s_ctx)) {
            ImGui_ImplDX11_Shutdown();
            ImGui_ImplWin32_Shutdown();
            ImGui::DestroyContext();
            ReleaseDevice();
            return false;
        }

        menu::ApplyTheme(g_cfg.menuTheme);
        if (!EnsureRenderTarget(swap)) {
            ImGui_ImplDX11_Shutdown();
            ImGui_ImplWin32_Shutdown();
            ImGui::DestroyContext();
            ReleaseDevice();
            return false;
        }

        s_init = true;
        return true;
    }

    bool Initialized() { return s_init; }
    HWND Window() { return s_hwnd; }
    ID3D11Device* Device() { return s_device; }
    ID3D11DeviceContext* Context() { return s_ctx; }

    void InvalidateRenderTarget() { ReleaseRenderTarget(); }

    void RenderFrame(IDXGISwapChain* swap) {
        if (!s_init || !swap) return;

        if (swap != s_swap) {
            ID3D11Device* newDevice = nullptr;
            if (FAILED(swap->GetDevice(IID_PPV_ARGS(&newDevice))) || !newDevice) return;
            const bool sameDevice = newDevice == s_device;
            newDevice->Release();
            if (!sameDevice) return;
            s_swap = swap;
            ReleaseRenderTarget();
        }
        if (!EnsureRenderTarget(swap)) return;

        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        const ImVec2 display = ImGui::GetIO().DisplaySize;
        ::g_viewW = static_cast<int>(display.x);
        ::g_viewH = static_cast<int>(display.y);

        static bool once = false;
        if (!once) { once = true; DBLOG("RenderFrame: first frame, view=%dx%d, calling features::Tick", ::g_viewW, ::g_viewH); }
        features::Tick();
        menu::Render();

        ImGui::Render();
        s_ctx->OMSetRenderTargets(1, &s_rtv, nullptr);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
    }

    void Shutdown() {
        if (!s_init) {
            ReleaseDevice();
            return;
        }

        menu::SetOpen(false);
        skin_catalog::Shutdown();
        Config_Save();
        ImGui_ImplDX11_Shutdown();
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext();
        ReleaseDevice();
        s_init = false;
    }
}
