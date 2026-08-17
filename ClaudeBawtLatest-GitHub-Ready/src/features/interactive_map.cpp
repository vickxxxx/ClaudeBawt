#include "interactive_map.h"

#include "config.h"
#include "overlay.h"
#include "notifications.h"

#include "imgui.h"

#include <windows.h>
#include <wincodec.h>
#include <d3d11.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace interactive_map {
namespace {

struct Texture {
    ID3D11ShaderResourceView* view = nullptr;
    UINT width = 0;
    UINT height = 0;
};

Texture g_base;
Texture g_portals;
Texture g_rookie;
Texture g_adept;
Texture g_veteran;
Texture g_names;
Texture g_logo;
bool g_loadAttempted = false;
bool g_loaded = false;
ImVec2 g_pan(0.0f, 0.0f);

void Release(Texture& texture) {
    if (texture.view) texture.view->Release();
    texture = {};
}

void ReleaseAll() {
    Release(g_base);
    Release(g_portals);
    Release(g_rookie);
    Release(g_adept);
    Release(g_veteran);
    Release(g_names);
    Release(g_logo);
    g_loaded = false;
}

bool LoadTexture(const char* path, Texture& out) {
    ID3D11Device* device = overlay::Device();
    if (!device || !path) return false;

    wchar_t widePath[MAX_PATH]{};
    if (!MultiByteToWideChar(CP_UTF8, 0, path, -1, widePath, MAX_PATH))
        return false;

    IWICImagingFactory* factory = nullptr;
    IWICBitmapDecoder* decoder = nullptr;
    IWICBitmapFrameDecode* frame = nullptr;
    IWICFormatConverter* converter = nullptr;
    ID3D11Texture2D* texture = nullptr;
    bool ok = false;

    if (SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr,
            CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory))) &&
        SUCCEEDED(factory->CreateDecoderFromFilename(widePath, nullptr,
            GENERIC_READ, WICDecodeMetadataCacheOnLoad, &decoder)) &&
        SUCCEEDED(decoder->GetFrame(0, &frame)) &&
        SUCCEEDED(factory->CreateFormatConverter(&converter)) &&
        SUCCEEDED(converter->Initialize(frame, GUID_WICPixelFormat32bppRGBA,
            WICBitmapDitherTypeNone, nullptr, 0.0,
            WICBitmapPaletteTypeCustom)) &&
        SUCCEEDED(converter->GetSize(&out.width, &out.height)) &&
        out.width && out.height) {
        std::vector<unsigned char> pixels(
            static_cast<size_t>(out.width) * out.height * 4);
        if (SUCCEEDED(converter->CopyPixels(nullptr, out.width * 4,
                static_cast<UINT>(pixels.size()), pixels.data()))) {
            D3D11_TEXTURE2D_DESC desc{};
            desc.Width = out.width;
            desc.Height = out.height;
            desc.MipLevels = 1;
            desc.ArraySize = 1;
            desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
            desc.SampleDesc.Count = 1;
            desc.Usage = D3D11_USAGE_IMMUTABLE;
            desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
            D3D11_SUBRESOURCE_DATA data{pixels.data(), out.width * 4, 0};
            if (SUCCEEDED(device->CreateTexture2D(&desc, &data, &texture)))
                ok = SUCCEEDED(device->CreateShaderResourceView(
                    texture, nullptr, &out.view));
        }
    }

    if (texture) texture->Release();
    if (converter) converter->Release();
    if (frame) frame->Release();
    if (decoder) decoder->Release();
    if (factory) factory->Release();
    if (!ok) Release(out);
    return ok;
}

std::string AssetPath(const char* filename) {
    char modulePath[MAX_PATH]{};
    HMODULE self = nullptr;
    GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                          GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                      reinterpret_cast<LPCSTR>(&Tick), &self);
    GetModuleFileNameA(self, modulePath, MAX_PATH);
    char* slash = std::strrchr(modulePath, '\\');
    if (slash) *slash = '\0';
    std::string result = modulePath;
    result += "\\interactive-map\\";
    result += filename;
    return result;
}

