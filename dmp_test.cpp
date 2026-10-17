#include <cstdio>
#include <cstdint>
#include <cstring>
#include <vector>
#include <string>
#include <Windows.h>

struct DirEnt { uint32_t type, size, rva; };
struct Mod { uint64_t base; uint32_t size; std::wstring name; };

int main(int argc, char** argv) {
    if (argc < 2) return 1;
    FILE* f = nullptr;
    fopen_s(&f, argv[1], "rb");
    if (!f) { printf("open fail\n"); return 1; }
    fseek(f, 0, SEEK_END); long flen = ftell(f); fseek(f, 0, SEEK_SET);
    std::vector<uint8_t> d(flen);
    fread(d.data(), 1, flen, f); fclose(f);
    const uint8_t* p = d.data();
    const uint8_t* end = p + flen;
    auto ok = [&](const void* q, size_t n) { return q >= p && (const uint8_t*)q + n <= end; };
    if (flen < 32 || memcmp(p, "MDMP", 4)) { printf("not mdmp\n"); return 1; }
    uint32_t nstreams = *(uint32_t*)(p + 8);
    uint32_t dir = *(uint32_t*)(p + 12);
    const DirEnt* de = (const DirEnt*)(p + dir);
    uint32_t excRva = 0, modRva = 0, memRva = 0;
    for (uint32_t i = 0; i < nstreams && ok(&de[i], 12); ++i) {
        if (de[i].type == 4) modRva = de[i].rva;
        if (de[i].type == 5) memRva = de[i].rva;
        if (de[i].type == 6) excRva = de[i].rva;
    }

    std::vector<Mod> mods;
    if (ok(p + modRva, 4)) {
        uint32_t nmod = *(uint32_t*)(p + modRva);
        const uint8_t* mp = p + modRva + 4;
        for (uint32_t i = 0; i < nmod && ok(mp, 108); ++i) {
            Mod m; m.base = *(uint64_t*)mp; m.size = *(uint32_t*)(mp + 8);
            uint32_t nRva = *(uint32_t*)(mp + 20);
            if (ok(p + nRva, 4)) {
                uint32_t nlen = *(uint32_t*)(p + nRva);
                if (ok(p + nRva + 4, nlen) && nlen < 4096)
                    m.name.assign((const wchar_t*)(p + nRva + 4), nlen / 2);
            }
            mods.push_back(m); mp += 108;
        }
    }
    auto inMod = [&](uint64_t a) -> const Mod* {
        for (auto& m : mods) if (a >= m.base && a < m.base + m.size) return &m;
        return nullptr;
    };
    auto nm = [](const Mod* m) {
        std::wstring w = m->name; size_t s = w.find_last_of(L"\\/");
        return s != std::wstring::npos ? w.c_str() + s + 1 : w.c_str();
    };

    if (!ok(p + excRva, 168)) { printf("no exc stream\n"); return 1; }
    const uint8_t* ep = p + excRva;
    uint32_t code = *(uint32_t*)(ep + 8);
    uint64_t eaddr = *(uint64_t*)(ep + 24);
    uint32_t ctxRva = *(uint32_t*)(ep + 164);
    uint32_t nparams = *(uint32_t*)(ep + 32);
    printf("code=0x%08X addr=0x%llX ctxRva=0x%X nparams=%u\n", code,
           (unsigned long long)eaddr, ctxRva, nparams);
    for (uint32_t i = 0; i < nparams && i < 4; ++i)
        printf("  param%u = 0x%llX\n", i,
               (unsigned long long)*(uint64_t*)(ep + 40 + i * 8));
    if (!ok(p + ctxRva, 0x100)) { printf("bad ctx\n"); return 1; }
    const uint8_t* ctx = p + ctxRva;
    uint64_t rip = *(uint64_t*)(ctx + 0xF8);
    uint64_t rsp = *(uint64_t*)(ctx + 0x98);
    printf("rip=0x%llX rsp=0x%llX\n",
           (unsigned long long)rip, (unsigned long long)rsp);
    if (const Mod* m = inMod(rip))
        printf("RIP -> %ls +0x%llX\n", nm(m), (unsigned long long)(rip - m->base));

    if (!ok(p + memRva, 4)) { printf("no mem list\n"); return 1; }
    uint32_t nmem = *(uint32_t*)(p + memRva);
    const uint8_t* mm = p + memRva + 4;
    for (uint32_t i = 0; i < nmem && ok(mm, 16); ++i, mm += 16) {
        uint64_t st = *(uint64_t*)mm;
        uint32_t sz = *(uint32_t*)(mm + 8);
        uint32_t rva = *(uint32_t*)(mm + 12);
        if (rsp >= st && rsp < st + sz && ok(p + rva, sz)) {
            const uint64_t* sp = (const uint64_t*)(p + rva + (rsp - st));
            size_t words = (sz - (rsp - st)) / 8;
            int found = 0;
            for (size_t w = 0; w < words && found < 24; ++w) {
                if (const Mod* m = inMod(sp[w])) {
                    printf("  [rsp+0x%04zX] %ls +0x%llX\n", w * 8, nm(m),
                           (unsigned long long)(sp[w] - m->base));
                    ++found;
                }
            }
            break;
        }
    }
    return 0;
}
