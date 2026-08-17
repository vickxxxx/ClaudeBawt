#include "skin_catalog.h"
#include "overlay.h"
#include "imgui.h"

#include <windows.h>
#include <winhttp.h>
#include <wincrypt.h>
#include <wincodec.h>
#include <d3d11.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace skin_catalog {
namespace {
    struct SkinEntry {
        int id = 0;
        std::string name;
        std::vector<unsigned char> png;
        ID3D11ShaderResourceView* texture = nullptr;
        int width = 0;
        int height = 0;
    };

    std::vector<SkinEntry> s_entries;
    std::mutex s_mutex;
    std::atomic<bool> s_loading{false};
    std::atomic<bool> s_loaded{false};
    std::atomic<bool> s_failed{false};
    char s_search[96]{};

    std::string HtmlDecode(std::string value) {
        struct Pair { const char* from; const char* to; };
        static const Pair pairs[] = {
            {"&amp;", "&"}, {"&#39;", "'"}, {"&quot;", "\""},
            {"&lt;", "<"}, {"&gt;", ">"}, {"&nbsp;", " "}
        };
        for (const auto& pair : pairs) {
            size_t pos = 0;
            while ((pos = value.find(pair.from, pos)) != std::string::npos) {
                value.replace(pos, strlen(pair.from), pair.to);
                pos += strlen(pair.to);
            }
        }
        return value;
    }

    bool DecodeBase64(const std::string& text, std::vector<unsigned char>& out) {
        DWORD size = 0;
        if (!CryptStringToBinaryA(text.c_str(), static_cast<DWORD>(text.size()),
                                  CRYPT_STRING_BASE64, nullptr, &size, nullptr, nullptr))
            return false;
        out.resize(size);
        return CryptStringToBinaryA(text.c_str(), static_cast<DWORD>(text.size()),
                                    CRYPT_STRING_BASE64, out.data(), &size, nullptr, nullptr) != FALSE;
    }

    std::vector<SkinEntry> Parse(const std::string& html) {
        std::vector<SkinEntry> result;
        size_t cursor = 0;
        while ((cursor = html.find("item-preview", cursor)) != std::string::npos) {
            const size_t cardEnd = html.find("item-preview", cursor + 12);
            const size_t end = cardEnd == std::string::npos ? html.size() : cardEnd;

            const size_t idTag = html.find("/object?id=", cursor);
            const size_t nameTag = html.find("card-header'>", cursor);
            if (idTag == std::string::npos || nameTag == std::string::npos ||
                idTag >= end || nameTag >= end) {
                cursor += 12;
                continue;
            }

            const size_t idStart = idTag + 11;
            size_t idEnd = idStart;
            while (idEnd < end && std::isdigit(static_cast<unsigned char>(html[idEnd])))
                ++idEnd;
            if (idEnd == idStart) {
                cursor += 12;
                continue;
            }

            const size_t nameStart = nameTag + 13;
            const size_t nameEnd = html.find("</div>", nameStart);
            if (nameEnd == std::string::npos || nameEnd > end) {
                cursor += 12;
                continue;
            }

            SkinEntry entry;
            try { entry.id = std::stoi(html.substr(idStart, idEnd - idStart)); }
            catch (...) { cursor += 12; continue; }
            entry.name = HtmlDecode(html.substr(nameStart, nameEnd - nameStart));

            const char* imageMarker = "src='data:image/png;base64,";
            const size_t imageTag = html.find(imageMarker, cursor);
            if (imageTag != std::string::npos && imageTag < end) {
                const size_t dataStart = imageTag + strlen(imageMarker);
                const size_t dataEnd = html.find('\'', dataStart);
                if (dataEnd != std::string::npos && dataEnd <= end)
                    DecodeBase64(html.substr(dataStart, dataEnd - dataStart), entry.png);
            }
            result.push_back(std::move(entry));
            cursor = end;
        }
        std::sort(result.begin(), result.end(), [](const SkinEntry& a, const SkinEntry& b) {
            return a.name < b.name;
        });
        return result;
    }

