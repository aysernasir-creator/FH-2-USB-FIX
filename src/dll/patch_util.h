// SPDX-License-Identifier: MIT
// Shared helpers for patching the game image (internal).
#pragma once
#include <windows.h>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace fh2 {

// Allocates RWX memory within rel32 range of `base`; nullptr on failure.
void* allocNear(uintptr_t base, size_t size);

// Suspends every other thread of this process while code is rewritten.
struct Suspended {
    std::vector<HANDLE> handles;
    void suspendOthers();
    void resumeAll();
};

// Redirects the `E8 rel32` call at `callSite` to `target` through a stub written
// to `thunkMem` (`mov rax, imm64; jmp rax`, 12 bytes; must be RWX and within
// rel32 range of the call site). Returns false (nothing changed) if the stub is out of range
// or the call site cannot be made writable.
// With `passRsiAsR8` the stub first runs `mov r8, rsi` (15 bytes), handing the caller's rsi to the
// target as its third argument.
bool patchCallRel32(uintptr_t callSite, void* target, void* thunkMem, bool passRsiAsR8 = false);

// True if [p, p+n) is committed, readable and not a guard page. Caches the last region per thread
// (for a short time) to stay cheap on hot paths.
bool readable(const void* p, size_t n);

} // namespace fh2
