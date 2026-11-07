// SPDX-License-Identifier: MIT
// DirectInput steering wheel reader and force feedback output.
#define DIRECTINPUT_VERSION 0x0800
#include "wheel_device.h"
#include <dinput.h>
#include <cmath>
#include <cstdlib>
#include <cwctype>
#include <deque>
#include <string>
#include <vector>
#include "log.h"
#include "steer_lock.h"
#include "wheel_com.h"
#include "wheel_ffb.h"

namespace fh2 {

namespace {

WheelConfig        g_cfg;
SRWLOCK            g_lock = SRWLOCK_INIT;
WheelInput         g_snap;
IDirectInputDevice8W* g_dev = nullptr;
IDirectInput8W*       g_di = nullptr;
bool               g_started = false;
bool               g_diag = false;
bool               g_ffbCapable = false;
FfbStatus          g_ffbStatus;

// Pedals, shifter and handbrake can live on their own USB device (PedalsDevice / ShifterDevice /
// HandbrakeDevice). useWheel = no separate device configured (or it is the wheel itself): read the wheel state.
struct AuxDev {
    const char*           label = "";
    std::string           match;
    bool                  useWheel = true;
    IDirectInputDevice8W* dev = nullptr;
    std::wstring          name;
    bool                  ready = false;       // cooperative level, range and Acquire done
    bool                  acquired = false;    // logged once
    bool                  fresh = false;       // st holds a current reading
    double                lastTryMs = -1e9;
    DIJOYSTATE2           st{};
    uint32_t              prevBtn[4] = {};
    double                lastDiagMs = 0;
};
AuxDev             g_aux[3];                   // 0 pedals, 1 shifter, 2 handbrake
GUID               g_wheelGuid{};

std::string utf8(const wchar_t* w) {
    int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
    if (n <= 0) return "?";
    std::string s(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w, -1, &s[0], n, nullptr, nullptr);
    s.resize(n - 1);
    return s;
}

std::wstring lower(const std::wstring& s) {
    std::wstring r = s;
    for (auto& c : r) c = (wchar_t)towlower(c);
    return r;
}

struct DevInfo {
    GUID         instance;
    std::wstring name;
    DWORD        type;
    DWORD        data1;
};

BOOL CALLBACK enumCb(const DIDEVICEINSTANCEW* inst, void* user) {
    auto* v = static_cast<std::vector<DevInfo>*>(user);
    v->push_back({inst->guidInstance, inst->tszProductName, inst->dwDevType, inst->guidProduct.Data1});
    return DIENUM_CONTINUE;
}

HWND findOwnWindow() {
    struct Ctx { DWORD pid; HWND hwnd; } ctx{GetCurrentProcessId(), nullptr};
    EnumWindows([](HWND h, LPARAM lp) -> BOOL {
        auto* c = reinterpret_cast<Ctx*>(lp);
        DWORD pid = 0;
        GetWindowThreadProcessId(h, &pid);
        if (pid == c->pid && IsWindowVisible(h)) { c->hwnd = h; return FALSE; }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&ctx));
    return ctx.hwnd;
}

LONG axisValue(const DIJOYSTATE2& s, WheelAxis a, bool& present) {
    present = true;
    switch (a) {
        case WheelAxis::X: return s.lX;
        case WheelAxis::Y: return s.lY;
        case WheelAxis::Z: return s.lZ;
        case WheelAxis::RX: return s.lRx;
        case WheelAxis::RY: return s.lRy;
        case WheelAxis::RZ: return s.lRz;
        case WheelAxis::Slider0: return s.rglSlider[0];
        case WheelAxis::Slider1: return s.rglSlider[1];
        default: present = false; return 0;
    }
}

DWORD axisOffset(WheelAxis a) {
    switch (a) {
        case WheelAxis::X: return DIJOFS_X;
        case WheelAxis::Y: return DIJOFS_Y;
        case WheelAxis::Z: return DIJOFS_Z;
        case WheelAxis::RX: return DIJOFS_RX;
        case WheelAxis::RY: return DIJOFS_RY;
        case WheelAxis::RZ: return DIJOFS_RZ;
        case WheelAxis::Slider0: return DIJOFS_SLIDER(0);
        case WheelAxis::Slider1: return DIJOFS_SLIDER(1);
        default: return DIJOFS_X;
    }
}

float pedal(const DIJOYSTATE2* sp, WheelAxis a) {
    if (!sp) return 0.0f;                  // pedals device missing: pedals read as released
    const DIJOYSTATE2& s = *sp;
    bool present;
    LONG raw = axisValue(s, a, present);
    if (!present) return 0.0f;
    double p = raw / 65535.0;
    p = p < 0 ? 0 : p > 1 ? 1 : p;
    if (g_cfg.pedalsInverted) p = 1.0 - p;
    double dz = g_cfg.pedalDeadzone;
    p = (p - dz) / (1.0 - dz);
    p = p < 0 ? 0 : p > 1 ? 1 : p;
    return (float)p;
}

bool btn(const DIJOYSTATE2& s, int idx) {
    return idx >= 0 && idx < 128 && (s.rgbButtons[idx] & 0x80);
}

// s = the wheel; ped / shf / hb = where the pedals, shifter and handbrake are read (nullptr = device missing).
void mapState(const DIJOYSTATE2& s, const DIJOYSTATE2* ped, const DIJOYSTATE2* shf, const DIJOYSTATE2* hbs,
              WheelInput& o) {
    bool present;
    LONG raw = axisValue(s, g_cfg.steeringAxis, present);
    double st = present ? (raw - 32767.5) / 32767.5 : 0.0;
    st = st > 1 ? 1 : st < -1 ? -1 : st;
    if (g_cfg.steeringInvert) st = -st;
    o.steering = (float)st;
    o.throttle = pedal(ped, g_cfg.throttleAxis);
    o.brake    = pedal(ped, g_cfg.brakeAxis);
    o.clutch   = pedal(ped, g_cfg.clutchAxis);
    o.a = btn(s, g_cfg.buttonA); o.b = btn(s, g_cfg.buttonB);
    o.x = btn(s, g_cfg.buttonX); o.y = btn(s, g_cfg.buttonY);
    o.view = btn(s, g_cfg.buttonView); o.menu = btn(s, g_cfg.buttonMenu);
    o.lb = btn(s, g_cfg.buttonLB); o.rb = btn(s, g_cfg.buttonRB);
    {   // real handbrake -> the configured game button
        bool hbPresent = false;
        LONG hraw = hbs ? axisValue(*hbs, g_cfg.handbrakeAxis, hbPresent) : 0;
        double h = 0.0;
        if (hbPresent) {
            h = hraw / 65535.0;
            h = h < 0 ? 0 : h > 1 ? 1 : h;
            if (g_cfg.handbrakeAxisInvert) h = 1.0 - h;
        }
        o.handbrakeAxis = (float)h;
        o.handbrake = (hbs && btn(*hbs, g_cfg.handbrakeButton)) || (hbPresent && h >= g_cfg.handbrakeThreshold);
        if (o.handbrake) {
            switch (g_cfg.handbrakeGameButton) {
                case 'A': o.a = true; break;
                case 'X': o.x = true; break;
                case 'Y': o.y = true; break;
                case 'L': o.lb = true; break;
                case 'R': o.rb = true; break;
                default:  o.b = true; break;
            }
        }
    }
    o.up = o.down = o.left = o.right = false;
    DWORD pov = s.rgdwPOV[0];
    if ((pov & 0xFFFF) != 0xFFFF) {
        pov %= 36000;
        o.up    = pov >= 31500 || pov <= 4500;
        o.right = pov >= 4500 && pov <= 13500;
        o.down  = pov >= 13500 && pov <= 22500;
        o.left  = pov >= 22500 && pov <= 31500;
    }
    o.raw[0] = s.lX; o.raw[1] = s.lY; o.raw[2] = s.lZ;
    o.raw[3] = s.lRx; o.raw[4] = s.lRy; o.raw[5] = s.lRz;
    o.raw[6] = s.rglSlider[0]; o.raw[7] = s.rglSlider[1];
    for (int i = 0; i < 4; ++i) o.rawButtons[i] = 0;
    for (int i = 0; i < 128; ++i)
        if (s.rgbButtons[i] & 0x80) o.rawButtons[i / 32] |= (1u << (i % 32));
    o.gear = 0;
    if (g_cfg.hShifter && shf) {
        for (int i = 0; i < 7; ++i)
            if (btn(*shf, g_cfg.gearButtons[i])) { o.gear = i + 1; break; }
        if (o.gear == 0 && btn(*shf, g_cfg.reverseButton)) o.gear = -1;
    }
}

uint64_t qpc() { LARGE_INTEGER t; QueryPerformanceCounter(&t); return (uint64_t)t.QuadPart; }

void publish(const WheelInput& in) {
    AcquireSRWLockExclusive(&g_lock);
    g_snap = in;
    ReleaseSRWLockExclusive(&g_lock);
}

DWORD WINAPI testThread(void*) {
    for (;;) {
        WheelInput in;
        in.steering = 0.25f; in.throttle = 0.5f; in.a = true;
        in.timestamp = qpc();
        in.valid = true;
        publish(in);
        Sleep(2);
    }
}

double qpcMs() {
    static double freq = [] { LARGE_INTEGER f; QueryPerformanceFrequency(&f); return (double)f.QuadPart; }();
    return (double)qpc() * 1000.0 / freq;
}

std::wstring widen(const std::string& s) {
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    std::wstring w(n > 0 ? n : 1, L'\0');
    if (n > 0) MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, &w[0], n);
    w.resize(wcslen(w.c_str()));
    return w;
}

// Finds the first attached device whose name contains a.match and opens it. If that device is the wheel itself,
// the pedals/shifter/handbrake are simply read from the wheel. Returns true when the aux is resolved.
bool openAux(AuxDev& a, bool logMiss) {
    if (!g_di) return false;
    std::vector<DevInfo> devs;
    g_di->EnumDevices(DI8DEVCLASS_GAMECTRL, enumCb, &devs, DIEDFL_ATTACHEDONLY);
    const std::wstring m = lower(widen(a.match));
    for (auto& d : devs) {
        if (lower(d.name).find(m) == std::wstring::npos) continue;
        if (IsEqualGUID(d.instance, g_wheelGuid)) {
            a.useWheel = true;
            LOG("wheel: %s: \"%s\" is the wheel itself; reading them from the wheel", a.label, utf8(d.name.c_str()).c_str());
            return true;
        }
        IDirectInputDevice8W* dev = nullptr;
        HRESULT hr = g_di->CreateDevice(d.instance, &dev, nullptr);
        if (FAILED(hr) || !dev) { LOG("wheel: %s: CreateDevice failed 0x%08lx", a.label, (unsigned long)hr); continue; }
        hr = dev->SetDataFormat(&c_dfDIJoystick2);
        if (FAILED(hr)) { LOG("wheel: %s: SetDataFormat failed 0x%08lx", a.label, (unsigned long)hr); dev->Release(); continue; }
        a.dev = dev;
        a.name = d.name;
        a.useWheel = false;
        a.ready = false;
        a.acquired = false;
        LOG("wheel: %s: using \"%s\"", a.label, utf8(d.name.c_str()).c_str());
        return true;
    }
    if (logMiss) LOG("wheel: %s: no device found (name contains '%s'); will keep looking", a.label, a.match.c_str());
    return false;
}

void diagAux(AuxDev& a, double nowMs) {
    bool changed = false;
    uint32_t cur[4] = {};
    for (int i = 0; i < 128; ++i)
        if (a.st.rgbButtons[i] & 0x80) cur[i / 32] |= (1u << (i % 32));
    for (int w = 0; w < 4; ++w) if (cur[w] != a.prevBtn[w]) changed = true;
    if (changed) {
        std::string dn, up;
        for (int b = 0; b < 128; ++b) {
            bool now = (cur[b / 32] >> (b % 32)) & 1, was = (a.prevBtn[b / 32] >> (b % 32)) & 1;
            if (now && !was) dn += (dn.empty() ? "" : ",") + std::to_string(b);
            if (!now && was) up += (up.empty() ? "" : ",") + std::to_string(b);
        }
        LOG("%s buttons: down %s up %s", a.label, dn.empty() ? "-" : dn.c_str(), up.empty() ? "-" : up.c_str());
        for (int w = 0; w < 4; ++w) a.prevBtn[w] = cur[w];
    }
    if (nowMs - a.lastDiagMs >= 2000.0) {
        a.lastDiagMs = nowMs;
        LOG("%s axes: X %ld Y %ld Z %ld Rx %ld Ry %ld Rz %ld S0 %ld S1 %ld", a.label, (long)a.st.lX, (long)a.st.lY,
            (long)a.st.lZ, (long)a.st.lRx, (long)a.st.lRy, (long)a.st.lRz, (long)a.st.rglSlider[0], (long)a.st.rglSlider[1]);
    }
}

void pollAux(AuxDev& a, HWND hwnd, double nowMs) {
    if (a.useWheel) return;
    if (!a.dev) {                                   // not found yet, or unplugged: look again every 3 s
        if (nowMs - a.lastTryMs < 3000.0) return;
        a.lastTryMs = nowMs;
        if (!openAux(a, false) || a.useWheel || !a.dev) return;
    }
    if (!a.ready) {
        HRESULT hr = a.dev->SetCooperativeLevel(hwnd, DISCL_NONEXCLUSIVE | DISCL_BACKGROUND);
        if (FAILED(hr)) LOG("wheel: %s: SetCooperativeLevel failed 0x%08lx", a.label, (unsigned long)hr);
        DIPROPRANGE rng{};
        rng.diph.dwSize = sizeof(rng);
        rng.diph.dwHeaderSize = sizeof(DIPROPHEADER);
        rng.diph.dwObj = 0;
        rng.diph.dwHow = DIPH_DEVICE;
        rng.lMin = 0; rng.lMax = 65535;
        a.dev->SetProperty(DIPROP_RANGE, &rng.diph);   // failures ignored
        a.dev->Acquire();
        a.ready = true;
    }
    HRESULT hr = a.dev->Poll();
    DIJOYSTATE2 s{};
    if (SUCCEEDED(hr)) hr = a.dev->GetDeviceState(sizeof(s), &s);
    if (hr == DIERR_INPUTLOST || hr == DIERR_NOTACQUIRED) {
        a.fresh = false;
        a.dev->Acquire();
        return;
    }
    if (FAILED(hr)) {
        a.fresh = false;
        if (hr == DIERR_UNPLUGGED) {                // drop it; the retry above re-opens it when it is back
            LOG("wheel: %s: device unplugged", a.label);
            a.dev->Release();
            a.dev = nullptr;
            a.ready = false;
        }
        return;
    }
    if (!a.acquired) { a.acquired = true; LOG("wheel: %s: device acquired", a.label); }
    a.st = s;
    a.fresh = true;
    if (g_diag) diagAux(a, nowMs);
}

DWORD WINAPI pollThread(void*) {
    // Wait for a visible top-level window of this process.
    HWND hwnd = nullptr;
    while (!(hwnd = findOwnWindow())) Sleep(200);
    bool ffbOn = g_cfg.ffbEnabled && g_ffbCapable && !g_cfg.testMode;
    HRESULT hr = S_OK;
    if (ffbOn) {
        hr = g_dev->SetCooperativeLevel(hwnd, DISCL_EXCLUSIVE | DISCL_BACKGROUND);
        if (FAILED(hr)) {
            LOG("wheel ffb: exclusive cooperative level failed 0x%08lx; force feedback off", (unsigned long)hr);
            ffbOn = false;
        }
    }
    if (!ffbOn) {
        hr = g_dev->SetCooperativeLevel(hwnd, DISCL_NONEXCLUSIVE | DISCL_BACKGROUND);
        if (FAILED(hr)) LOG("wheel: SetCooperativeLevel failed 0x%08lx", (unsigned long)hr);
    }
    DIPROPRANGE rng{};
    rng.diph.dwSize = sizeof(rng);
    rng.diph.dwHeaderSize = sizeof(DIPROPHEADER);
    rng.diph.dwObj = 0;
    rng.diph.dwHow = DIPH_DEVICE;
    rng.lMin = 0; rng.lMax = 65535;
    g_dev->SetProperty(DIPROP_RANGE, &rng.diph);   // failures ignored
    if (ffbOn) {
        DIPROPDWORD ac{};
        ac.diph.dwSize = sizeof(ac);
        ac.diph.dwHeaderSize = sizeof(DIPROPHEADER);
        ac.diph.dwObj = 0;
        ac.diph.dwHow = DIPH_DEVICE;
        ac.dwData = DIPROPAUTOCENTER_OFF;
        g_dev->SetProperty(DIPROP_AUTOCENTER, &ac.diph);   // failures ignored
    }
    g_dev->Acquire();
    LOG("wheel: polling started");

    bool acquired = false;
    IDirectInputEffect* effect = nullptr;
    bool effectTried = false, needRestart = false, sendErrLogged = false, forceSend = false;
    DICONSTANTFORCE cf{};
    DIEFFECT eff{};
    LONG dir[1] = {1};          // eff points at these for the lifetime of the effect
    DWORD axes[1] = {DIJOFS_X};
    LONG lastSent = 0;
    double lastSendMs = 0;
    struct Sample { double ms; float angle; float phys; };
    std::deque<Sample> samples;
    float velocity = 0;
    float physVel = 0;     // degrees per second of the real wheel (not clamped at the lock)
    for (;;) {
        hr = g_dev->Poll();
        DIJOYSTATE2 st{};
        if (SUCCEEDED(hr)) hr = g_dev->GetDeviceState(sizeof(st), &st);
        if (hr == DIERR_INPUTLOST || hr == DIERR_NOTACQUIRED) {
            acquired = false;
            if (effect) needRestart = true;
            g_dev->Acquire();
            AcquireSRWLockExclusive(&g_lock);
            g_snap.valid = false;
            ReleaseSRWLockExclusive(&g_lock);
            Sleep(2);
            continue;
        }
        if (SUCCEEDED(hr)) {
            if (!acquired) { acquired = true; LOG("wheel: device acquired"); }
            WheelInput in;
            const double auxNowMs = qpcMs();
            for (AuxDev& a : g_aux) pollAux(a, hwnd, auxNowMs);
            auto src = [&](const AuxDev& a) -> const DIJOYSTATE2* {
                return a.useWheel ? &st : (a.fresh ? &a.st : nullptr);
            };
            mapState(st, src(g_aux[0]), src(g_aux[1]), src(g_aux[2]), in);
            in.timestamp = qpc();
            in.valid = true;
            publish(in);

            if (g_diag) {
                static bool havePrev = false;
                static uint32_t prevBtn[4] = {};
                static DWORD prevPov = 0xFFFFFFFF;
                bool changed = !havePrev;
                for (int w = 0; w < 4; ++w) if (in.rawButtons[w] != prevBtn[w]) changed = true;
                if (changed && havePrev) {
                    std::string dn, up;
                    for (int b = 0; b < 128; ++b) {
                        bool now = (in.rawButtons[b / 32] >> (b % 32)) & 1, was = (prevBtn[b / 32] >> (b % 32)) & 1;
                        if (now && !was) dn += (dn.empty() ? "" : ",") + std::to_string(b);
                        if (!now && was) up += (up.empty() ? "" : ",") + std::to_string(b);
                    }
                    LOG("wheel buttons: down %s up %s", dn.empty() ? "-" : dn.c_str(), up.empty() ? "-" : up.c_str());
                }
                for (int w = 0; w < 4; ++w) prevBtn[w] = in.rawButtons[w];
                if (!havePrev || st.rgdwPOV[0] != prevPov) {
                    if (havePrev) LOG("wheel pov: %lu", (unsigned long)st.rgdwPOV[0]);
                    prevPov = st.rgdwPOV[0];
                }
                havePrev = true;
            }

            if (ffbOn && !effectTried) {
                effectTried = true;
                axes[0] = axisOffset(g_cfg.steeringAxis);
                cf.lMagnitude = 0;
                eff.dwSize = sizeof(DIEFFECT);
                eff.dwFlags = DIEFF_CARTESIAN | DIEFF_OBJECTOFFSETS;
                eff.dwDuration = INFINITE;
                eff.dwSamplePeriod = 0;
                eff.dwGain = 10000;
                eff.dwTriggerButton = DIEB_NOTRIGGER;
                eff.dwTriggerRepeatInterval = 0;
                eff.cAxes = 1;
                eff.rgdwAxes = axes;
                eff.rglDirection = dir;
                eff.lpEnvelope = nullptr;
                eff.cbTypeSpecificParams = sizeof(DICONSTANTFORCE);
                eff.lpvTypeSpecificParams = &cf;
                eff.dwStartDelay = 0;
                HRESULT he = g_dev->CreateEffect(GUID_ConstantForce, &eff, &effect, nullptr);
                if (SUCCEEDED(he) && effect) he = effect->Start(1, 0);
                if (SUCCEEDED(he) && effect) {
                    LOG("wheel ffb: constant force effect started");
                    lastSent = 0; lastSendMs = qpcMs();
                } else {
                    LOG("wheel ffb: constant force effect failed 0x%08lx; force feedback off", (unsigned long)he);
                    if (effect) { effect->Release(); effect = nullptr; }
                    ffbOn = false;
                }
            }
            if (effect && needRestart) {
                effect->Start(1, 0);
                forceSend = true;      // reset lastSent: the next value is always sent
                needRestart = false;
            }
            if (ffbOn && effect) {
                double nowMs = qpcMs();
                float angle = VirtualWheel_GameAngle(in.steering);
                samples.push_back({nowMs, angle, in.steering * (float)g_cfg.wheelRange * 0.5f});
                // keep the newest sample that is >= 20 ms old; drop everything older
                int idx = -1;
                for (int i = (int)samples.size() - 1; i >= 0; --i)
                    if (nowMs - samples[i].ms >= 20.0) { idx = i; break; }
                if (idx >= 0) {
                    samples.erase(samples.begin(), samples.begin() + idx);
                    const float dtS = (float)((nowMs - samples.front().ms) / 1000.0);
                    velocity = (angle - samples.front().angle) / dtS;
                    physVel = (samples.back().phys - samples.front().phys) / dtS;
                }
                FfbParams p;
                VirtualWheel_FfbSnapshot(p);
                FfbTerms terms{};
                float F = FfbForce(p, g_cfg.ffbTuning, angle, velocity, nowMs, &terms);
                LONG mag = FfbDeviceMagnitude(F, g_cfg.ffbGain, g_cfg.ffbInvert);
                // Soft lock: end stop at the physical lock (works also when the game's FFB is idle, e.g. in menus).
                const float physLock = VirtualWheel_PhysicalLockDeg();
                const float physDeg = in.steering * (float)g_cfg.wheelRange * 0.5f;
                const float lockK = SoftLockK(physDeg, physLock, (float)g_cfg.wheelRange, g_cfg.ffbLockStrength, g_cfg.ffbLockRampDeg);
                if (lockK > 0.0f) {
                    mag = ApplySoftLock(mag, physDeg, physLock, (float)g_cfg.wheelRange, g_cfg.ffbLockStrength, g_cfg.ffbLockRampDeg,
                                        g_cfg.ffbInvert);
                    // damping inside the end zone, from the real wheel's speed (the game angle stops at the lock)
                    long damp = std::lround(physVel * 0.002f * lockK * 10000.0f * g_cfg.ffbLockStrength);
                    mag += g_cfg.ffbInvert ? -damp : damp;
                    mag = mag > 10000 ? 10000 : mag < -10000 ? -10000 : mag;
                }
                bool send = forceSend || labs(mag - lastSent) >= 20 || (mag != lastSent && nowMs - lastSendMs >= 10.0) ||
                            (mag == 0 && lastSent != 0);
                if (send) {
                    cf.lMagnitude = mag;
                    HRESULT sh = effect->SetParameters(&eff, DIEP_TYPESPECIFICPARAMS);
                    if (SUCCEEDED(sh)) {
                        lastSent = mag; lastSendMs = nowMs; forceSend = false;
                    } else if (sh == DIERR_INPUTLOST || sh == DIERR_NOTACQUIRED) {
                        acquired = false;
                        needRestart = true;
                        g_dev->Acquire();
                        AcquireSRWLockExclusive(&g_lock);
                        g_snap.valid = false;
                        ReleaseSRWLockExclusive(&g_lock);
                    } else if (!sendErrLogged) {
                        sendErrLogged = true;
                        LOG("wheel ffb: SetParameters failed 0x%08lx", (unsigned long)sh);
                    }
                }
                AcquireSRWLockExclusive(&g_lock);
                g_ffbStatus.terms = terms; g_ffbStatus.angle = angle; g_ffbStatus.velocity = velocity;
                g_ffbStatus.mag = mag; g_ffbStatus.lockDeg = std::fabs(physDeg); g_ffbStatus.lockLim = physLock * 0.5f;
                g_ffbStatus.lockK = lockK; g_ffbStatus.race = p.race; g_ffbStatus.stale = p.stale;
                g_ffbStatus.active = true;
                g_ffbStatus.haveGame = p.haveGame; g_ffbStatus.game = p.game;
                g_ffbStatus.leftAmp = p.leftAmp; g_ffbStatus.rightAmp = p.rightAmp;
                ReleaseSRWLockExclusive(&g_lock);
            }
        }
        Sleep(2);
    }
}

} // namespace