    bool Fetch(std::string& body) {
        HINTERNET session = WinHttpOpen(L"SkinCacheFetcher/1.0",
            WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME,
            WINHTTP_NO_PROXY_BYPASS, 0);
        if (!session) return false;
        HINTERNET connect = WinHttpConnect(session, L"realm.wiki",
                                           INTERNET_DEFAULT_HTTPS_PORT, 0);
        HINTERNET request = connect ? WinHttpOpenRequest(connect, L"GET",
            L"/list/ObjectClass?type=Skin", nullptr, WINHTTP_NO_REFERER,
            WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE) : nullptr;
        bool ok = request && WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS,
            0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) && WinHttpReceiveResponse(request, nullptr);
        while (ok) {
            DWORD available = 0;
            if (!WinHttpQueryDataAvailable(request, &available)) { ok = false; break; }
            if (!available) break;
            const size_t old = body.size();
            body.resize(old + available);
            DWORD read = 0;
            if (!WinHttpReadData(request, body.data() + old, available, &read)) {
                ok = false; break;
            }
            body.resize(old + read);
        }
        if (request) WinHttpCloseHandle(request);
        if (connect) WinHttpCloseHandle(connect);
        WinHttpCloseHandle(session);
        return ok && !body.empty();
    }

    bool CreateTexture(SkinEntry& entry) {
        if (entry.png.empty() || entry.texture || !overlay::Device()) return false;
        IWICImagingFactory* factory = nullptr;
        IWICStream* stream = nullptr;
        IWICBitmapDecoder* decoder = nullptr;
        IWICBitmapFrameDecode* frame = nullptr;
        IWICFormatConverter* converter = nullptr;
        bool ok = false;
        if (SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr,
              CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory))) &&
            SUCCEEDED(factory->CreateStream(&stream)) &&
            SUCCEEDED(stream->InitializeFromMemory(entry.png.data(),
              static_cast<DWORD>(entry.png.size()))) &&
            SUCCEEDED(factory->CreateDecoderFromStream(stream, nullptr,
              WICDecodeMetadataCacheOnLoad, &decoder)) &&
            SUCCEEDED(decoder->GetFrame(0, &frame)) &&
            SUCCEEDED(factory->CreateFormatConverter(&converter)) &&
            SUCCEEDED(converter->Initialize(frame, GUID_WICPixelFormat32bppRGBA,
              WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom))) {
            UINT w = 0, h = 0;
            converter->GetSize(&w, &h);
            std::vector<unsigned char> pixels(static_cast<size_t>(w) * h * 4);
            if (w && h && SUCCEEDED(converter->CopyPixels(nullptr, w * 4,
                    static_cast<UINT>(pixels.size()), pixels.data()))) {
                D3D11_TEXTURE2D_DESC desc{};
                desc.Width = w; desc.Height = h; desc.MipLevels = 1; desc.ArraySize = 1;
                desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
                desc.SampleDesc.Count = 1; desc.Usage = D3D11_USAGE_IMMUTABLE;
                desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
                D3D11_SUBRESOURCE_DATA data{pixels.data(), w * 4, 0};
                ID3D11Texture2D* texture = nullptr;
                if (SUCCEEDED(overlay::Device()->CreateTexture2D(&desc, &data, &texture))) {
                    ok = SUCCEEDED(overlay::Device()->CreateShaderResourceView(
                        texture, nullptr, &entry.texture));
                    texture->Release();
                    entry.width = static_cast<int>(w);
                    entry.height = static_cast<int>(h);
                }
            }
        }
        if (converter) converter->Release();
        if (frame) frame->Release();
        if (decoder) decoder->Release();
        if (stream) stream->Release();
        if (factory) factory->Release();
        entry.png.clear();
        entry.png.shrink_to_fit();
        return ok;
    }

    bool Matches(const SkinEntry& entry) {
        if (!s_search[0]) return true;
        std::string needle = s_search;
        std::string name = entry.name;
        std::transform(needle.begin(), needle.end(), needle.begin(), ::tolower);
        std::transform(name.begin(), name.end(), name.begin(), ::tolower);
        if (name.find(needle) != std::string::npos) return true;
        return std::to_string(entry.id).find(needle) != std::string::npos;
    }
}

void EnsureLoading() {
    if (s_loaded || s_loading) return;
    s_loading = true;
    std::thread([] {
        CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        std::string html;
        std::vector<SkinEntry> parsed;
        if (Fetch(html)) parsed = Parse(html);
        {
            std::lock_guard<std::mutex> lock(s_mutex);
            s_entries = std::move(parsed);
            s_failed = s_entries.empty();
            s_loaded = !s_entries.empty();
        }
        s_loading = false;
        CoUninitialize();
    }).detach();
}

void PumpTextures() {
    if (!s_loaded) return;
    std::lock_guard<std::mutex> lock(s_mutex);
    int budget = 4;
    for (auto& entry : s_entries) {
        if (budget <= 0) break;
        if (!entry.texture && !entry.png.empty()) {
            CreateTexture(entry);
            --budget;
        }
    }
}

void Render(int& selectedSkinId) {
    EnsureLoading();
    PumpTextures();
    if (s_loading) {
        ImGui::TextDisabled("Loading skins from realm.wiki...");
        return;
    }
    if (s_failed) {
        ImGui::TextDisabled("Failed to load realm.wiki skin catalog.");
        if (ImGui::Button("Retry Skin Catalog")) {
            s_failed = false;
            s_loaded = false;
            EnsureLoading();
        }
        return;
    }
    ImGui::InputTextWithHint("##SkinSearch", "Search Skins", s_search, sizeof(s_search));
    if (ImGui::BeginChild("##SkinList", ImVec2(0, 240), true)) {
        std::lock_guard<std::mutex> lock(s_mutex);
        for (auto& entry : s_entries) {
            if (!Matches(entry)) continue;
            ImGui::PushID(entry.id);
            if (entry.texture) {
                ImGui::Image((ImTextureID)entry.texture, ImVec2(32, 32));
                ImGui::SameLine();
            }
            std::string label = entry.name + "  [" + std::to_string(entry.id) + "]";
            if (ImGui::Selectable(label.c_str(), selectedSkinId == entry.id,
                                  ImGuiSelectableFlags_None, ImVec2(0, 32)))
                selectedSkinId = entry.id;
            ImGui::PopID();
        }
    }
    ImGui::EndChild();
}

void Shutdown() {
    std::lock_guard<std::mutex> lock(s_mutex);
    for (auto& entry : s_entries)
        if (entry.texture) entry.texture->Release();
    s_entries.clear();
    s_loaded = false;
}
}
