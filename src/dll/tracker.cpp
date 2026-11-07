// SPDX-License-Identifier: MIT
// Head tracker input (FreeTrack shared memory or OpenTrack UDP), centring and smoothing.
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <atomic>
#include <cstdint>
#include <cstring>
#include "tracker.h"
#include "freetrack.h"
#include "log.h"
#include "wheel_device.h"

namespace fh2 {

namespace {
Config            g_cfg;
SRWLOCK           g_pubLock = SRWLOCK_INIT;
CameraOffsets     g_published;         // guarded by g_pubLock
bool              g_publishedValid = false;
std::atomic<bool> g_enabled{true};
std::atomic<bool> g_recenterRequest{false};

// Pose processing state, guarded by g_stateLock (sources run on their own threads).
CRITICAL_SECTION  g_stateLock;
RawPose           g_center, g_smoothed, g_lastRaw;
bool              g_haveSmoothed = false;
double            g_lastSampleT = -1e9, g_lastStepT = 0, g_lastFreeTrackT = -1e9;
const char*       g_activeSource = "none";
bool              g_stale = true;
CameraOffsets     g_lastOffsets;

double nowSeconds() {
    static LARGE_INTEGER freq = [] { LARGE_INTEGER f; QueryPerformanceFrequency(&f); return f; }();
    LARGE_INTEGER t; QueryPerformanceCounter(&t);
    return double(t.QuadPart) / double(freq.QuadPart);
}

void publish(const CameraOffsets& o, bool valid) {
    AcquireSRWLockExclusive(&g_pubLock);
    g_published = o;
    g_publishedValid = valid;
    ReleaseSRWLockExclusive(&g_pubLock);
}

void onSample(const RawPose& p, bool fromFreeTrack) {
    if (!poseFinite(p)) return;   // a NaN would stick in the smoothed pose and the centre, and reach the camera
    double t = nowSeconds();
    if (fromFreeTrack) g_lastFreeTrackT = t;
    // In auto mode FreeTrack wins while it is live, so two sources never fight.
    else if (g_cfg.source == Source::Auto && t - g_lastFreeTrackT < 0.3) return;

    EnterCriticalSection(&g_stateLock);
    const char* src = fromFreeTrack ? "freetrack" : "udp";
    if (g_activeSource != src) { LOG("input: using %s", src); g_activeSource = src; }
    double dt = t - g_lastStepT; g_lastStepT = t;
    if (!g_haveSmoothed || dt > 0.25) { g_smoothed = p; g_haveSmoothed = true; }
    else smoothPose(g_smoothed, p, smoothFactor(g_cfg.smoothing, dt));
    g_lastRaw = p;
    g_lastSampleT = t;
    if (g_recenterRequest.exchange(false)) { g_center = g_smoothed; LOG("recentred"); }
    if (g_stale) { LOG("input: tracking data flowing"); g_stale = false; }
    g_lastOffsets = mapPose(g_smoothed, g_center, g_cfg.map);
    publish(g_lastOffsets, true);
    LeaveCriticalSection(&g_stateLock);
}

// ---------------------------------------------------------------- UDP ----
DWORD WINAPI udpThread(void*) {
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) { LOG("udp: WSAStartup failed"); return 1; }
    SOCKET s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s == INVALID_SOCKET) { LOG("udp: socket() failed %d", WSAGetLastError()); return 1; }
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons((u_short)g_cfg.udpPort);
    if (inet_pton(AF_INET, g_cfg.bindAddress.c_str(), &addr.sin_addr) != 1) {
        LOG("udp: bad BindAddress '%s', using 127.0.0.1", g_cfg.bindAddress.c_str());
        inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
    }
    if (bind(s, (sockaddr*)&addr, sizeof(addr)) != 0) {
        LOG("udp: bind %s:%d failed (%d). Is another program using the port?",
            g_cfg.bindAddress.c_str(), g_cfg.udpPort, WSAGetLastError());
        return 1;
    }
    LOG("udp: listening on %s:%d", g_cfg.bindAddress.c_str(), g_cfg.udpPort);
    bool first = true;
    for (;;) {
        double buf[6];
        int n = recv(s, (char*)buf, sizeof(buf), 0);
        if (n != (int)sizeof(buf)) continue;
        RawPose p{buf[0], buf[1], buf[2], buf[3], buf[4], buf[5]};
        if (first) {
            LOG("udp: first packet x%.1f y%.1f z%.1f yaw%.1f pitch%.1f roll%.1f", p.x, p.y, p.z, p.yaw, p.pitch, p.roll);
            first = false;
        }
        onSample(p, false);
    }
}

