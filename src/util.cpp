#include "util.h"
#include <windows.h>
#include <cstring>

namespace util {
    bool Patch(void* addr, const void* bytes, size_t len) {
        if (!addr || !bytes || !len || len > 256) return false;

        DWORD old = 0;

        if (!VirtualProtect(addr, 256, PAGE_EXECUTE_READWRITE, &old))
            return false;

        memcpy(addr, bytes, len);
        FlushInstructionCache(GetCurrentProcess(), addr, len);

        DWORD ignored = 0;
        return VirtualProtect(addr, 256, old, &ignored) != FALSE;
    }
}
