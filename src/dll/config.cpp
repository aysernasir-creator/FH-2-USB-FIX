// SPDX-License-Identifier: MIT
// Plugin settings: loads FH2SimImmersion.ini into Config.
#include "config.h"
#include "log.h"
#include "../common/default_ini.h"
#include "../common/ini_migrate.h"
#include "../common/cars_ini.h"
#include <cstdio>
#include <cstdlib>
#include <cctype>

namespace fh2 {


// "L3+R3" -> XINPUT_GAMEPAD_* mask
static unsigned parseButtons(const std::string& spec) {
    static const struct { const char* name; unsigned mask; } kNames[] = {
        {"UP", 0x0001}, {"DOWN", 0x0002}, {"LEFT", 0x0004}, {"RIGHT", 0x0008},
        {"START", 0x0010}, {"BACK", 0x0020}, {"L3", 0x0040}, {"R3", 0x0080},
        {"LB", 0x0100}, {"RB", 0x0200}, {"A", 0x1000}, {"B", 0x2000}, {"X", 0x4000}, {"Y", 0x8000},
    };
    unsigned mask = 0;
    size_t i = 0;
    while (i < spec.size()) {
        size_t j = spec.find('+', i);
        if (j == std::string::npos) j = spec.size();
        std::string tok;
        for (size_t k = i; k < j; ++k) if (spec[k] != ' ') tok += (char)toupper((unsigned char)spec[k]);
        bool known = tok.empty();
        for (auto& n : kNames) if (tok == n.name) { mask |= n.mask; known = true; }
        if (!known) LOG("config: unknown controller button '%s' in RecenterButtons", tok.c_str());
        i = j + 1;
    }
    return mask;
}

// "4+5" -> {4,5}; empty or "none" -> 0 entries.
static int parseWheelButtons(const std::string& spec, int* out) {
    int n = 0;
    size_t i = 0;
    while (i < spec.size()) {
        size_t j = spec.find('+', i);
        if (j == std::string::npos) j = spec.size();
        std::string tok;
        for (size_t k = i; k < j; ++k) if (spec[k] != ' ' && spec[k] != '\t') tok += spec[k];
        i = j + 1;
        if (tok.empty()) continue;
        std::string low;
        for (char ch : tok) low += (char)tolower((unsigned char)ch);
        if (low == "none") continue;
        char* end = nullptr;
        long v = strtol(tok.c_str(), &end, 10);
        if (!end || *end != 0 || v < 0 || v > 127) { LOG("config: bad button '%s' in WheelRecenterButtons", tok.c_str()); continue; }
        if (n < 4) out[n++] = (int)v;
    }
    return n;
}

static std::string readStr(const wchar_t* ini, const char* sec, const char* key, const char* def) {
    wchar_t buf[256];
    auto wide = [](const char* a) { return std::wstring(a, a + strlen(a)); };   // section/key/default are ASCII
    GetPrivateProfileStringW(wide(sec).c_str(), wide(key).c_str(), wide(def).c_str(), buf, 256, ini);
    int n = WideCharToMultiByte(CP_UTF8, 0, buf, -1, nullptr, 0, nullptr, nullptr);
    std::string out(n > 0 ? n : 1, char(0));
    if (n > 0) WideCharToMultiByte(CP_UTF8, 0, buf, -1, &out[0], n, nullptr, nullptr);
    out.resize(strlen(out.c_str()));
    return out;
}
static double readF(const wchar_t* ini, const char* sec, const char* key, double def) {
    char d[64]; snprintf(d, sizeof(d), "%g", def);
    return atof(readStr(ini, sec, key, d).c_str());
}
static int readI(const wchar_t* ini, const char* sec, const char* key, int def) {
    char d[32]; snprintf(d, sizeof(d), "%d", def);
    return (int)strtol(readStr(ini, sec, key, d).c_str(), nullptr, 0);
}
static void readOrder(const wchar_t* ini, const char* key, int out[3]) {
    std::string s = readStr(ini, "Advanced", key, "0,1,2");
    int a = 0, b = 1, c = 2;
    if (sscanf(s.c_str(), "%d,%d,%d", &a, &b, &c) == 3 && a >= 0 && a < 3 && b >= 0 && b < 3 &&
        c >= 0 && c < 3 && a != b && b != c && a != c) {
        out[0] = a; out[1] = b; out[2] = c;
    } else {
        LOG("config: %s='%s' is not a permutation of 0,1,2; using 0,1,2", key, s.c_str());
    }
}
static AxisCfg readAxis(const wchar_t* ini, const char* sec, const char* name, float defLimit) {
    AxisCfg a;
    char k[64];
    snprintf(k, sizeof(k), "%sGain", name);   a.gain   = (float)readF(ini, sec, k, 1.0);
    snprintf(k, sizeof(k), "%sInvert", name); a.invert = readI(ini, sec, k, 0) != 0;
    snprintf(k, sizeof(k), "%sLimit", name);  a.limit  = (float)readF(ini, sec, k, defLimit);
    if (a.limit < 0) a.limit = 0;
    return a;
}

// NaN-safe clamp; logs when the value had to change.
static float clampCfg(const wchar_t*, const char* key, float v, float lo, float hi) {
    if (!(v >= lo)) { LOG("config: %s=%.2f below %.2f; clamping", key, v, lo); return lo; }
    if (v > hi) { LOG("config: %s=%.2f above %.2f; clamping", key, v, hi); return hi; }
    return v;
}

static WheelAxis readWheelAxis(const wchar_t* ini, const char* key, WheelAxis def, const char* defName) {
    std::string s = readStr(ini, "Wheel", key, defName), t;
    for (char ch : s) if (ch != ' ') t += (char)toupper((unsigned char)ch);
    static const struct { const char* n; WheelAxis a; } kAx[] = {
        {"X", WheelAxis::X}, {"Y", WheelAxis::Y}, {"Z", WheelAxis::Z}, {"RX", WheelAxis::RX},
        {"RY", WheelAxis::RY}, {"RZ", WheelAxis::RZ}, {"SLIDER0", WheelAxis::Slider0},
        {"SLIDER1", WheelAxis::Slider1}, {"NONE", WheelAxis::None}};
    for (auto& a : kAx) if (t == a.n) return a.a;
    LOG("config: unknown axis '%s' for %s; using %s", s.c_str(), key, defName);
    return def;
}
static const char* wheelAxisName(WheelAxis a) {
    static const char* n[] = {"X", "Y", "Z", "RX", "RY", "RZ", "SLIDER0", "SLIDER1", "NONE"};
    return n[(int)a];
}
static void readWheel(const wchar_t* ini, WheelConfig& w) {
    w.enabled        = readI(ini, "Wheel", "Enabled", 1) != 0;
    w.overrideAzimuthWheel = readI(ini, "Wheel", "OverrideAzimuthWheel", 1) != 0;
    w.deviceMatch    = readStr(ini, "Wheel", "DeviceMatch", "G29");
    w.pedalsMatch    = readStr(ini, "Wheel", "PedalsDevice", "");
    w.shifterMatch   = readStr(ini, "Wheel", "ShifterDevice", "");
    w.handbrakeMatch = readStr(ini, "Wheel", "HandbrakeDevice", "");
    LOG("config: separate devices: pedals '%s' shifter '%s' handbrake '%s' (empty = the wheel)",
        w.pedalsMatch.c_str(), w.shifterMatch.c_str(), w.handbrakeMatch.c_str());
    w.wheelRange     = readI(ini, "Wheel", "WheelRange", 900);
    if (w.wheelRange < 40 || w.wheelRange > 65535) {
        LOG("config: WheelRange=%d out of range 40..65535; using 900", w.wheelRange);
        w.wheelRange = 900;
    }
    w.steeringAxis   = readWheelAxis(ini, "SteeringAxis", WheelAxis::X, "X");
    w.steeringInvert = readI(ini, "Wheel", "SteeringInvert", 0) != 0;
    w.throttleAxis   = readWheelAxis(ini, "ThrottleAxis", WheelAxis::Y, "Y");
    w.brakeAxis      = readWheelAxis(ini, "BrakeAxis", WheelAxis::RZ, "RZ");
    w.clutchAxis     = readWheelAxis(ini, "ClutchAxis", WheelAxis::Slider0, "SLIDER0");
    w.pedalsInverted = readI(ini, "Wheel", "PedalsInverted", 1) != 0;
    w.pedalDeadzone  = (float)readF(ini, "Wheel", "PedalDeadzone", 0.02);
    if (!(w.pedalDeadzone >= 0.0f)) w.pedalDeadzone = 0.0f;
    if (w.pedalDeadzone > 0.9f) w.pedalDeadzone = 0.9f;
    w.buttonA    = readI(ini, "Wheel", "ButtonA", 0);
    w.buttonB    = readI(ini, "Wheel", "ButtonB", 2);
    w.buttonX    = readI(ini, "Wheel", "ButtonX", 1);
    w.buttonY    = readI(ini, "Wheel", "ButtonY", 3);
    w.buttonView = readI(ini, "Wheel", "ButtonView", 8);
    w.buttonMenu = readI(ini, "Wheel", "ButtonMenu", 9);
    w.buttonLB   = readI(ini, "Wheel", "ButtonLB", 5);
    w.buttonRB   = readI(ini, "Wheel", "ButtonRB", 4);
    w.hShifter   = readI(ini, "Wheel", "HShifter", 0) != 0;
    {
        static const int defGears[7] = {12, 13, 14, 15, 16, 17, -1};
        for (int i = 0; i < 7; ++i) w.gearButtons[i] = defGears[i];
        std::string g = readStr(ini, "Wheel", "GearButtons", "12,13,14,15,16,17,-1");
        const char* p = g.c_str();
        for (int i = 0; i < 7; ++i) {
            char* e = nullptr;
            long v = strtol(p, &e, 0);
            if (e == p) { LOG("config: GearButtons='%s' has fewer than 7 entries; rest use defaults", g.c_str()); break; }
            w.gearButtons[i] = (int)v;
            p = e;
            while (*p == ' ') ++p;
            if (*p == ',') ++p; else break;
        }
    }
    w.reverseButton = readI(ini, "Wheel", "ReverseButton", 18);
    w.handbrakeButton     = readI(ini, "Wheel", "HandbrakeButton", -1);
    w.handbrakeAxis       = readWheelAxis(ini, "HandbrakeAxis", WheelAxis::None, "NONE");
    w.handbrakeAxisInvert = readI(ini, "Wheel", "HandbrakeAxisInvert", 0) != 0;
    w.handbrakeThreshold  = clampCfg(ini, "HandbrakeThreshold", (float)readF(ini, "Wheel", "HandbrakeThreshold", 0.3), 0.05f, 0.95f);
    {
        std::string g = readStr(ini, "Wheel", "HandbrakeGameButton", "B"), t;
        for (char ch : g) if (ch != ' ') t += (char)toupper((unsigned char)ch);
        char c = 'B';
        if (t == "A" || t == "B" || t == "X" || t == "Y") c = t[0];
        else if (t == "LB") c = 'L';
        else if (t == "RB") c = 'R';
        else LOG("config: unknown HandbrakeGameButton '%s'; using B", g.c_str());
        w.handbrakeGameButton = c;
        LOG("config: handbrake button %d axis %s%s threshold %.2f -> game %c", w.handbrakeButton,
            wheelAxisName(w.handbrakeAxis), w.handbrakeAxisInvert ? "(inv)" : "", w.handbrakeThreshold, c);
    }
    std::string um = readStr(ini, "Wheel", "UserMode", "auto"), umt;
    for (char ch : um) if (ch != ' ') umt += (char)tolower((unsigned char)ch);
    if (umt == "none") w.userMode = WheelUserMode::None;
    else {
        if (umt != "auto") LOG("config: unknown UserMode '%s'; using auto", um.c_str());
        w.userMode = WheelUserMode::Auto;
    }
    w.forceRescan  = readI(ini, "Wheel", "ForceRescan", 1) != 0;
    w.vendorId     = readI(ini, "Wheel", "VendorId", 0x046D) & 0xFFFF;
    w.productId    = readI(ini, "Wheel", "ProductId", 0xC261) & 0xFFFF;
    w.configFlag14 = readI(ini, "Wheel", "ConfigFlag14", 1);
    w.testMode     = readI(ini, "Wheel", "TestMode", 0) != 0;
    w.ffbEnabled   = readI(ini, "Wheel", "FFB", 1) != 0;
    w.ffbGain      = (float)readF(ini, "Wheel", "FFBGain", 1.0);
    if (w.ffbGain < 0.0f || w.ffbGain > 4.0f) {
        LOG("config: FFBGain=%.2f out of range 0..4; clamping", w.ffbGain);
        w.ffbGain = w.ffbGain < 0.0f ? 0.0f : 4.0f;
    }
    w.ffbInvert    = readI(ini, "Wheel", "FFBInvert", 0) != 0;
    {
        FfbTuning& t = w.ffbTuning;
        t.torqueGain      = clampCfg(ini, "FFBTorqueGain", (float)readF(ini, "Wheel", "FFBTorqueGain", 2.0), 0.0f, 5.0f);
        t.springGain      = clampCfg(ini, "FFBSpringGain", (float)readF(ini, "Wheel", "FFBSpringGain", 1.0), 0.0f, 5.0f);
        t.slipSpringFloor = clampCfg(ini, "FFBSlipSpringFloor", (float)readF(ini, "Wheel", "FFBSlipSpringFloor", 0.4), 0.0f, 1.0f);
        t.slipGain        = clampCfg(ini, "FFBSlipGain", (float)readF(ini, "Wheel", "FFBSlipGain", 0.5), 0.0f, 5.0f);
        t.slipStart       = clampCfg(ini, "FFBSlipStart", (float)readF(ini, "Wheel", "FFBSlipStart", 0.08), 0.0f, 2.0f);
        t.slipFull        = clampCfg(ini, "FFBSlipFull", (float)readF(ini, "Wheel", "FFBSlipFull", 0.55), 0.0f, 2.0f);
        t.slipInvert      = readI(ini, "Wheel", "FFBSlipInvert", 1) != 0;
        t.alignGain       = clampCfg(ini, "FFBAlignGain", (float)readF(ini, "Wheel", "FFBAlignGain", 0.6), 0.0f, 5.0f);
        t.alignStart      = clampCfg(ini, "FFBAlignStart", (float)readF(ini, "Wheel", "FFBAlignStart", 0.05), 0.0f, 2.0f);
        t.alignFull       = clampCfg(ini, "FFBAlignFull", (float)readF(ini, "Wheel", "FFBAlignFull", 0.5), 0.0f, 2.0f);
        t.rumbleGain      = clampCfg(ini, "FFBRumbleGain", (float)readF(ini, "Wheel", "FFBRumbleGain", 1.5), 0.0f, 5.0f);
        w.ffbLockStrength = clampCfg(ini, "FFBLockStrength", (float)readF(ini, "Wheel", "FFBLockStrength", 1.0), 0.0f, 1.0f);
        w.ffbLockRampDeg  = clampCfg(ini, "FFBLockRampDeg", (float)readF(ini, "Wheel", "FFBLockRampDeg", 3.0), 0.5f, 30.0f);
        LOG("config: ffb torque %.2f spring %.2f floor %.2f slip %.2f [%.2f..%.2f] inv %d align %.2f [%.2f..%.2f] rumble %.2f lock %.2f ramp %.1f",
            t.torqueGain, t.springGain, t.slipSpringFloor, t.slipGain, t.slipStart, t.slipFull, t.slipInvert,
            t.alignGain, t.alignStart, t.alignFull, t.rumbleGain, w.ffbLockStrength, w.ffbLockRampDeg);
    }
    LOG("config: wheel enabled %d match '%s' range %d axes steer %s%s thr %s brk %s clu %s pedalsInv %d dz %.2f "
        "hshifter %d user %s rescan %d id %04x:%04x flag14 %d test %d ffb %d gain %.2f inv %d overrideAzimuth %d",
        w.enabled, w.deviceMatch.c_str(), w.wheelRange, wheelAxisName(w.steeringAxis),
        w.steeringInvert ? "(inv)" : "", wheelAxisName(w.throttleAxis), wheelAxisName(w.brakeAxis),
        wheelAxisName(w.clutchAxis), w.pedalsInverted, w.pedalDeadzone, w.hShifter,
        w.userMode == WheelUserMode::Auto ? "auto" : "none", w.forceRescan, w.vendorId, w.productId,
        w.configFlag14, w.testMode, w.ffbEnabled, w.ffbGain, w.ffbInvert, w.overrideAzimuthWheel);
}

static AssistMode toMode(CarOvr o, bool& set) {
    set = (o == CarOvr::On || o == CarOvr::Off);
    return o == CarOvr::On ? AssistMode::On : o == CarOvr::Off ? AssistMode::Off : AssistMode::Game;
}

// [Cars] section: key = car Id, value = "ABS,TC,STM,CLUTCH", each on|off|auto (missing fields = auto).
// For CLUTCH, on = the car has a clutch, off = it has none, automatic = always shift automatically.
static void loadCarOverrides(const std::wstring& path, std::vector<CarAssistOverride>& out) {
    if (GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES) return;
    std::vector<wchar_t> buf(32768);
    DWORD n = GetPrivateProfileSectionW(L"Cars", buf.data(), (DWORD)buf.size(), path.c_str());
    for (const wchar_t* p = buf.data(); n && *p; p += wcslen(p) + 1) {
        std::wstring line = p;
        if (line.find(L'=') == std::wstring::npos) continue;
        CarAssistLine l;
        if (!ParseCarAssistLine(line, l)) { LOG("config: bad car id in %ls", line.c_str()); continue; }
        CarAssistOverride o;
        o.id = l.id;
        o.abs = toMode(l.abs, o.absSet);
        o.tc = toMode(l.tc, o.tcSet);
        o.stm = toMode(l.stm, o.stmSet);
        if (l.clutch == CarOvr::Automatic) { o.clutchSet = true; o.clutch = AssistMode::Off; o.forceAutomatic = true; }
        else o.clutch = toMode(l.clutch, o.clutchSet);
        if (o.absSet || o.tcSet || o.stmSet || o.clutchSet) out.push_back(o);
    }
}

// [SteeringLock] section: key = car Id, value = lock to lock in degrees (90..3600).
static void loadSteeringLocks(const std::wstring& path, std::vector<std::pair<unsigned, unsigned>>& out) {
    if (GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES) return;
    std::vector<wchar_t> buf(32768);
    DWORD n = GetPrivateProfileSectionW(L"SteeringLock", buf.data(), (DWORD)buf.size(), path.c_str());
    for (const wchar_t* p = buf.data(); n && *p; p += wcslen(p) + 1) {
        std::wstring line = p;
        unsigned id = 0, deg = 0;
        bool ok = ParseSteeringLockLine(line, id, deg);
        if (!ok) { LOG("config: bad [SteeringLock] line '%ls' (want CarId=90..3600); ignored", line.c_str()); continue; }
        bool dup = false;
        for (auto& e : out) if (e.first == id) dup = true;   // first line of a duplicated id wins, as for [Cars]
        if (!dup) out.push_back({id, deg});
    }
}

Config loadConfig(const std::wstring& iniPath) {
    int kept = 0;
    bool created = false;
    if (EnsureCurrentIni(iniPath, &kept, &created))
        LOG("config: settings from an older version saved as FH2SimImmersion.ini.old.bak; %d values carried over", kept);
    if (created) LOG("config: wrote default settings file");
    const wchar_t* ini = iniPath.c_str();
    Config c;
    std::string src = readStr(ini, "Input", "Source", "auto");
    c.source = src == "udp" ? Source::Udp : src == "freetrack" ? Source::FreeTrack : Source::Auto;
    c.diagnostics     = readI(ini, "Input", "Diagnostics", 0) != 0;
    c.udpPort         = readI(ini, "Network", "UdpPort", 4242);
    if (c.udpPort < 1 || c.udpPort > 65535) {
        LOG("config: UdpPort=%d out of range 1..65535; using 4242", c.udpPort);
        c.udpPort = 4242;
    }
    c.bindAddress     = readStr(ini, "Network", "BindAddress", "127.0.0.1");
    c.enableOnStartup = readI(ini, "General", "EnableOnStartup", 1) != 0;
    c.overrideAzimuthHeadTracking = readI(ini, "General", "OverrideAzimuthHeadTracking", 1) != 0;
    c.cockpitOnly     = readI(ini, "General", "CockpitOnly", 1) != 0;
    c.staleTimeoutMs  = readI(ini, "General", "StaleTimeoutMs", 500);
    if (c.staleTimeoutMs < 50 || c.staleTimeoutMs > 60000) {
        int v = c.staleTimeoutMs < 50 ? 50 : 60000;
        LOG("config: StaleTimeoutMs=%d out of range 50..60000; using %d", c.staleTimeoutMs, v);
        c.staleTimeoutMs = v;
    }
    c.smoothing       = readF(ini, "General", "Smoothing", 0.0);
    c.toggleKey        = readI(ini, "Hotkeys", "ToggleKey", VK_END);
    c.recenterKey      = readI(ini, "Hotkeys", "RecenterKey", 0);
    c.chordToggleKey   = readI(ini, "Hotkeys", "ChordToggleKey", 'Y');
    c.chordRecenterKey = readI(ini, "Hotkeys", "ChordRecenterKey", 0);
    c.recenterButtons  = parseButtons(readStr(ini, "Hotkeys", "RecenterButtons", "L3+R3"));
    c.wheelRecenterCount = parseWheelButtons(readStr(ini, "Hotkeys", "WheelRecenterButtons", "4+5"), c.wheelRecenterButtons);
    c.map.yaw   = readAxis(ini, "Rotation", "Yaw", 90.0f);
    c.map.pitch = readAxis(ini, "Rotation", "Pitch", 40.0f);
    c.map.roll  = readAxis(ini, "Rotation", "Roll", 20.0f);
    c.map.positionEnabled = readI(ini, "Position", "Enabled", 1) != 0;
    c.map.x = readAxis(ini, "Position", "X", 0.10f);
    c.map.y = readAxis(ini, "Position", "Y", 0.06f);
    c.map.z = readAxis(ini, "Position", "Z", 0.08f);
    c.map.yawLeftGain  = clampCfg(ini, "YawLeftGain", (float)readF(ini, "Rotation", "YawLeftGain", 1.0), 0.0f, 5.0f);
    c.map.yawRightGain = clampCfg(ini, "YawRightGain", (float)readF(ini, "Rotation", "YawRightGain", 1.0), 0.0f, 5.0f);
    c.map.yawLeftIsPositive = readI(ini, "Rotation", "YawLeftIsPositive", 1) != 0;
    readOrder(ini, "RotOrder", c.map.rotOrder);
    readOrder(ini, "PosOrder", c.map.posOrder);
    LOG("config: source %s diagnostics %d recenter buttons 0x%04x", src.c_str(), c.diagnostics, c.recenterButtons);
    {
        std::string wb;
        for (int k = 0; k < c.wheelRecenterCount; ++k) wb += (k ? "+" : "") + std::to_string(c.wheelRecenterButtons[k]);
        LOG("config: wheel recenter buttons %s", wb.empty() ? "none" : wb.c_str());
    }
    LOG("config: port %d bind %s enabled %d cockpitOnly %d smoothing %.2f overrideAzimuthHT %d",
        c.udpPort, c.bindAddress.c_str(), c.enableOnStartup, c.cockpitOnly, c.smoothing, c.overrideAzimuthHeadTracking);
    LOG("config: yaw side gains left %.2f right %.2f leftIsPositive %d", c.map.yawLeftGain, c.map.yawRightGain,
        c.map.yawLeftIsPositive);
    LOG("config: yaw g%.2f i%d l%.0f  pitch g%.2f i%d l%.0f  roll g%.2f i%d l%.0f",
        c.map.yaw.gain, c.map.yaw.invert, c.map.yaw.limit, c.map.pitch.gain, c.map.pitch.invert,
        c.map.pitch.limit, c.map.roll.gain, c.map.roll.invert, c.map.roll.limit);
    LOG("config: pos %d  x g%.2f i%d l%.2f  y g%.2f i%d l%.2f  z g%.2f i%d l%.2f",
        c.map.positionEnabled, c.map.x.gain, c.map.x.invert, c.map.x.limit, c.map.y.gain,
        c.map.y.invert, c.map.y.limit, c.map.z.gain, c.map.z.invert, c.map.z.limit);
    readWheel(ini, c.wheel);
    c.sim.fovCockpit = clampCfg(ini, "FovCockpit", (float)readF(ini, "Simulation", "FovCockpit", 0.0), -30.0f, 60.0f);
    c.sim.fovDriver  = clampCfg(ini, "FovDriver", (float)readF(ini, "Simulation", "FovDriver", 0.0), -30.0f, 60.0f);
    c.sim.fovHood    = clampCfg(ini, "FovHood", (float)readF(ini, "Simulation", "FovHood", 0.0), -30.0f, 60.0f);
    c.sim.fovBumper  = clampCfg(ini, "FovBumper", (float)readF(ini, "Simulation", "FovBumper", 0.0), -30.0f, 60.0f);
    LOG("config: fov cockpit %.1f driver %.1f hood %.1f bumper %.1f", c.sim.fovCockpit, c.sim.fovDriver,
        c.sim.fovHood, c.sim.fovBumper);
    c.sim.realisticAbs     = readI(ini, "Simulation", "RealisticABS", 0) != 0;
    c.sim.realisticTc      = readI(ini, "Simulation", "RealisticTC", 0) != 0;
    std::string opt        = readStr(ini, "Simulation", "OptionalAssists", "game");
    for (char& ch : opt) ch = (char)tolower((unsigned char)ch);
    c.sim.optionalAssists  = opt == "on" ? AssistMode::On : opt == "off" ? AssistMode::Off : AssistMode::Game;
    c.sim.realisticStm     = readI(ini, "Simulation", "RealisticSTM", 0) != 0;
    c.sim.realisticClutch  = readI(ini, "Simulation", "RealisticClutch", 0) != 0;
    c.sim.realisticAutomatic = readI(ini, "Simulation", "RealisticAutomatic", 0) != 0;
    LOG("config: realistic abs %d tc %d stm %d clutch %d automatic %d optional %s", c.sim.realisticAbs, c.sim.realisticTc, c.sim.realisticStm,
        c.sim.realisticClutch, c.sim.realisticAutomatic,
        c.sim.optionalAssists == AssistMode::On ? "on" : c.sim.optionalAssists == AssistMode::Off ? "off" : "game");
    {
        SimConfig& m = c.sim;
        m.realisticSteeringLock = readI(ini, "Simulation", "RealisticSteeringLock", 0) != 0;
        int maxRot = readI(ini, "Simulation", "MaxWheelRotation", 0);
        if (maxRot != 0 && maxRot < 90) { LOG("config: MaxWheelRotation=%d below 90; using 90", maxRot); maxRot = 90; }
        if (maxRot > c.wheel.wheelRange) {
            LOG("config: MaxWheelRotation=%d above WheelRange %d; using WheelRange", maxRot, c.wheel.wheelRange);
            maxRot = c.wheel.wheelRange;
        }
        m.maxWheelRotation = maxRot;
        std::string mode = readStr(ini, "Simulation", "SteeringLockMode", "cap"), mt;
        for (char ch : mode) if (ch != ' ') mt += (char)tolower((unsigned char)ch);
        if (mt == "scale") m.lockScale = true;
        else {
            if (mt != "cap") LOG("config: unknown SteeringLockMode '%s'; using cap", mode.c_str());
            m.lockScale = false;
        }
        m.realCockpitWheel = readI(ini, "Simulation", "RealCockpitWheel", 0) != 0;
        m.hideDriverInCockpit = readI(ini, "Simulation", "HideDriverInCockpit", 0) != 0;
        int dir = readI(ini, "Simulation", "CockpitWheelDirection", 1);
        if (dir != 1 && dir != -1) { LOG("config: CockpitWheelDirection=%d is not 1 or -1; using 1", dir); dir = 1; }
        m.cockpitWheelDirection = dir;
        LOG("config: steering lock realistic %d max %d mode %s cockpitWheel %d dir %d hideDriver %d", m.realisticSteeringLock,
            m.maxWheelRotation, m.lockScale ? "scale" : "cap", m.realCockpitWheel, m.cockpitWheelDirection, m.hideDriverInCockpit);
    }
    {
        DrawDistConfig& d = c.drawDist;
        d.shadow  = clampCfg(ini, "ShadowDistance", (float)readF(ini, "DrawDistance", "ShadowDistance", 0.0), 0.0f, 1000.0f);
        d.world   = clampCfg(ini, "WorldDistance", (float)readF(ini, "DrawDistance", "WorldDistance", 0.0), 0.0f, 3000.0f);
        d.props   = clampCfg(ini, "PropsDistance", (float)readF(ini, "DrawDistance", "PropsDistance", 0.0), 0.0f, 3000.0f);
        d.crowd   = clampCfg(ini, "CrowdDistance", (float)readF(ini, "DrawDistance", "CrowdDistance", 0.0), 0.0f, 1000.0f);
        d.grass   = clampCfg(ini, "GrassDistance", (float)readF(ini, "DrawDistance", "GrassDistance", 0.0), 0.0f, 500.0f);
        d.traffic = clampCfg(ini, "TrafficDistance", (float)readF(ini, "DrawDistance", "TrafficDistance", 0.0), 0.0f, 500.0f);
        d.racer   = clampCfg(ini, "RacerDistance", (float)readF(ini, "DrawDistance", "RacerDistance", 0.0), 0.0f, 1000.0f);
        LOG("config: drawdist shadow %.0f world %.0f props %.0f crowd %.0f grass %.0f traffic %.0f racer %.0f",
            d.shadow, d.world, d.props, d.crowd, d.grass, d.traffic, d.racer);
    }
    {
        std::wstring cars = iniPath;
        size_t sep = cars.find_last_of(L"\\/");
        cars = (sep == std::wstring::npos ? std::wstring() : cars.substr(0, sep + 1)) + L"FH2SimImmersion_cars.ini";
        loadCarOverrides(cars, c.sim.carOverrides);
        LOG("config: %d per-car assist overrides", (int)c.sim.carOverrides.size());
        loadSteeringLocks(cars, c.sim.steeringLocks);
        LOG("config: %d per-car steering lock overrides", (int)c.sim.steeringLocks.size());
    }
    return c;
}

} // namespace fh2