// ---------------------------------------------------------- FreeTrack ----
// Shared memory written by Beam ("Head Tracking Device Emulation"), OpenTrack's
// "freetrack 2.0 Enhanced" output and other TrackIR/FreeTrack emulators.
DWORD WINAPI freetrackThread(void*) {
    HANDLE map = nullptr;
    const FTHeap* heap = nullptr;
    DWORD lastErr = 0;
    uint32_t lastId = 0;
    float lastPose[6] = {};
    bool loggedFirst = false;
    double lastChange = 0;
    for (;;) {
        if (!heap) {
            map = OpenFileMappingW(FILE_MAP_READ, FALSE, kFreeTrackHeap);
            if (!map) {
                DWORD e = GetLastError();
                if (e != lastErr) {
                    if (e == ERROR_FILE_NOT_FOUND) LOG("freetrack: no shared memory yet (start Beam/OpenTrack); still watching");
                    else LOG("freetrack: cannot open shared memory, error %lu%s", e,
                             e == ERROR_ACCESS_DENIED ? " (is the tracker running as administrator while Azimuth is not?)" : "");
                    lastErr = e;
                }
                Sleep(500);
                continue;
            }
            heap = static_cast<const FTHeap*>(MapViewOfFile(map, FILE_MAP_READ, 0, 0, sizeof(FTHeap)));
            if (!heap) { CloseHandle(map); map = nullptr; Sleep(1000); continue; }
            LOG("freetrack: shared memory found");
            lastErr = 0;
        }
        FTData d;
        std::memcpy(&d, (const void*)&heap->data, sizeof(d));
        float pose[6] = {d.Yaw, d.Pitch, d.Roll, d.X, d.Y, d.Z};
        bool changed = d.DataID != lastId || std::memcmp(pose, lastPose, sizeof(pose)) != 0;
        double t = nowSeconds();
        if (changed) {
            lastId = d.DataID;
            std::memcpy(lastPose, pose, sizeof(pose));
            lastChange = t;
            RawPose p = freetrackToOpentrack(d);
            if (!loggedFirst) {
                LOG("freetrack: data updating (id %u) x%.1f y%.1f z%.1f yaw%.1f pitch%.1f roll%.1f",
                    d.DataID, p.x, p.y, p.z, p.yaw, p.pitch, p.roll);
                loggedFirst = true;
            }
            onSample(p, true);
        } else if (t - lastChange > 5.0 && loggedFirst) {
            loggedFirst = false;   // log again when it resumes
        }
        Sleep(2);
    }
}

// ------------------------------------------------------ monitor/hotkeys ----
bool pressed(int vk) { return vk > 0 && (GetAsyncKeyState(vk) & 0x8000) != 0; }

// XInput is loaded dynamically and only read (button state), so it does not
// interfere with Azimuth's own controller handling.
struct XPad { DWORD packet; WORD buttons; BYTE lt, rt; SHORT lx, ly, rx, ry; };
typedef DWORD (WINAPI *XInputGetStateFn)(DWORD, XPad*);

XInputGetStateFn loadXInput() {
    for (const wchar_t* dll : {L"xinput1_4.dll", L"xinput1_3.dll", L"xinput9_1_0.dll"}) {
        if (HMODULE m = LoadLibraryW(dll)) {
            if (auto fn = reinterpret_cast<XInputGetStateFn>(GetProcAddress(m, "XInputGetState"))) {
                LOG("controller: using %ls for the recenter buttons", dll);
                return fn;
            }
        }
    }
    LOG("controller: XInput not available, controller recenter disabled");
    return nullptr;
}

