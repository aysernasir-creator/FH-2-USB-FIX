// SPDX-License-Identifier: MIT
// Stand-in for azimuth.exe used to test the injector: maps the game image and waits.
#include <windows.h>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>
#include <algorithm>
static uint8_t* mapImage(const wchar_t* path) {
    HANDLE f = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (f == INVALID_HANDLE_VALUE) return nullptr;
    DWORD size = GetFileSize(f, nullptr);
    std::vector<uint8_t> file(size);
    DWORD got; ReadFile(f, file.data(), size, &got, nullptr); CloseHandle(f);
    auto dos = (IMAGE_DOS_HEADER*)file.data();
    auto nt = (IMAGE_NT_HEADERS64*)(file.data() + dos->e_lfanew);
    uint8_t* base = (uint8_t*)VirtualAlloc(nullptr, nt->OptionalHeader.SizeOfImage, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (!base) return nullptr;
    memcpy(base, file.data(), nt->OptionalHeader.SizeOfHeaders);
    auto sec = IMAGE_FIRST_SECTION(nt);
    for (int i = 0; i < nt->FileHeader.NumberOfSections; ++i)
        if (sec[i].SizeOfRawData)
            memcpy(base + sec[i].VirtualAddress, file.data() + sec[i].PointerToRawData,
                   std::min(sec[i].SizeOfRawData, sec[i].Misc.VirtualSize ? sec[i].Misc.VirtualSize : sec[i].SizeOfRawData));
    // relocations
    uint64_t delta = (uint64_t)base - nt->OptionalHeader.ImageBase;
    auto& rd = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC];
    uint8_t* r = base + rd.VirtualAddress; uint8_t* rend = r + rd.Size;
    while (r < rend) {
        auto blk = (IMAGE_BASE_RELOCATION*)r;
        if (!blk->SizeOfBlock) break;
        auto ent = (WORD*)(blk + 1);
        int n = (blk->SizeOfBlock - sizeof(*blk)) / 2;
        for (int i = 0; i < n; ++i)
            if ((ent[i] >> 12) == IMAGE_REL_BASED_DIR64)
                *(uint64_t*)(base + blk->VirtualAddress + (ent[i] & 0xFFF)) += delta;
        r += blk->SizeOfBlock;
    }
    return base;
}


int wmain(int argc, wchar_t** argv) {
    uint8_t* base = mapImage(argv[1]);
    uint8_t orig[5]; memcpy(orig, base + 0x103832A, 5);
    printf("fake azimuth: image at %p, waiting for injection\n", base); fflush(stdout);
    for (int i = 0; i < 200; ++i) {
        Sleep(100);
        if (memcmp(orig, base + 0x103832A, 5) != 0) { printf("fake azimuth: PATCHED after %d ms\n", i * 100); return 0; }
    }
    printf("fake azimuth: not patched\n"); return 1;
}
