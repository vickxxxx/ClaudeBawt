
#include "features.h"
#include "config.h"
#include "il2cpp.h"
#include "log.h"

#include <windows.h>
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstring>
#include "MinHook.h"
#include "imgui.h"

namespace fame {
namespace {

constexpr uintptr_t kStatsUpdateRva = 0xC2CCF0;
constexpr uintptr_t kFameWritePatchRva = 0x365E2F;

using StatsUpdateFn = int64_t (__fastcall*)(uintptr_t, uintptr_t);
StatsUpdateFn oStatsUpdate = nullptr;
int g_selectedObjectId = -1;

using DomainGetFn = void* (*)();
using DomainAssemblyOpenFn = void* (*)(void*, const char*);
using AssemblyGetImageFn = void* (*)(void*);
using ClassFromNameFn = void* (*)(void*, const char*, const char*);
using ClassGetMethodFn = const void* (*)(void*, const char*, int);
using MethodGetPointerFn = void* (*)(const void*);
using StringNewFn = void* (*)(const char*);
using CharacterInfoVoidFn = void(__fastcall*)(void*, void*);
using CharacterInfoPlayerFn = void(__fastcall*)(void*, void*, void*);
using TmpSetTextFn = void(__fastcall*)(uintptr_t, uintptr_t, uintptr_t);

DomainGetFn il2cppDomainGet = nullptr;
DomainAssemblyOpenFn il2cppDomainAssemblyOpen = nullptr;
AssemblyGetImageFn il2cppAssemblyGetImage = nullptr;
ClassFromNameFn il2cppClassFromName = nullptr;
ClassGetMethodFn il2cppClassGetMethod = nullptr;
MethodGetPointerFn il2cppMethodGetPointer = nullptr;
StringNewFn il2cppStringNew = nullptr;
CharacterInfoVoidFn oCharacterInfoInit = nullptr;
CharacterInfoVoidFn oCharacterInfoRefresh = nullptr;
CharacterInfoVoidFn oCharacterInfoClean = nullptr;
CharacterInfoVoidFn oCharacterInfoDestroy = nullptr;
CharacterInfoPlayerFn oCharacterInfoPlayer[6]{};
std::atomic<uintptr_t> g_characterInfoPanel{0};
std::atomic<int> g_nameRetries{0};
char g_appliedName[25]{};

uint8_t g_patchBackup[6]{};
bool g_havePatchBackup = false;
bool g_patchEnabled = false;

bool ReadList(uintptr_t list, uintptr_t& data, int& count) {
    if (!list)
        return false;
    data = *reinterpret_cast<uintptr_t*>(list + 0x10);
    count = *reinterpret_cast<int*>(list + 0x18);
    return data && count > 0 && count <= 500;
}

void* MethodPointer(void* klass, const char* name, int argc) {
    if (!klass || !il2cppClassGetMethod)
        return nullptr;
    const void* method = il2cppClassGetMethod(klass, name, argc);
    return method
        ? (il2cppMethodGetPointer ? il2cppMethodGetPointer(method)
                                  : *reinterpret_cast<void* const*>(method))
        : nullptr;
}

bool ApplyCharacterInfoName(uintptr_t panel, const char* name) {
    if (!panel || !name || !name[0] || !il2cppStringNew || !il2cppClassGetMethod)
        return false;
    __try {
        const uintptr_t label = *reinterpret_cast<uintptr_t*>(panel + 0x28);
        if (!label)
            return false;
        void* klass = *reinterpret_cast<void**>(label);
        const void* method = il2cppClassGetMethod(klass, "set_text", 1);
        if (!method)
            return false;
        auto setText = reinterpret_cast<TmpSetTextFn>(
            il2cppMethodGetPointer ? il2cppMethodGetPointer(method)
                                   : *reinterpret_cast<void* const*>(method));
        const uintptr_t text =
            reinterpret_cast<uintptr_t>(il2cppStringNew(name));
        if (!setText || !text)
            return false;
        setText(label, text, reinterpret_cast<uintptr_t>(method));
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

void RememberCharacterInfo(void* self) {
    if (!self)
        return;
    g_characterInfoPanel.store(reinterpret_cast<uintptr_t>(self),
                               std::memory_order_release);
    if (g_cfg.spoofName && g_cfg.spoofNameValue[0]) {
        ApplyCharacterInfoName(reinterpret_cast<uintptr_t>(self),
                               g_cfg.spoofNameValue);
        g_nameRetries.store(180, std::memory_order_release);
    }
}

void __fastcall hkCharacterInfoInit(void* self, void* method) {
    if (oCharacterInfoInit) oCharacterInfoInit(self, method);
    RememberCharacterInfo(self);
}
void __fastcall hkCharacterInfoRefresh(void* self, void* method) {
    if (oCharacterInfoRefresh) oCharacterInfoRefresh(self, method);
    RememberCharacterInfo(self);
}
void __fastcall hkCharacterInfoClean(void* self, void* method) {
    if (oCharacterInfoClean) oCharacterInfoClean(self, method);
    if (g_characterInfoPanel.load() == reinterpret_cast<uintptr_t>(self))
        g_characterInfoPanel.store(0);
}
void __fastcall hkCharacterInfoDestroy(void* self, void* method) {
    if (oCharacterInfoDestroy) oCharacterInfoDestroy(self, method);
    if (g_characterInfoPanel.load() == reinterpret_cast<uintptr_t>(self))
        g_characterInfoPanel.store(0);
}

#define CHARACTER_PLAYER_HOOK(N) \
void __fastcall hkCharacterInfoPlayer##N(void* self, void* player, void* method) { \
    if (oCharacterInfoPlayer[N]) oCharacterInfoPlayer[N](self, player, method); \
    RememberCharacterInfo(self); \
}
CHARACTER_PLAYER_HOOK(0)
CHARACTER_PLAYER_HOOK(1)
CHARACTER_PLAYER_HOOK(2)
CHARACTER_PLAYER_HOOK(3)
CHARACTER_PLAYER_HOOK(4)
CHARACTER_PLAYER_HOOK(5)
#undef CHARACTER_PLAYER_HOOK


void WriteStatString(uintptr_t stat, const char* text) {
    const uintptr_t strObj = *reinterpret_cast<uintptr_t*>(stat + 0x18);
    if (strObj <= 0xFFFF || !text)
        return;
    __try {
        const int cap = *reinterpret_cast<int*>(strObj + 0x10);
        if (cap < 2 || cap > 24)
            return;
        int n = 0;
        while (text[n] && n < cap)
            ++n;
        for (int i = 0; i < n; ++i)
            *reinterpret_cast<uint16_t*>(strObj + 0x14 + 2ull * i) =
                static_cast<uint16_t>(static_cast<unsigned char>(text[i]));
        *reinterpret_cast<int*>(strObj + 0x10) = n;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}

void RewriteSelectedStats(uintptr_t packet) {
    if (!packet || (!g_cfg.stars && !g_cfg.accountFame &&
                    !g_cfg.spoofGuildName && !g_cfg.spoofGuildRank &&
                    !g_cfg.skinChanger && !g_cfg.dyeChanger &&
                    !g_cfg.accessoryDyeChanger))
        return;

    uintptr_t outerData = 0;
    int outerCount = 0;
    if (!ReadList(*reinterpret_cast<uintptr_t*>(packet + 0x28), outerData, outerCount))
        return;

    for (int i = 0; i < outerCount; ++i) {


        uintptr_t objectUpdate =
            *reinterpret_cast<uintptr_t*>(outerData + 0x20 + 8ull * i);
        if (!objectUpdate)
            continue;
        uintptr_t descriptor = *reinterpret_cast<uintptr_t*>(objectUpdate + 0x18);
        if (!descriptor)
            continue;
        uintptr_t stats = *reinterpret_cast<uintptr_t*>(descriptor + 0x20);
        if (!stats ||
            *reinterpret_cast<int*>(descriptor + 0x10) != g_selectedObjectId)
            continue;

        uintptr_t statData = 0;
        int statCount = 0;
        if (!ReadList(stats, statData, statCount))
            continue;
        for (int j = 0; j < statCount; ++j) {
            uintptr_t stat = *reinterpret_cast<uintptr_t*>(statData + 0x20 + 8ull * j);
            if (!stat)
                continue;
            const int type = *reinterpret_cast<int*>(stat + 0x10);
            if (type == 30 && g_cfg.stars) {
                *reinterpret_cast<int*>(stat + 0x14) = g_cfg.starsValue;
            } else if (type == 39 && g_cfg.accountFame) {
                *reinterpret_cast<int*>(stat + 0x14) =
                    static_cast<int>(g_cfg.accountFameValue);
            } else if (type == 63 && g_cfg.spoofGuildRank) {
                *reinterpret_cast<int*>(stat + 0x14) = g_cfg.guildRankValue;
            } else if (type == 62 && g_cfg.spoofGuildName &&
                       g_cfg.guildNameValue[0]) {
                WriteStatString(stat, g_cfg.guildNameValue);
            } else if (type == 25 && g_cfg.skinChanger && g_cfg.skinId) {
                *reinterpret_cast<int*>(stat + 0x14) = g_cfg.skinId;
            } else if (type == 32 && g_cfg.dyeChanger && g_cfg.dyeId) {
                *reinterpret_cast<int*>(stat + 0x14) = g_cfg.dyeId;
            } else if (type == 33 && g_cfg.accessoryDyeChanger &&
                       g_cfg.accessoryDyeId) {
                *reinterpret_cast<int*>(stat + 0x14) = g_cfg.accessoryDyeId;
            }
        }
    }
}

int64_t __fastcall hkStatsUpdate(uintptr_t self, uintptr_t packet) {
    if (packet) {
        uintptr_t descriptor = *reinterpret_cast<uintptr_t*>(packet);


        uintptr_t kindPtr = descriptor
            ? *reinterpret_cast<uintptr_t*>(descriptor + 0x2B8)
            : 0;
        uint8_t kind = kindPtr ? *reinterpret_cast<uint8_t*>(kindPtr + 1) : 0;
        if (kind == 92)
            g_selectedObjectId = -1;
        else if (kind == 101) {
            g_selectedObjectId = *reinterpret_cast<int*>(packet + 0x10);
            DBLOG("name spoof: selected object id=%d", g_selectedObjectId);
        } else if (kind == 42)
            RewriteSelectedStats(packet);
    }
    const int64_t result = oStatsUpdate ? oStatsUpdate(self, packet) : 0;
    return result;
}

void SetFamePatch(bool enable) {
    if (enable == g_patchEnabled)
        return;
    uint8_t* site = static_cast<uint8_t*>(ga::Rva(kFameWritePatchRva));
    if (!site)
        return;

    DWORD oldProtect = 0;
    if (!VirtualProtect(site, sizeof(g_patchBackup), PAGE_EXECUTE_READWRITE, &oldProtect))
        return;
    if (enable) {
        if (!g_havePatchBackup) {
            memcpy(g_patchBackup, site, sizeof(g_patchBackup));
            g_havePatchBackup = true;
        }
        memset(site, 0x90, sizeof(g_patchBackup));
    } else if (g_havePatchBackup) {
        memcpy(site, g_patchBackup, sizeof(g_patchBackup));
    }
    FlushInstructionCache(GetCurrentProcess(), site, sizeof(g_patchBackup));
    DWORD ignored = 0;
    VirtualProtect(site, sizeof(g_patchBackup), oldProtect, &ignored);
    g_patchEnabled = enable;
}

void DrawFpm(uintptr_t player) {
    static bool tracking = false;
    static int startFame = 0;
    static LARGE_INTEGER start{};
    static double fpm = 0.0;

    if (!g_cfg.showFpm || !player) {
        tracking = false;
        return;
    }

    int currentFame = *reinterpret_cast<int*>(player + 0x550);
    LARGE_INTEGER now{}, freq{};
    QueryPerformanceCounter(&now);
    QueryPerformanceFrequency(&freq);
    if (!tracking) {
        tracking = true;
        startFame = currentFame;
        start = now;
        fpm = 0.0;
    }

    double seconds = freq.QuadPart
        ? static_cast<double>(now.QuadPart - start.QuadPart) /
              static_cast<double>(freq.QuadPart)
        : 0.0;
    int gained = currentFame - startFame;
    if (seconds > 0.0 && gained != 0 && !g_cfg.fameValue)
        fpm = static_cast<double>(gained) / (seconds / 60.0);
    else
        fpm = 0.0;

    ImGui::SetNextWindowBgAlpha(0.35f);
    ImGui::Begin("##client_fpm", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
                 ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoSavedSettings);
    ImGui::Text("FPM: %.2f", fpm);
    ImGui::End();
}

}

void Install() {
    HMODULE game = GetModuleHandleA("GameAssembly.dll");
    il2cppDomainGet = reinterpret_cast<DomainGetFn>(
        GetProcAddress(game, "il2cpp_domain_get"));
    il2cppDomainAssemblyOpen = reinterpret_cast<DomainAssemblyOpenFn>(
        GetProcAddress(game, "il2cpp_domain_assembly_open"));
    il2cppAssemblyGetImage = reinterpret_cast<AssemblyGetImageFn>(
        GetProcAddress(game, "il2cpp_assembly_get_image"));
    il2cppClassFromName = reinterpret_cast<ClassFromNameFn>(
        GetProcAddress(game, "il2cpp_class_from_name"));
    il2cppClassGetMethod = reinterpret_cast<ClassGetMethodFn>(
        GetProcAddress(game, "il2cpp_class_get_method_from_name"));
    il2cppMethodGetPointer = reinterpret_cast<MethodGetPointerFn>(
        GetProcAddress(game, "il2cpp_method_get_pointer"));
    il2cppStringNew = reinterpret_cast<StringNewFn>(
        GetProcAddress(game, "il2cpp_string_new"));

    void* characterInfoClass = nullptr;
    if (il2cppDomainGet && il2cppDomainAssemblyOpen && il2cppAssemblyGetImage &&
        il2cppClassFromName) {
        void* domain = il2cppDomainGet();
        void* assembly = domain
            ? il2cppDomainAssemblyOpen(domain, "Assembly-CSharp")
            : nullptr;
        void* image = assembly ? il2cppAssemblyGetImage(assembly) : nullptr;
        characterInfoClass = image
            ? il2cppClassFromName(image, "DecaGames.RotMG.UI.GUI",
                                  "CharacterInfo")
            : nullptr;
    }

    void* methods[] = {
        MethodPointer(characterInfoClass, "Init", 0),
        MethodPointer(characterInfoClass, "PEGHFEJLCMK", 0),
        MethodPointer(characterInfoClass, "Clean", 0),
        MethodPointer(characterInfoClass, "OnDestroy", 0),
        MethodPointer(characterInfoClass, "ADIPBDPBBPN", 1),
        MethodPointer(characterInfoClass, "JCHCJBJCHME", 1),
        MethodPointer(characterInfoClass, "EJBDMCCOPIB", 1),
        MethodPointer(characterInfoClass, "APKPBHMODIP", 1),
        MethodPointer(characterInfoClass, "FAHGPHDNBFE", 1),
        MethodPointer(characterInfoClass, "ALENMBMNCLO", 1),
    };
    void* hooks[] = {
        reinterpret_cast<void*>(&hkCharacterInfoInit),
        reinterpret_cast<void*>(&hkCharacterInfoRefresh),
        reinterpret_cast<void*>(&hkCharacterInfoClean),
        reinterpret_cast<void*>(&hkCharacterInfoDestroy),
        reinterpret_cast<void*>(&hkCharacterInfoPlayer0),
        reinterpret_cast<void*>(&hkCharacterInfoPlayer1),
        reinterpret_cast<void*>(&hkCharacterInfoPlayer2),
        reinterpret_cast<void*>(&hkCharacterInfoPlayer3),
        reinterpret_cast<void*>(&hkCharacterInfoPlayer4),
        reinterpret_cast<void*>(&hkCharacterInfoPlayer5),
    };
    void** originals[] = {
        reinterpret_cast<void**>(&oCharacterInfoInit),
        reinterpret_cast<void**>(&oCharacterInfoRefresh),
        reinterpret_cast<void**>(&oCharacterInfoClean),
        reinterpret_cast<void**>(&oCharacterInfoDestroy),
        reinterpret_cast<void**>(&oCharacterInfoPlayer[0]),
        reinterpret_cast<void**>(&oCharacterInfoPlayer[1]),
        reinterpret_cast<void**>(&oCharacterInfoPlayer[2]),
        reinterpret_cast<void**>(&oCharacterInfoPlayer[3]),
        reinterpret_cast<void**>(&oCharacterInfoPlayer[4]),
        reinterpret_cast<void**>(&oCharacterInfoPlayer[5]),
    };
    int characterHooks = 0;
    for (size_t i = 0; i < _countof(methods); ++i) {
        if (methods[i] && MH_CreateHook(methods[i], hooks[i], originals[i]) == MH_OK)
            ++characterHooks;
    }
    DBLOG("name changer: CharacterInfo class=%p hooks=%d string_new=%p",
          characterInfoClass, characterHooks, (void*)il2cppStringNew);

    void* target = ga::Rva(kStatsUpdateRva);
    DBLOG("fame::Install stats target=%p (GA+0x%llX)", target,
          (unsigned long long)kStatsUpdateRva);
    if (target) {
        const MH_STATUS st =
            MH_CreateHook(target, reinterpret_cast<void*>(&hkStatsUpdate),
                          reinterpret_cast<void**>(&oStatsUpdate));
        DBLOG("fame::Install MH_CreateHook=%d orig=%p", (int)st,
              (void*)oStatsUpdate);
    }
}

void Tick() {
    uintptr_t player = game::Player();
    SetFamePatch(g_cfg.fameValue);

    if (player && g_cfg.fameValue) {
        int value = static_cast<int>(g_cfg.fameValueAmount);
        *reinterpret_cast<int*>(player + 0x550) = value;
        *reinterpret_cast<int*>(player + 0x548) = value;
    }

    DrawFpm(player);
}

void Poll() {
    static bool lastEnabled = false;
    if (!g_cfg.spoofName || !g_cfg.spoofNameValue[0]) {
        lastEnabled = false;
        g_appliedName[0] = '\0';
        return;
    }
    if (!lastEnabled || std::strcmp(g_appliedName, g_cfg.spoofNameValue) != 0) {
        strncpy_s(g_appliedName, g_cfg.spoofNameValue, _TRUNCATE);
        g_nameRetries.store(180, std::memory_order_release);
        lastEnabled = true;
    }
    int retries = g_nameRetries.load(std::memory_order_acquire);
    if (retries <= 0)
        return;
    const uintptr_t panel =
        g_characterInfoPanel.load(std::memory_order_acquire);
    if (ApplyCharacterInfoName(panel, g_appliedName)) {
        g_nameRetries.fetch_sub(1, std::memory_order_acq_rel);
        static bool logged = false;
        if (!logged) {
            logged = true;
            DBLOG("name changer: applied '%s' panel=%p",
                  g_appliedName, (void*)panel);
        }
    } else {
        g_nameRetries.fetch_sub(1, std::memory_order_acq_rel);
    }
}

}
