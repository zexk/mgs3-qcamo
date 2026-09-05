// Copied from bbtracker (../bbtracker/src/common/mem.h), MIT licensed.
#pragma once

#include <windows.h>
#include <cstdint>
#include <cstring>

namespace qcamo::mem {

inline bool range_readable(uintptr_t addr, size_t len)
{
    const uintptr_t end = addr + len;
    while (addr < end) {
        MEMORY_BASIC_INFORMATION mbi{};
        if (!VirtualQuery(reinterpret_cast<LPCVOID>(addr), &mbi, sizeof(mbi))) {
            return false;
        }
        if (mbi.State != MEM_COMMIT) {
            return false;
        }
        constexpr DWORD kReadable = PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY
            | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;
        if ((mbi.Protect & kReadable) == 0 || (mbi.Protect & PAGE_GUARD) != 0) {
            return false;
        }
        const uintptr_t region_end = reinterpret_cast<uintptr_t>(mbi.BaseAddress) + mbi.RegionSize;
        addr = region_end;
    }
    return true;
}

template <typename T>
T read(uintptr_t address)
{
    T value{};
    std::memcpy(&value, reinterpret_cast<const void*>(address), sizeof(value));
    return value;
}

template <typename T>
void write(uintptr_t address, T value)
{
    std::memcpy(reinterpret_cast<void*>(address), &value, sizeof(value));
}

} // namespace qcamo::mem
