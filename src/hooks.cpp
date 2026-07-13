#include "hooks.h"
#include "overlay.h"
#include "config.h"
#include "menu.h"
#include "il2cpp.h"
#include "log.h"
#include "features/features.h"

#include <windows.h>
#include <atomic>
#include "imgui.h"
#include "MinHook.h"

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);

namespace hooks {
    HRESULT (__stdcall* oPresent)(IDXGISwapChain*, UINT, UINT) = nullptr;

    static void**   s_presentSlot = nullptr;
    static WNDPROC  s_oWndProc    = nullptr;
    static HWND     s_wndWindow   = nullptr;
    static SRWLOCK  s_stateLock   = SRWLOCK_INIT;
    static std::atomic<bool> s_installed{false};
    static std::atomic<bool> s_wndHooked{false};
    static std::atomic<bool> s_featuresInstalled{false};
    static std::atomic<bool> s_unloading{false};
    static std::atomic<long> s_presentCalls{0};

    static HRESULT __stdcall hkPresent(IDXGISwapChain* swap, UINT sync, UINT flags);

    static LRESULT __stdcall hkWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {

        if (msg == WM_KEYDOWN && (int)wParam == g_cfg.menuToggleHotkey)
            menu::Toggle();

        if (overlay::Initialized() && menu::IsOpen()) {
            if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam))
                return TRUE;
            ImGuiIO& io = ImGui::GetIO();
            if (io.WantCaptureMouse && msg >= WM_MOUSEFIRST && msg <= WM_MOUSELAST)
                return TRUE;
            if (io.WantCaptureKeyboard && msg >= WM_KEYFIRST && msg <= WM_KEYLAST)
                return TRUE;
        }
        return s_oWndProc
            ? CallWindowProc(s_oWndProc, hWnd, msg, wParam, lParam)
            : DefWindowProc(hWnd, msg, wParam, lParam);
    }

    static HRESULT __stdcall hkPresent(IDXGISwapChain* swap, UINT sync, UINT flags) {
        s_presentCalls.fetch_add(1, std::memory_order_acq_rel);
        auto original = oPresent;

        if (!s_unloading.load(std::memory_order_acquire)) {


            if (!s_featuresInstalled.load(std::memory_order_acquire) && ga::Init()) {
                DBLOG("hkPresent: first frame, features::InstallAll()");
                features::InstallAll();
                s_featuresInstalled.store(true, std::memory_order_release);
                DBLOG("hkPresent: features::InstallAll() done");
            }

            if (!overlay::Initialized()) {
                if (overlay::Init(swap) && !s_wndHooked.load(std::memory_order_acquire)) {
                    HWND window = overlay::Window();
                    SetLastError(ERROR_SUCCESS);
                    WNDPROC previous = reinterpret_cast<WNDPROC>(
                        SetWindowLongPtr(window, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(hkWndProc)));
                    if (previous || GetLastError() == ERROR_SUCCESS) {
                        s_oWndProc = previous;
                        s_wndWindow = window;
                        s_wndHooked.store(true, std::memory_order_release);
                    }
                }
            }
            if (overlay::Initialized())
                overlay::RenderFrame(swap);
        }

        const HRESULT result = original ? original(swap, sync, flags) : E_FAIL;
        s_presentCalls.fetch_sub(1, std::memory_order_acq_rel);
        return result;
    }

    bool Install() {
        if (s_installed.load(std::memory_order_acquire))
            return true;

        AcquireSRWLockExclusive(&s_stateLock);
        if (s_installed.load(std::memory_order_relaxed)) {
            ReleaseSRWLockExclusive(&s_stateLock);
            return true;
        }

        DXGI_SWAP_CHAIN_DESC desc{};
        desc.BufferCount = 1;
        desc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        desc.OutputWindow = GetForegroundWindow();
        desc.SampleDesc.Count = 1;
        desc.Windowed = TRUE;
        desc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

        D3D_FEATURE_LEVEL level = D3D_FEATURE_LEVEL_11_0;
        IDXGISwapChain* swap = nullptr; ID3D11Device* dev = nullptr; ID3D11DeviceContext* ctx = nullptr;
        DBLOG("hooks::Install: D3D11CreateDeviceAndSwapChain");
        HRESULT hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0,
                                                   &level, 1, D3D11_SDK_VERSION, &desc,
                                                   &swap, &dev, nullptr, &ctx);
        DBLOG("hooks::Install: D3D11CreateDeviceAndSwapChain hr=0x%08lX swap=%p", (unsigned long)hr, (void*)swap);
        if (FAILED(hr) || !swap) {
            if (ctx) ctx->Release();
            if (dev) dev->Release();
            if (swap) swap->Release();
            ReleaseSRWLockExclusive(&s_stateLock);
            return false;
        }


        s_presentSlot = &(*reinterpret_cast<void***>(swap))[8];
        oPresent = reinterpret_cast<HRESULT(__stdcall*)(IDXGISwapChain*, UINT, UINT)>(*s_presentSlot);
        DBLOG("hooks::Install: Present slot=%p orig=%p, patching vtable", (void*)s_presentSlot, (void*)oPresent);

        DWORD prot = 0;
        bool patched = false;
        if (VirtualProtect(s_presentSlot, sizeof(void*), PAGE_EXECUTE_READWRITE, &prot)) {
            InterlockedExchangePointer(
                reinterpret_cast<void* volatile*>(s_presentSlot),
                reinterpret_cast<void*>(&hkPresent));
            DWORD ignored = 0;
            VirtualProtect(s_presentSlot, sizeof(void*), prot, &ignored);
            FlushInstructionCache(GetCurrentProcess(), s_presentSlot, sizeof(void*));
            patched = true;
        }

        if (ctx) ctx->Release();
        if (dev) dev->Release();
        if (swap) swap->Release();

        if (patched) {
            s_unloading.store(false, std::memory_order_release);
            s_installed.store(true, std::memory_order_release);
        } else {
            s_presentSlot = nullptr;
            oPresent = nullptr;
        }
        ReleaseSRWLockExclusive(&s_stateLock);
        return patched;
    }

    void Uninstall() {
        if (!s_installed.exchange(false, std::memory_order_acq_rel))
            return;

        s_unloading.store(true, std::memory_order_release);
        AcquireSRWLockExclusive(&s_stateLock);
        if (s_presentSlot && oPresent) {
            DWORD prot = 0;
            if (VirtualProtect(s_presentSlot, sizeof(void*), PAGE_EXECUTE_READWRITE, &prot)) {
                InterlockedCompareExchangePointer(
                    reinterpret_cast<void* volatile*>(s_presentSlot),
                    reinterpret_cast<void*>(oPresent),
                    reinterpret_cast<void*>(&hkPresent));
                DWORD ignored = 0;
                VirtualProtect(s_presentSlot, sizeof(void*), prot, &ignored);
            }
        }
        ReleaseSRWLockExclusive(&s_stateLock);


        for (unsigned i = 0; i != 2000 && s_presentCalls.load(std::memory_order_acquire) != 0; ++i)
            Sleep(1);


        MH_DisableHook(MH_ALL_HOOKS);
        MH_Uninitialize();

        if (s_wndHooked.load(std::memory_order_acquire) && s_oWndProc &&
            s_wndWindow && IsWindow(s_wndWindow) &&
            reinterpret_cast<WNDPROC>(GetWindowLongPtr(s_wndWindow, GWLP_WNDPROC)) == hkWndProc) {
            SetWindowLongPtr(s_wndWindow, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(s_oWndProc));
        }
        s_wndHooked.store(false, std::memory_order_release);
        s_wndWindow = nullptr;
        s_oWndProc = nullptr;

        overlay::Shutdown();
        game::ResetRuntimeState();
    }

    bool Installed() {
        return s_installed.load(std::memory_order_acquire);
    }
}
