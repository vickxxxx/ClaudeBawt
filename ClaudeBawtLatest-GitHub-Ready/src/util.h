


#pragma once
#include <cstdint>
#include <cstddef>

namespace util {


    bool Patch(void* addr, const void* bytes, size_t len);

    template <typename T>
    bool Write(void* addr, T value) { return Patch(addr, &value, sizeof(T)); }

    template <typename T>
    T Read(void* addr) { return *reinterpret_cast<T*>(addr); }
}
