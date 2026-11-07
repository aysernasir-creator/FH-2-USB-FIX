// SPDX-License-Identifier: MIT
// Redirects the game's head-tracking camera inputs to the tracker.
#include <windows.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>
#include "hook.h"
#include "patch_util.h"
#include "wheel_hook.h"
#include "fov_hook.h"
#include "cockpit_hook.h"
#include "assist_hook.h"
#include "drawdist_hook.h"
#include "game_layout.h"
#include "tracker.h"
#include "log.h"

namespace fh2 {

namespace {

// The case block is identified by an FNV-1a hash rather than by embedding the
// game's bytes. The deep scan pre-filters candidates with an opcode skeleton
// (displacements wildcarded) before hashing.
constexpr uint64_t kCaseBlockFnv = 0x94F0E52A52E2CD1Bull;
const int kSkeleton[] = {0x48, 0x8B, 0x0D, -1, -1, -1, -1, 0x48, 0x8D, 0x54, 0x24, 0x40, 0xE8};

uint64_t fnv1a(const uint8_t* p, size_t n) {
    uint64_t h = 0xCBF29CE484222325ull;
    for (size_t i = 0; i < n; ++i) { h ^= p[i]; h *= 0x100000001B3ull; }
    return h;
}

bool skeletonAt(const uint8_t* p) {
    for (size_t i = 0; i < sizeof(kSkeleton) / sizeof(kSkeleton[0]); ++i)
        if (kSkeleton[i] >= 0 && p[i] != kSkeleton[i]) return false;
    return true;
}

bool g_cockpitOnly = true;

// ---------------------------------------------------------------------------
// Layer bookkeeping (game thread). Fixed-size table, no allocation.
// ---------------------------------------------------------------------------
enum LayerClass : uint8_t { kNone, kRotCockpit, kRotOther, kPosCockpit, kPosOther };

struct LayerEntry {
    uint8_t*   layer;
    float      written[4];
    LayerClass cls;
    ULONGLONG  seen;   // GetTickCount64 of the last lookup, for pruning
};

constexpr int kTableSize = 1024;
LayerEntry        g_table[kTableSize];
std::atomic_flag  g_tableLock = ATOMIC_FLAG_INIT;
int               g_tableCount = 0;

template <class T> T& at(uint8_t* p, int off) { return *reinterpret_cast<T*>(p + off); }

const char* className(LayerClass c) {
    switch (c) {
        case kRotCockpit: return "rotation (cockpit)";
        case kRotOther:   return "rotation (hood/bumper)";
        case kPosCockpit: return "position (cockpit)";
        case kPosOther:   return "position (bumper)";
        default:          return "unused";
    }
}

LayerClass classify(uint8_t* L) {
    int in  = at<int>(L, layer::kInputType);
    int out = at<int>(L, layer::kOutputType);
    float px = at<float>(L, layer::kParam);
    bool ypr = out == CarSpaceYPROffset || out == CameraSpaceYPROffset;
    bool xyz = out == CarSpaceXYZOffset || out == CameraSpaceXYZOffset;
    if (in == HeadTrackingYaw && ypr)
        return std::fabs(px - kCockpitYawParam) < 0.01f ? kRotCockpit : kRotOther;
    if (in == HeadTrackingX && xyz)
        return std::fabs(px - kCockpitXParam) < 0.0001f ? kPosCockpit : kPosOther;
    return kNone;
}

// Open-addressing probe shared by lookup, rememberWritten and prune: the entry holding L, else
// the first empty slot on its chain (nullptr if the table has no empty slot).
LayerEntry* probeLayer(uint8_t* L) {
    size_t h = (reinterpret_cast<uintptr_t>(L) >> 4) * 0x9E3779B97F4A7C15ull;
    int idx = int(h >> 54) & (kTableSize - 1);
    for (int probe = 0; probe < kTableSize; ++probe) {
        LayerEntry& c = g_table[(idx + probe) & (kTableSize - 1)];
        if (c.layer == L || c.layer == nullptr) return &c;
    }
    return nullptr;
}

// Drops layers not looked up for 5 s (camera layers re-created at new addresses). Caller holds the lock.
void pruneTable(ULONGLONG now) {
    static LayerEntry keep[kTableSize];   // static: too big for the game thread's stack
    int before = g_tableCount, kept = 0;
    for (const LayerEntry& c : g_table)
        if (c.layer && now - c.seen <= 5000) keep[kept++] = c;
    std::memset(g_table, 0, sizeof(g_table));
    g_tableCount = 0;
    for (int i = 0; i < kept; ++i) { *probeLayer(keep[i].layer) = keep[i]; ++g_tableCount; }
    LOG("layers: table pruned, %d live of %d", kept, before);
}

// Returns a copy of the entry for this layer, creating/refreshing it as needed.
LayerClass lookupLayer(uint8_t* L, bool& isNew) {
    isNew = false;
    while (g_tableLock.test_and_set(std::memory_order_acquire)) {}
    ULONGLONG now = GetTickCount64();
    LayerEntry* e = probeLayer(L);
    if (e && e->layer == nullptr) {   // not present: insert, pruning dead layers first if over 3/4
        if (g_tableCount >= kTableSize * 3 / 4) {
            pruneTable(now);
            e = probeLayer(L);
        }
        if (e && e->layer == nullptr && g_tableCount < kTableSize * 3 / 4) {
            e->layer = L; ++g_tableCount; isNew = true;
        } else if (e && e->layer == nullptr) {
            e = nullptr;   // all entries live: fall back below
        }
    }
    LayerClass cls = kNone;
    if (e) {
        e->seen = now;
        float* param = &at<float>(L, layer::kParam);
        // A layer we have not written, or one whose Param no longer holds what we
        // wrote (camera data reloaded at the same address), is fingerprinted afresh.
        if (isNew || std::memcmp(param, e->written, sizeof(e->written)) != 0) {
            std::memcpy(e->written, param, sizeof(e->written));
            e->cls = classify(L);
            isNew = true;
        }
        cls = e->cls;
    } else {
        cls = classify(L);   // table full: still works, just without re-fingerprinting
    }
    g_tableLock.clear(std::memory_order_release);
    return cls;
}

void rememberWritten(uint8_t* L) {
    while (g_tableLock.test_and_set(std::memory_order_acquire)) {}
    LayerEntry* c = probeLayer(L);
    if (c && c->layer == L) std::memcpy(c->written, &at<float>(L, layer::kParam), sizeof(c->written));
    g_tableLock.clear(std::memory_order_release);
}

bool layerActive(LayerClass c) {
    if (c == kNone) return false;
    if (g_cockpitOnly) return c == kRotCockpit || c == kPosCockpit;
    return true;
}

// Computes the scalar input for one head-tracking layer evaluation.
// For an active layer the per-frame offset vector is written into the layer's
// Param, its input is forced to 1.0 and its mapping to Linear, so the game's
// output (input * Param, then its spring) is exactly our offset.
float evaluate(uint8_t* L) {
    bool isNew;
    LayerClass cls = lookupLayer(L, isNew);
    if (isNew) {
        LOG("layer %p input %d output %d Param.x %.3f -> %s", (void*)L, at<int>(L, layer::kInputType),
            at<int>(L, layer::kOutputType), at<float>(L, layer::kParam), className(cls));
    }
    if (!layerActive(cls)) return 0.0f;

    // Guard against applying the offset twice in one frame: if a *different* layer of
    // the same class was driven within the last 3 ms (same frame), this one is neutral.
    {
        static uint8_t* lastLayer[5] = {};
        static int64_t  lastTick[5]  = {};
        static bool     warned = false;
        static const int64_t window = [] { LARGE_INTEGER f; QueryPerformanceFrequency(&f); return f.QuadPart * 3 / 1000; }();
        LARGE_INTEGER now; QueryPerformanceCounter(&now);
        if (lastLayer[cls] && lastLayer[cls] != L && now.QuadPart - lastTick[cls] < window) {
            if (!warned) {
                LOG("note: two %s layers evaluated in one frame (%p, %p); driving only the first",
                    className(cls), (void*)lastLayer[cls], (void*)L);
                warned = true;
            }
            return 0.0f;
        }
        lastLayer[cls] = L;
        lastTick[cls] = now.QuadPart;
    }

    CameraOffsets o;
    bool ok = Tracker::read(o);
    float* param = &at<float>(L, layer::kParam);
    const float* src = (cls == kRotCockpit || cls == kRotOther) ? o.rotDeg : o.posM;
    for (int i = 0; i < 3; ++i) param[i] = ok ? src[i] : 0.0f;
    at<float>(L, layer::kClampMin) = -2.0f;
    at<float>(L, layer::kClampMax) = 2.0f;
    at<uint8_t>(L, layer::kInputAbs) = 0;
    at<int>(L, layer::kMapping) = kMappingLinear;
    rememberWritten(L);
    return ok ? 1.0f : 0.0f;
}

// ---------------------------------------------------------------------------
// Hook entry points. Reached from a per-site thunk:
//   mov r8d, axis ; mov r9, rsi (layer) ; mov rax, hook ; jmp rax
// with the original call's return address on the stack, so they return
// straight into the game's case code, which reads out[axis].
// ---------------------------------------------------------------------------
extern "C" float* fh2HookPosition(void*, float* out, int axis, uint8_t* layer) {
    out[0] = out[1] = out[2] = out[3] = 0.0f;
    out[axis & 3] = evaluate(layer);
    return out;
}

extern "C" float* fh2HookRotation(void*, float* out, int axis, uint8_t* layer) {
    out[0] = out[1] = out[2] = out[3] = 0.0f;
    // The case code multiplies by 180/pi; hand it radians so the input is 1.0 or 0.0.
    out[axis & 3] = evaluate(layer) * 0.017453292519943295f;
    return out;
}

// ---------------------------------------------------------------------------
// Finding the game image
// ---------------------------------------------------------------------------
bool readMem(uintptr_t addr, void* dst, size_t n) {
    SIZE_T got = 0;
    return ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<void*>(addr), dst, n, &got) && got == n;
}

bool sigAt(uintptr_t base) {
    uint8_t buf[kCaseBlockSize];
    return readMem(base + kCaseBlockRva, buf, sizeof(buf)) && fnv1a(buf, sizeof(buf)) == kCaseBlockFnv;
}

bool readableExec(const MEMORY_BASIC_INFORMATION& m) {
    if (m.State != MEM_COMMIT || (m.Protect & (PAGE_GUARD | PAGE_NOACCESS))) return false;
    return (m.Protect & (PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY)) != 0;
}

uintptr_t findGameBase(bool deepScan) {
    SYSTEM_INFO si; GetSystemInfo(&si);
    uintptr_t p = reinterpret_cast<uintptr_t>(si.lpMinimumApplicationAddress);
    uintptr_t end = reinterpret_cast<uintptr_t>(si.lpMaximumApplicationAddress);
    uintptr_t lastAlloc = 0;
    std::vector<uint8_t> chunk;
    while (p < end) {
        MEMORY_BASIC_INFORMATION m;
        if (!VirtualQuery(reinterpret_cast<void*>(p), &m, sizeof(m))) break;
        uintptr_t regionEnd = reinterpret_cast<uintptr_t>(m.BaseAddress) + m.RegionSize;
        uintptr_t ab = reinterpret_cast<uintptr_t>(m.AllocationBase);
        if (m.State == MEM_COMMIT && ab && ab != lastAlloc) {
            lastAlloc = ab;
            // Fast path: image laid out from its allocation base (with or without headers).
            if (sigAt(ab)) return ab;
            if (ab >= 0x1000 && sigAt(ab - 0x1000)) return ab - 0x1000;
        }
        if (deepScan && readableExec(m) && m.RegionSize >= kCaseBlockSize) {
            uintptr_t rb = reinterpret_cast<uintptr_t>(m.BaseAddress);
            const size_t step = 4 << 20;
            for (uintptr_t q = rb; q < regionEnd; q += step) {
                size_t n = std::min<size_t>(step + kCaseBlockSize, regionEnd - q);
                chunk.resize(n);
                if (!readMem(q, chunk.data(), n)) continue;
                for (size_t i = 0; i + kCaseBlockSize <= n; ++i) {
                    if (chunk[i] == 0x48 && skeletonAt(&chunk[i]) &&
                        fnv1a(&chunk[i], kCaseBlockSize) == kCaseBlockFnv) {
                        uintptr_t found = q + i - kCaseBlockRva;
                        LOG("game image located by deep scan");
                        return found;
                    }
                }
            }
        }
        p = regionEnd;
    }
    return 0;
}

bool verify(uintptr_t base) {
    for (const auto& s : kSites) {
        uint32_t entry;
        if (!readMem(base + kJumpTableRva + 4 * s.inputType, &entry, 4)) { LOG("verify: jump table unreadable"); return false; }
        if (entry != s.caseRva) {
            LOG("verify: jump table case %d = %08x, expected %08x", s.inputType, entry, s.caseRva);
            return false;
        }
    }
    for (const auto& s : kSites) {
        uint8_t ins[5];
        if (!readMem(base + s.rva, ins, 5) || ins[0] != 0xE8) { LOG("verify: site %08x is not a call", s.rva); return false; }
        int32_t rel; std::memcpy(&rel, ins + 1, 4);
        uintptr_t target = base + s.rva + 5 + rel;
        if (target != base + s.calleeRva) {
            LOG("verify: site %08x calls %p, expected %p (already patched by something else?)",
                s.rva, (void*)target, (void*)(base + s.calleeRva));
            return false;
        }
    }
    return true;
}

uint8_t* emitThunk(uint8_t* p, int axis, void* fn) {
    // mov r8d, imm32
    *p++ = 0x41; *p++ = 0xB8; std::memcpy(p, &axis, 4); p += 4;
    // mov r9, rsi
    *p++ = 0x49; *p++ = 0x89; *p++ = 0xF1;
    // mov rax, imm64
    *p++ = 0x48; *p++ = 0xB8; uint64_t a = reinterpret_cast<uint64_t>(fn); std::memcpy(p, &a, 8); p += 8;
    // jmp rax
    *p++ = 0xFF; *p++ = 0xE0;
    // pad to 32 bytes with int3
    while (reinterpret_cast<uintptr_t>(p) & 31) *p++ = 0xCC;
    return p;
}

bool patch(uintptr_t base) {
    uint8_t* thunks = static_cast<uint8_t*>(allocNear(base + kCaseBlockRva, 4096));
    if (!thunks) { LOG("patch: could not allocate thunk memory near the game image"); return false; }
    uint8_t* p = thunks;
    int32_t rels[5];
    for (int i = 0; i < 5; ++i) {
        const CallSite& s = kSites[i];
        uint8_t* t = p;
        p = emitThunk(p, s.axis, s.rotation ? (void*)&fh2HookRotation : (void*)&fh2HookPosition);
        intptr_t rel = reinterpret_cast<intptr_t>(t) - static_cast<intptr_t>(base + s.rva + 5);
        if (rel > INT32_MAX || rel < INT32_MIN) { LOG("patch: thunk out of rel32 range"); return false; }
        rels[i] = static_cast<int32_t>(rel);
    }
    DWORD old;
    VirtualProtect(thunks, 4096, PAGE_EXECUTE_READ, &old);
    FlushInstructionCache(GetCurrentProcess(), thunks, 4096);

    // Make the whole case block writable once, outside the suspended window.
    uint8_t* block = reinterpret_cast<uint8_t*>(base + kCaseBlockRva);
    DWORD prot;
    if (!VirtualProtect(block, kCaseBlockSize, PAGE_EXECUTE_READWRITE, &prot)) {
        LOG("patch: VirtualProtect failed %lu", GetLastError());
        return false;
    }
    Suspended sus;
    sus.suspendOthers();
    for (int i = 0; i < 5; ++i)
        std::memcpy(reinterpret_cast<uint8_t*>(base + kSites[i].rva + 1), &rels[i], 4);
    FlushInstructionCache(GetCurrentProcess(), block, kCaseBlockSize);
    sus.resumeAll();
    VirtualProtect(block, kCaseBlockSize, prot, &prot);
    LOG("patch: 5 head-tracking inputs redirected (thunks at %p)", (void*)thunks);
    return true;
}

} // namespace

bool installWhenReady(const Config& cfg) {
    g_cockpitOnly = cfg.cockpitOnly;
    LOG("waiting for the Forza Horizon 2 image to be mapped...");
    for (int attempt = 0;; ++attempt) {
        bool deep = (attempt % 250) == 249;       // full scan every ~5 s
        if (uintptr_t base = findGameBase(deep)) {
            LOG("game image found at %p", (void*)base);
            Sleep(250);   // let Azimuth finish its own fix-ups of the image
            if (!verify(base)) {
                LOG("this game build is not recognised; head tracking and wheel support stay dormant, the game runs unmodified");
                return false;
            }
            bool patched = false;
            if (cfg.overrideAzimuthHeadTracking) patched = patch(base);
            else LOG("head tracking: left to Azimuth (OverrideAzimuthHeadTracking=0)");
            WheelHook_Install(base, cfg);
            FovHook_Install(base, cfg);
            AssistHook_Install(base, cfg);
            DrawDist_Install(base, cfg);
            CockpitHook_Install(base, cfg);
            return patched;
        }
        Sleep(20);
    }
}

} // namespace fh2
