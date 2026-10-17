#pragma once
// ============================================================================
//  C4pt0r Cheats — Memory Utilities
//  Pattern scanner, read/write helpers, pointer chain resolution
// ============================================================================

#include <Windows.h>
#include <Psapi.h>
#include <cstdint>
#include <vector>
#include <string>

#pragma comment(lib, "psapi.lib")

namespace C4pt0r::Memory
{
    // ---- Module helpers -------------------------------------------------
    inline uintptr_t GetModuleBase(const wchar_t* name = nullptr) {
        return reinterpret_cast<uintptr_t>(GetModuleHandleW(name));
    }

    inline size_t GetModuleSize(const wchar_t* name = nullptr) {
        HMODULE h = GetModuleHandleW(name);
        if (!h) return 0;
        MODULEINFO mi{};
        GetModuleInformation(GetCurrentProcess(), h, &mi, sizeof(mi));
        return mi.SizeOfImage;
    }

    // ---- AOB pattern scan -----------------------------------------------
    // Pattern string: "48 8B 05 ?? ?? ?? ?? 48 05"
    // '?' or '??' = wildcard byte
    inline uintptr_t PatternScan(uintptr_t start, size_t size, const char* pattern) {
        std::vector<int> bytes;
        for (const char* p = pattern; *p; ) {
            if (*p == ' ') { ++p; continue; }
            if (*p == '?') {
                bytes.push_back(-1);
                ++p;
                if (*p == '?') ++p;
                continue;
            }
            char hex[3] = { p[0], p[1], 0 };
            bytes.push_back(static_cast<int>(strtol(hex, nullptr, 16)));
            p += 2;
        }

        const auto* data = reinterpret_cast<const uint8_t*>(start);
        const size_t pLen = bytes.size();

        for (size_t i = 0; i + pLen <= size; ++i) {
            bool found = true;
            for (size_t j = 0; j < pLen; ++j) {
                if (bytes[j] != -1 && data[i + j] != static_cast<uint8_t>(bytes[j])) {
                    found = false;
                    break;
                }
            }
            if (found) return start + i;
        }
        return 0;
    }

    inline uintptr_t PatternScanModule(const wchar_t* moduleName, const char* pattern) {
        uintptr_t base = GetModuleBase(moduleName);
        size_t    size = GetModuleSize(moduleName);
        if (!base || !size) return 0;

        // Walk committed+readable pages only — a raw scan over SizeOfImage
        // crosses guard/noaccess regions and AVs mid-scan
        uintptr_t addr = base;
        const uintptr_t end = base + size;
        while (addr < end) {
            MEMORY_BASIC_INFORMATION mbi{};
            if (!VirtualQuery(reinterpret_cast<void*>(addr), &mbi, sizeof(mbi)) ||
                !mbi.RegionSize) break;

            uintptr_t regionEnd = reinterpret_cast<uintptr_t>(mbi.BaseAddress) + mbi.RegionSize;
            if (mbi.State == MEM_COMMIT &&
                !(mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD))) {
                uintptr_t scanEnd = regionEnd > end ? end : regionEnd;
                uintptr_t hit = PatternScan(addr, scanEnd - addr, pattern);
                if (hit) return hit;
            }
            addr = regionEnd;
        }
        return 0;
    }

    // ---- Executable check — validate a resolved pointer before calling it -
    inline bool IsExecutable(uintptr_t addr) {
        if (!addr) return false;
        MEMORY_BASIC_INFORMATION mbi{};
        if (!VirtualQuery(reinterpret_cast<void*>(addr), &mbi, sizeof(mbi))) return false;
        constexpr DWORD execMask = PAGE_EXECUTE | PAGE_EXECUTE_READ |
                                   PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;
        return mbi.State == MEM_COMMIT && (mbi.Protect & execMask) != 0;
    }

    // ---- Readable check — validate a pointer before dereferencing a string -
    inline bool IsReadable(uintptr_t addr) {
        if (!addr) return false;
        MEMORY_BASIC_INFORMATION mbi{};
        if (!VirtualQuery(reinterpret_cast<void*>(addr), &mbi, sizeof(mbi))) return false;
        return mbi.State == MEM_COMMIT && (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD)) == 0;
    }

    // ---- RIP-relative address resolution --------------------------------
    // addr         = address of the instruction containing the offset
    // ripOffset    = byte position of the 4-byte relative within the instruction
    // instrLength  = total instruction length
    inline uintptr_t ResolveRIP(uintptr_t addr, int ripOffset, int instrLength) {
        auto rel = *reinterpret_cast<int32_t*>(addr + ripOffset);
        return addr + instrLength + rel;
    }

    // ---- Safe read / write ----------------------------------------------
    template <typename T>
    inline T Read(uintptr_t addr) {
        if (IsBadReadPtr(reinterpret_cast<void*>(addr), sizeof(T))) return T{};
        return *reinterpret_cast<T*>(addr);
    }

    template <typename T>
    inline void Write(uintptr_t addr, T value) {
        DWORD old;
        VirtualProtect(reinterpret_cast<void*>(addr), sizeof(T), PAGE_EXECUTE_READWRITE, &old);
        *reinterpret_cast<T*>(addr) = value;
        VirtualProtect(reinterpret_cast<void*>(addr), sizeof(T), old, &old);
    }

    // ---- Pointer chain --------------------------------------------------
    // ReadPointerChain(base, { 0x10, 0x20, 0x30 })
    // reads [[[base]+0x10]+0x20]+0x30
    inline uintptr_t ReadPointerChain(uintptr_t base, const std::vector<uintptr_t>& offsets) {
        uintptr_t addr = base;
        for (size_t i = 0; i < offsets.size(); ++i) {
            if (IsBadReadPtr(reinterpret_cast<void*>(addr), sizeof(uintptr_t))) return 0;
            addr = *reinterpret_cast<uintptr_t*>(addr);
            if (!addr) return 0;
            addr += offsets[i];
        }
        return addr;
    }

    // ---- NOP a region ---------------------------------------------------
    inline void Nop(uintptr_t addr, size_t size) {
        DWORD old;
        VirtualProtect(reinterpret_cast<void*>(addr), size, PAGE_EXECUTE_READWRITE, &old);
        memset(reinterpret_cast<void*>(addr), 0x90, size);
        VirtualProtect(reinterpret_cast<void*>(addr), size, old, &old);
    }

    // ---- Patch bytes ----------------------------------------------------
    inline void Patch(uintptr_t addr, const uint8_t* data, size_t size) {
        DWORD old;
        VirtualProtect(reinterpret_cast<void*>(addr), size, PAGE_EXECUTE_READWRITE, &old);
        memcpy(reinterpret_cast<void*>(addr), data, size);
        VirtualProtect(reinterpret_cast<void*>(addr), size, old, &old);
    }
}