bool EnsureLoaded() {
    if (g_loaded) return true;
    if (g_loadAttempted) return false;
    g_loadAttempted = true;

    g_loaded =
        LoadTexture(AssetPath("map.png").c_str(), g_base) &&
        LoadTexture(AssetPath("map-portals.png").c_str(), g_portals) &&
        LoadTexture(AssetPath("difficulties-rookie.png").c_str(), g_rookie) &&
        LoadTexture(AssetPath("difficulties-adept.png").c_str(), g_adept) &&
        LoadTexture(AssetPath("difficulties-veteran.png").c_str(), g_veteran) &&
        LoadTexture(AssetPath("biomes-names.png").c_str(), g_names) &&
        LoadTexture(AssetPath("logo.png").c_str(), g_logo);

    if (!g_loaded) {
        ReleaseAll();
        notifications::Push("Interactive Map", "Map assets could not be loaded",
                            IM_COL32(226, 92, 103, 255), 6.0f);
    }
    return g_loaded;
}

void DrawLayer(ImDrawList* draw, const Texture& texture,
               ImVec2 min, ImVec2 max) {
    if (texture.view)
        draw->AddImage(texture.view, min, max);
}

void DrawMapCanvas() {
    ImVec2 available = ImGui::GetContentRegionAvail();
    available.x = std::max(available.x, 100.0f);
    available.y = std::max(available.y, 100.0f);
    const ImVec2 canvasMin = ImGui::GetCursorScreenPos();
    const ImVec2 canvasMax(canvasMin.x + available.x,
                           canvasMin.y + available.y);

    ImGui::InvisibleButton("##interactive_map_canvas", available,
                           ImGuiButtonFlags_MouseButtonLeft);
    const bool hovered = ImGui::IsItemHovered();
    const bool dragging = ImGui::IsItemActive() &&
                          ImGui::IsMouseDragging(ImGuiMouseButton_Left);
    if (hovered && ImGui::GetIO().MouseWheel != 0.0f) {
        const float oldZoom = g_cfg.interactiveMapZoom;
        g_cfg.interactiveMapZoom = std::clamp(
            oldZoom + ImGui::GetIO().MouseWheel * 0.18f, 1.0f, 4.0f);
    }
    if (dragging) {
        const ImVec2 delta = ImGui::GetIO().MouseDelta;
        g_pan.x += delta.x;
        g_pan.y += delta.y;
    }
    if (hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
        g_pan = ImVec2(0.0f, 0.0f);
        g_cfg.interactiveMapZoom = 1.0f;
    }

    ImDrawList* draw = ImGui::GetWindowDrawList();
    draw->AddRectFilled(canvasMin, canvasMax, IM_COL32(7, 9, 12, 255), 4.0f);
    if (!g_loaded || !g_base.width || !g_base.height) {
        draw->AddText(ImVec2(canvasMin.x + 16.0f, canvasMin.y + 16.0f),
                      IM_COL32(220, 100, 110, 255), "Map assets unavailable");
        return;
    }

    const float fit = std::min(available.x / static_cast<float>(g_base.width),
                               available.y / static_cast<float>(g_base.height));
    const float scale = fit * g_cfg.interactiveMapZoom;
    const ImVec2 imageSize(g_base.width * scale, g_base.height * scale);
    const ImVec2 imageMin(canvasMin.x + (available.x - imageSize.x) * 0.5f + g_pan.x,
                          canvasMin.y + (available.y - imageSize.y) * 0.5f + g_pan.y);
    const ImVec2 imageMax(imageMin.x + imageSize.x,
                          imageMin.y + imageSize.y);

    draw->PushClipRect(canvasMin, canvasMax, true);
    DrawLayer(draw, g_base, imageMin, imageMax);
    if (g_cfg.mapShowRookie) DrawLayer(draw, g_rookie, imageMin, imageMax);
    if (g_cfg.mapShowAdept) DrawLayer(draw, g_adept, imageMin, imageMax);
    if (g_cfg.mapShowVeteran) DrawLayer(draw, g_veteran, imageMin, imageMax);
    if (g_cfg.mapShowNames) DrawLayer(draw, g_names, imageMin, imageMax);
    if (g_cfg.mapShowPortals) DrawLayer(draw, g_portals, imageMin, imageMax);
    draw->AddRect(imageMin, imageMax, IM_COL32(99, 105, 126, 190));
    draw->AddText(ImVec2(canvasMin.x + 10.0f, canvasMax.y - 22.0f),
                  IM_COL32(160, 165, 178, 210),
                  "Wheel: zoom   Drag: pan   Double-click: reset");
    draw->PopClipRect();
}