bool WheelDevice_Start(const WheelConfig& cfg, bool diagnostics) {
    if (g_started) return true;
    g_cfg = cfg;
    g_diag = diagnostics;
    if (cfg.testMode) {
        LOG("wheel: TestMode, synthetic input");
        if (HANDLE t = CreateThread(nullptr, 0, testThread, nullptr, 0, nullptr)) CloseHandle(t);
        g_started = true;
        return true;
    }
    HRESULT hr = DirectInput8Create(GetModuleHandleW(nullptr), DIRECTINPUT_VERSION, IID_IDirectInput8W,
                                    reinterpret_cast<void**>(&g_di), nullptr);
    if (FAILED(hr) || !g_di) { LOG("wheel: DirectInput8Create failed 0x%08lx", (unsigned long)hr); return false; }
    std::vector<DevInfo> devs;
    g_di->EnumDevices(DI8DEVCLASS_GAMECTRL, enumCb, &devs, DIEDFL_ATTACHEDONLY);
    for (auto& d : devs)
        LOG("wheel: device \"%s\" vid %04x pid %04x type 0x%x", utf8(d.name.c_str()).c_str(),
            (unsigned)(d.data1 & 0xFFFF), (unsigned)(d.data1 >> 16), (unsigned)d.type);

    int pick = -1;
    if (!cfg.deviceMatch.empty()) {
        int n = MultiByteToWideChar(CP_UTF8, 0, cfg.deviceMatch.c_str(), -1, nullptr, 0);
        std::wstring m(n > 0 ? n : 1, L'\0');
        if (n > 0) MultiByteToWideChar(CP_UTF8, 0, cfg.deviceMatch.c_str(), -1, &m[0], n);
        m.resize(wcslen(m.c_str()));
        m = lower(m);
        for (size_t i = 0; i < devs.size() && pick < 0; ++i)
            if (lower(devs[i].name).find(m) != std::wstring::npos) pick = (int)i;
    }
    if (pick < 0)
        for (size_t i = 0; i < devs.size() && pick < 0; ++i)
            if (GET_DIDEVICE_TYPE(devs[i].type) == DI8DEVTYPE_DRIVING) pick = (int)i;
    if (pick < 0) {
        LOG("wheel: no wheel found (DeviceMatch=%s)", cfg.deviceMatch.c_str());
        return false;
    }
    LOG("wheel: using \"%s\"", utf8(devs[pick].name.c_str()).c_str());
    g_wheelGuid = devs[pick].instance;
    g_aux[0].label = "pedals";   g_aux[0].match = cfg.pedalsMatch;
    g_aux[1].label = "shifter";  g_aux[1].match = cfg.shifterMatch;
    g_aux[2].label = "handbrake"; g_aux[2].match = cfg.handbrakeMatch;
    hr = g_di->CreateDevice(devs[pick].instance, &g_dev, nullptr);
    if (FAILED(hr) || !g_dev) { LOG("wheel: CreateDevice failed 0x%08lx", (unsigned long)hr); return false; }
    hr = g_dev->SetDataFormat(&c_dfDIJoystick2);
    if (FAILED(hr)) { LOG("wheel: SetDataFormat failed 0x%08lx", (unsigned long)hr); g_dev->Release(); g_dev = nullptr; return false; }
    if (cfg.ffbEnabled) {
        DIDEVCAPS caps{};
        caps.dwSize = sizeof(caps);
        if (SUCCEEDED(g_dev->GetCapabilities(&caps)) && (caps.dwFlags & DIDC_FORCEFEEDBACK)) {
            g_ffbCapable = true;
            LOG("wheel ffb: device supports force feedback");
        } else {
            LOG("wheel ffb: device has no force feedback");
        }
    }
    for (AuxDev& a : g_aux) {
        if (a.match.empty()) continue;             // read from the wheel
        a.useWheel = false;
        openAux(a, true);                          // pollAux keeps retrying when it is not found
    }
    if (HANDLE t = CreateThread(nullptr, 0, pollThread, nullptr, 0, nullptr)) CloseHandle(t);
    g_started = true;
    return true;
}

bool WheelDevice_ButtonsHeld(const int* idx, int n) {
    if (!idx || n <= 0) return false;
    WheelInput in;
    if (!WheelDevice_Read(in)) return false;
    for (int i = 0; i < n; ++i)
        if (idx[i] < 0 || idx[i] > 127 || !((in.rawButtons[idx[i] / 32] >> (idx[i] % 32)) & 1)) return false;
    return true;
}

bool WheelDevice_FfbStatus(FfbStatus& out) {
    AcquireSRWLockShared(&g_lock);
    out = g_ffbStatus;
    ReleaseSRWLockShared(&g_lock);
    return out.active;
}

bool WheelDevice_Read(WheelInput& out) {
    AcquireSRWLockShared(&g_lock);
    out = g_snap;
    ReleaseSRWLockShared(&g_lock);
    if (out.valid) {
        static uint64_t freq = [] { LARGE_INTEGER f; QueryPerformanceFrequency(&f); return (uint64_t)f.QuadPart; }();
        if (qpc() - out.timestamp > freq / 2) out.valid = false;
    }
    return out.valid;
}

} // namespace fh2