bool padCombo(XInputGetStateFn get, unsigned mask) {
    if (!get || !mask) return false;
    for (DWORD i = 0; i < 4; ++i) {
        XPad st{};
        if (get(i, &st) == ERROR_SUCCESS && (st.buttons & mask) == mask) return true;
    }
    return false;
}

DWORD WINAPI monitorThread(void*) {
    bool prevToggle = false, prevRecenter = false;
    double lastDiag = 0;
    XInputGetStateFn xinput = g_cfg.recenterButtons ? loadXInput() : nullptr;
    for (;;) {
        Sleep(15);
        bool chord = pressed(VK_CONTROL) && pressed(VK_SHIFT);
        bool toggle   = pressed(g_cfg.toggleKey)   || (chord && pressed(g_cfg.chordToggleKey));
        bool recenter = pressed(g_cfg.recenterKey) || (chord && pressed(g_cfg.chordRecenterKey)) ||
                        padCombo(xinput, g_cfg.recenterButtons) ||
                        WheelDevice_ButtonsHeld(g_cfg.wheelRecenterButtons, g_cfg.wheelRecenterCount);
        if (toggle && !prevToggle) {
            bool e = !g_enabled.load();
            g_enabled = e;
            LOG("head tracking %s", e ? "ON" : "OFF");
        }
        if (recenter && !prevRecenter) g_recenterRequest = true;
        prevToggle = toggle; prevRecenter = recenter;

        double t = nowSeconds();
        EnterCriticalSection(&g_stateLock);
        if (!g_stale && (t - g_lastSampleT) * 1000.0 > g_cfg.staleTimeoutMs) {
            LOG("input: no data for %d ms, view returns to centre", g_cfg.staleTimeoutMs);
            g_stale = true;
            publish(CameraOffsets{}, false);
        }
        if (g_cfg.diagnostics && !g_stale && t - lastDiag > 2.0) {
            lastDiag = t;
            const RawPose& r = g_lastRaw;
            const CameraOffsets& o = g_lastOffsets;
            float camYaw = o.rotDeg[g_cfg.map.rotOrder[0]];
            const char* yawSide = std::fabs(camYaw) < 1.0f ? "centre"
                                  : ((camYaw > 0.0f) == g_cfg.map.yawLeftIsPositive ? "left" : "right");
            LOG("diag %s: in yaw %.1f pitch %.1f roll %.1f x %.1f y %.1f z %.1f cm | centre yaw %.1f pitch %.1f | "
                "cam rot %.1f %.1f %.1f deg pos %.3f %.3f %.3f m%s (yaw %s)",
                g_activeSource, r.yaw, r.pitch, r.roll, r.x, r.y, r.z, g_center.yaw, g_center.pitch,
                o.rotDeg[0], o.rotDeg[1], o.rotDeg[2], o.posM[0], o.posM[1], o.posM[2],
                g_enabled.load() ? "" : " (tracking OFF)", yawSide);
        }
        LeaveCriticalSection(&g_stateLock);
    }
}
} // namespace

void Tracker::start(const Config& cfg) {
    g_cfg = cfg;
    g_enabled = cfg.enableOnStartup;
    InitializeCriticalSection(&g_stateLock);
    if (cfg.source != Source::FreeTrack) CloseHandle(CreateThread(nullptr, 0, udpThread, nullptr, 0, nullptr));
    if (cfg.source != Source::Udp)       CloseHandle(CreateThread(nullptr, 0, freetrackThread, nullptr, 0, nullptr));
    CloseHandle(CreateThread(nullptr, 0, monitorThread, nullptr, 0, nullptr));
}

bool Tracker::read(CameraOffsets& out) {
    if (!g_enabled.load(std::memory_order_relaxed)) return false;
    AcquireSRWLockShared(&g_pubLock);
    bool v = g_publishedValid;
    if (v) out = g_published;
    ReleaseSRWLockShared(&g_pubLock);
    return v;
}

} // namespace fh2
