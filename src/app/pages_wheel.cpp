// SPDX-License-Identifier: MIT
// Tab 2: steering wheel, with an optional live DirectInput test.
#define DIRECTINPUT_VERSION 0x0800
#include "pages.h"
#include <dinput.h>
#include <algorithm>
#include <cwctype>
#include "draw.h"
#include "ini.h"
#include "settings_tables.h"

namespace app {

namespace {
const int kIdTest = 9100;

struct FoundDev { GUID guid; std::wstring name; bool driving; };

BOOL CALLBACK enumCb(const DIDEVICEINSTANCEW* di, void* ctx) {
    auto* v = (std::vector<FoundDev>*)ctx;
    v->push_back({di->guidInstance, di->tszProductName, GET_DIDEVICE_TYPE(di->dwDevType) == DI8DEVTYPE_DRIVING});
    return DIENUM_CONTINUE;
}
std::wstring lowerW(std::wstring s) { for (auto& c : s) c = (wchar_t)towlower(c); return s; }
}

class WheelPage : public Page {
public:
    using Page::Page;
    ~WheelPage() override { StopTest(); }
    const wchar_t* Title() const override { return L"Wheel"; }

    void OnShow(bool visible) override { if (!visible) StopTest(); }

    void Tick() override {
        Page::Tick();
        if (!dev_) return;
        DIJOYSTATE2 js{};
        HRESULT hr = dev_->Poll();
        if (FAILED(hr)) { dev_->Acquire(); dev_->Poll(); }
        hr = dev_->GetDeviceState(sizeof(js), &js);
        if (FAILED(hr)) {
            if (hr == DIERR_INPUTLOST || hr == DIERR_NOTACQUIRED) dev_->Acquire();
            return;
        }
        live_.active = true;
        live_.steer = axis(js, "SteeringAxis", false);
        if (steerInvert()) live_.steer = 1.0 - live_.steer;
        double dz = Num("Wheel", "PedalDeadzone", 0.02);
        live_.throttle = pedal(axis(js, "ThrottleAxis", true), dz);
        live_.brake = pedal(axis(js, "BrakeAxis", true), dz);
        live_.clutch = pedal(axis(js, "ClutchAxis", true), dz);
        live_.pressed.clear();
        for (int i = 0; i < 128; ++i) if (js.rgbButtons[i] & 0x80) live_.pressed.push_back(i);
        {
            double hb = 0;
            Ctl* hbA = Find("Wheel", "HandbrakeAxis");
            if (hbA && GetValue(*hbA) != "NONE" && !GetValue(*hbA).empty()) {
                hb = axis(js, "HandbrakeAxis", true);
                Ctl* inv = Find("Wheel", "HandbrakeAxisInvert");
                if (inv && GetValue(*inv) == "1") hb = 1.0 - hb;
            }
            int hbBtn = (int)Num("Wheel", "HandbrakeButton", -1);
            if (hbBtn >= 0 && hbBtn < 128 && (js.rgbButtons[hbBtn] & 0x80)) hb = 1.0;
            live_.handbrake = hb;
        }
        if (picture_) InvalidateRect(picture_, nullptr, FALSE);
    }

    void PaintPicture(HDC dc, const RECT& rc) override {
        WheelPicState st;
        st.part = part_;
        st.live = live_;
        st.rangeDeg = Num("Wheel", "WheelRange", 900);
        static const struct { const char* key; const char* part; bool valueOnly; } kMap[] = {
            {"SteeringAxis", "steer", false}, {"ThrottleAxis", "throttle", true}, {"BrakeAxis", "brake", true},
            {"ClutchAxis", "clutch", true}, {"ButtonA", "btnA", false}, {"ButtonB", "btnB", false},
            {"ButtonX", "btnX", false}, {"ButtonY", "btnY", false}, {"ButtonView", "view", false},
            {"ButtonMenu", "menu", false}, {"ButtonLB", "lb", false}, {"ButtonRB", "rb", false},
            {"HShifter", "shifter", false}, {"FFB", "ffb", false}};
        for (auto& m : kMap) {
            Ctl* c = Find("Wheel", m.key);
            if (!c) continue;
            std::string v = GetValue(*c);
            if (c->s->type == SType::Enum && v.empty()) v = "?";
            st.label[m.part] = Widen(m.valueOnly ? v : std::string(m.key) + "=" + v);
        }
        {   // handbrake label: button and/or axis
            std::wstring t;
            Ctl* b = Find("Wheel", "HandbrakeButton");
            Ctl* a = Find("Wheel", "HandbrakeAxis");
            std::string bv = b ? GetValue(*b) : "", av = a ? GetValue(*a) : "";
            if (!bv.empty() && bv != "-1") t = L"button " + Widen(bv);
            if (!av.empty() && av != "NONE") t += (t.empty() ? L"axis " : L" / axis ") + Widen(av);
            st.label["handbrake"] = t.empty() ? L"not set" : t;
        }
        DrawWheelPicture(dc, rc, st, (int)G.dpi, G.font);
    }

protected:
    void Build() override {
        test_ = CreateWindowExW(0, L"BUTTON", L"Test input (live wheel axes and button numbers)", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
                                0, 0, 10, 10, host_, (HMENU)(INT_PTR)kIdTest, G.inst, nullptr);
        AddTip(test_, L"Opens the wheel selected by \"Device name contains\" with DirectInput (shared, in the background) "
                      L"and shows its axes and the numbers of the pressed buttons in the picture. Use it to find the numbers "
                      L"for the button settings. Stops when unchecked or when you leave this tab.");
        BuildRows(kWheelSettings, sizeof(kWheelSettings) / sizeof(Setting));
        MakePicture();
    }
    void Layout(int w, int h) override {
        int lw = w * 54 / 100, gap = S(8), top = S(28);
        SetWindowPos(test_, nullptr, S(8), S(2), lw - S(8), S(24), SWP_NOZORDER);
        SetWindowPos(scroller_, nullptr, 0, top, lw, h - top, SWP_NOZORDER);
        SetWindowPos(picture_, nullptr, lw + gap, 0, w - lw - gap, h, SWP_NOZORDER);
        LayoutRows();
    }
    void OnChanged(Ctl&) override { if (picture_) InvalidateRect(picture_, nullptr, FALSE); }
    void OnCommand(int id, int code, HWND) override {
        if (id == kIdTest && code == BN_CLICKED) {
            if (SendMessageW(test_, BM_GETCHECK, 0, 0) == BST_CHECKED) StartTest(); else StopTest();
        }
    }

private:
    HWND test_ = nullptr;
    IDirectInput8W* di_ = nullptr;
    IDirectInputDevice8W* dev_ = nullptr;
    WheelLive live_;