void RenderWindow() {
    ImGui::SetNextWindowSize(ImVec2(980.0f, 700.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSizeConstraints(ImVec2(720.0f, 500.0f),
                                        ImGui::GetIO().DisplaySize);
    if (!ImGui::Begin("Interactive Realm Map", &g_cfg.interactiveMapEnabled,
                      ImGuiWindowFlags_NoCollapse)) {
        ImGui::End();
        return;
    }

    ImGui::BeginChild("##interactive_map_sidebar", ImVec2(205.0f, 0.0f), true);
    if (g_logo.view) {
        const float logoWidth = 92.0f;
        const float logoHeight = logoWidth *
            static_cast<float>(g_logo.height) / static_cast<float>(g_logo.width);
        const float x = (ImGui::GetContentRegionAvail().x - logoWidth) * 0.5f;
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.0f, x));
        ImGui::Image(g_logo.view, ImVec2(logoWidth, logoHeight));
    }
    ImGui::TextWrapped("Realm of the Mad God Interactive Map");
    ImGui::Separator();
    ImGui::Checkbox("Biome Names", &g_cfg.mapShowNames);
    ImGui::Checkbox("Portals", &g_cfg.mapShowPortals);
    ImGui::Spacing();
    ImGui::TextUnformatted("Biome Difficulties");
    bool allDifficulties = g_cfg.mapShowRookie && g_cfg.mapShowAdept &&
                           g_cfg.mapShowVeteran;
    if (ImGui::Checkbox("All Difficulties", &allDifficulties)) {
        g_cfg.mapShowRookie = allDifficulties;
        g_cfg.mapShowAdept = allDifficulties;
        g_cfg.mapShowVeteran = allDifficulties;
    }
    ImGui::Indent();
    ImGui::Checkbox("Rookie", &g_cfg.mapShowRookie);
    ImGui::Checkbox("Adept", &g_cfg.mapShowAdept);
    ImGui::Checkbox("Veteran", &g_cfg.mapShowVeteran);
    ImGui::Unindent();
    ImGui::Spacing();
    ImGui::SliderFloat("Zoom", &g_cfg.interactiveMapZoom,
                       1.0f, 4.0f, "%.1fx");
    if (ImGui::Button("Reset View", ImVec2(-1.0f, 0.0f))) {
        g_pan = ImVec2(0.0f, 0.0f);
        g_cfg.interactiveMapZoom = 1.0f;
    }
    ImGui::SetCursorPosY(std::max(ImGui::GetCursorPosY(),
                                  ImGui::GetWindowHeight() - 52.0f));
    ImGui::Separator();
    ImGui::TextDisabled("Map by ErelDev");
    ImGui::TextDisabled("Used with permission");
    ImGui::EndChild();

    ImGui::SameLine();
    ImGui::BeginChild("##interactive_map_viewer", ImVec2(0.0f, 0.0f), true,
                      ImGuiWindowFlags_NoScrollbar |
                      ImGuiWindowFlags_NoScrollWithMouse);
    DrawMapCanvas();
    ImGui::EndChild();
    ImGui::End();
}

} // namespace

void Tick() {
    if (!g_cfg.interactiveMapEnabled) return;
    EnsureLoaded();
    RenderWindow();
}

void Shutdown() {
    ReleaseAll();
    g_loadAttempted = false;
}

} // namespace interactive_map
