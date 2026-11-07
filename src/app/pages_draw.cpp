// SPDX-License-Identifier: MIT
// Tab 4: draw distance. One slider + edit (metres) per distance. The slider runs from the game's own
// value (far left = 0 in the INI = "game default") to the maximum; a top-down picture shows the arcs.
#include "pages.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include "draw.h"
#include "ini.h"
#include "settings_tables.h"

namespace app {

namespace {
struct Range { const char* key; int def; };
const Range kRanges[] = {
    {"ShadowDistance", 85}, {"WorldDistance", 360}, {"PropsDistance", 450}, {"CrowdDistance", 250},
    {"GrassDistance", 100}, {"TrafficDistance", 150}, {"RacerDistance", 75},
};
const wchar_t* kGameDefault = L"game default";

int gameDefault(const char* key) {
    for (auto& r : kRanges) if (!strcmp(r.key, key)) return r.def;
    return 0;
}
}

class DrawDistPage : public Page {
public:
    using Page::Page;
    const wchar_t* Title() const override { return L"Draw distance"; }

    void PaintPicture(HDC dc, const RECT& rc) override {
        DrawDistPicState st;
        st.part = part_;
        st.shadow = Eff("ShadowDistance");   st.world = Eff("WorldDistance");   st.props = Eff("PropsDistance");
        st.crowd = Eff("CrowdDistance");     st.grass = Eff("GrassDistance");   st.traffic = Eff("TrafficDistance");
        st.racer = Eff("RacerDistance");
        DrawDrawDistPicture(dc, rc, st, (int)G.dpi, G.font);
    }

protected:
    void Build() override {
        sliderEditW_ = 84;
        BuildRows(kDrawDistSettings, sizeof(kDrawDistSettings) / sizeof(Setting));
        for (auto& c : ctls_) {         // the slider starts at the game's own value
            int def = gameDefault(c.s->key);
            SendMessageW(c.slider, TBM_SETRANGE, TRUE, MAKELONG(def, (int)c.s->max));
            SendMessageW(c.slider, TBM_SETPAGESIZE, 0, (LPARAM)std::max(1, ((int)c.s->max - def) / 10));
            SendMessageW(c.slider, TBM_SETLINESIZE, 0, (LPARAM)5);
        }
        hint_ = CreateWindowExW(0, L"STATIC",
            L"The game has no separate terrain or city distances: everything in the scenery (terrain, buildings, trees) "
            L"is culled by the one \"World objects\" distance. Props that can be knocked over (fences, signs, ...) have "
            L"their own distance. Racer cars set the distance of their farthest detail level; the nearer levels scale with it.\r\n\r\n"
            L"Each slider starts at the game's own value. Far left = \"game default\" (the setting is not changed). "
            L"Higher values draw more and cost frame rate; world objects and shadows cost the most. "
            L"Values are applied the next time the game starts.",
            WS_CHILD | WS_VISIBLE | SS_LEFT | SS_NOPREFIX, 0, 0, 10, 10, host_, nullptr, G.inst, nullptr);
        MakePicture();
    }

    void Layout(int w, int h) override {
        int lw = w * 54 / 100, gap = S(8), hintH = S(150);
        SetWindowPos(scroller_, nullptr, 0, 0, lw, h - hintH - S(6), SWP_NOZORDER);
        SetWindowPos(hint_, nullptr, S(6), h - hintH, lw - S(12), hintH, SWP_NOZORDER);
        SetWindowPos(picture_, nullptr, lw + gap, 0, w - lw - gap, h, SWP_NOZORDER);
        LayoutRows();
    }

    std::string GetValue(Ctl& c) override {
        wchar_t buf[128];
        GetWindowTextW(c.main, buf, 128);
        std::string t = trimStr(Narrow(buf));
        if (t == Narrow(kGameDefault)) return "0";
        char* end = nullptr;
        double v = strtod(t.c_str(), &end);
        if (t.empty() || (end && *end)) return c.shown.empty() ? c.loaded : c.shown;     // not a number: keep
        if (v <= gameDefault(c.s->key)) return "0";                                      // not above the game's value
        v = std::min(v, c.s->max);
        return std::to_string((long)std::lround(v));
    }

    void SetValue(Ctl& c, const std::string& v) override {
        bool was = loading_;
        loading_ = true;
        int def = gameDefault(c.s->key);
        double d = strtod(v.c_str(), nullptr);
        if (!(d > def)) {
            SetWindowTextW(c.main, kGameDefault);
            SendMessageW(c.slider, TBM_SETPOS, TRUE, def);
        } else {
            d = std::min(d, c.s->max);
            SetWindowTextW(c.main, std::to_wstring((long)std::lround(d)).c_str());
            SendMessageW(c.slider, TBM_SETPOS, TRUE, (LPARAM)std::lround(d));
        }
        loading_ = was;
        OnChanged(c);
    }

    void OnChanged(Ctl& c) override {
        // Slider moved to the far left: the value is the game default.
        if (!loading_ && GetFocus() == c.slider && (int)SendMessageW(c.slider, TBM_GETPOS, 0, 0) <= gameDefault(c.s->key)) {
            loading_ = true;
            SetWindowTextW(c.main, kGameDefault);
            loading_ = false;
        }
        if (picture_) InvalidateRect(picture_, nullptr, FALSE);
    }

private:
    HWND hint_ = nullptr;

    // distance in metres that is in effect for a setting (the game's own value when it is the game default)
    double Eff(const char* key) {
        Ctl* c = Find("DrawDistance", key);
        if (!c) return gameDefault(key);
        double v = strtod(GetValue(*c).c_str(), nullptr);
        return v > 0 ? v : gameDefault(key);
    }
};

Page* NewDrawDistPage(int index) { return new DrawDistPage(index); }

} // namespace app