    bool steerInvert() { Ctl* c = Find("Wheel", "SteeringInvert"); return c && GetValue(*c) == "1"; }

    // normalised 0..1 reading of the axis chosen in the combo box of `key`
    double axis(const DIJOYSTATE2& js, const char* key, bool pedalAxis) {
        Ctl* c = Find("Wheel", key);
        std::string a = c ? GetValue(*c) : "";
        LONG v = 32768;
        if (a == "X") v = js.lX; else if (a == "Y") v = js.lY; else if (a == "Z") v = js.lZ;
        else if (a == "RX") v = js.lRx; else if (a == "RY") v = js.lRy; else if (a == "RZ") v = js.lRz;
        else if (a == "SLIDER0") v = js.rglSlider[0]; else if (a == "SLIDER1") v = js.rglSlider[1];
        else return pedalAxis ? 0.0 : 0.5;
        return std::min(std::max(v / 65535.0, 0.0), 1.0);
    }
    double pedal(double raw, double deadzone) {
        Ctl* c = Find("Wheel", "PedalsInverted");
        double v = (c && GetValue(*c) == "1") ? 1.0 - raw : raw;
        if (v < deadzone) return 0.0;
        return std::min((v - deadzone) / std::max(1.0 - deadzone, 0.05), 1.0);
    }

    void StartTest() {
        StopTest();
        live_ = WheelLive();
        live_.device = L"searching...";
        HRESULT hr = DirectInput8Create(G.inst, DIRECTINPUT_VERSION, IID_IDirectInput8W, (void**)&di_, nullptr);
        if (FAILED(hr) || !di_) { Fail(L"DirectInput is not available"); return; }
        std::vector<FoundDev> devs;
        di_->EnumDevices(DI8DEVCLASS_GAMECTRL, enumCb, &devs, DIEDFL_ATTACHEDONLY);
        std::wstring match;
        if (Ctl* c = Find("Wheel", "DeviceMatch")) match = lowerW(Widen(GetValue(*c)));
        // same rule as the plugin (wheel_device.cpp): name match, else the first wheel
        const FoundDev* pick = nullptr;
        if (!match.empty())
            for (auto& d : devs) if (lowerW(d.name).find(match) != std::wstring::npos) { pick = &d; break; }
        if (!pick) for (auto& d : devs) if (d.driving) { pick = &d; break; }
        if (!pick) { Fail(devs.empty() ? L"no game controller found" : L"no wheel found (the plugin would not use one either)"); return; }
        hr = di_->CreateDevice(pick->guid, &dev_, nullptr);
        if (FAILED(hr) || !dev_) { Fail(L"could not open the device"); return; }
        dev_->SetDataFormat(&c_dfDIJoystick2);
        dev_->SetCooperativeLevel(G.main, DISCL_BACKGROUND | DISCL_NONEXCLUSIVE);
        dev_->Acquire();
        live_.active = true;
        live_.device = pick->name;
        SetStatus(L"Testing \"" + pick->name + L"\". Move the wheel and press buttons.");
    }
    void Fail(const std::wstring& why) {
        StopTest();
        live_ = WheelLive();
        live_.active = true;
        live_.device = why;
        if (test_) SendMessageW(test_, BM_SETCHECK, BST_UNCHECKED, 0);
        SetStatus(L"Test input: " + why + L".");
        if (picture_) InvalidateRect(picture_, nullptr, FALSE);
        live_.active = false;
    }
    void StopTest() {
        if (dev_) { dev_->Unacquire(); dev_->Release(); dev_ = nullptr; }
        if (di_) { di_->Release(); di_ = nullptr; }
        live_ = WheelLive();
        if (test_) SendMessageW(test_, BM_SETCHECK, BST_UNCHECKED, 0);
        if (picture_) InvalidateRect(picture_, nullptr, FALSE);
    }
};

Page* NewWheelPage(int index) { return new WheelPage(index); }

} // namespace app
