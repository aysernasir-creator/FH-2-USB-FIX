// SPDX-License-Identifier: MIT
// Tab 1: head tracking.
#include "pages.h"
#include "draw.h"
#include "settings_tables.h"

namespace app {

class HeadPage : public Page {
public:
    using Page::Page;
    const wchar_t* Title() const override { return L"Head tracking"; }
    void PaintPicture(HDC dc, const RECT& rc) override {
        HeadPicState st;
        st.part = part_;
        st.yawLim = Num("Rotation", "YawLimit", 90);
        st.pitchLim = Num("Rotation", "PitchLimit", 40);
        st.rollLim = Num("Rotation", "RollLimit", 20);
        st.xLim = Num("Position", "XLimit", 0.10);
        st.yLim = Num("Position", "YLimit", 0.06);
        st.zLim = Num("Position", "ZLimit", 0.08);
        DrawHeadPicture(dc, rc, st, (int)G.dpi, G.font);
    }
protected:
    void Build() override {
        BuildRows(kHeadSettings, sizeof(kHeadSettings) / sizeof(Setting));
        MakePicture();
    }
    void Layout(int w, int h) override {
        int lw = w * 54 / 100, gap = S(8);
        SetWindowPos(scroller_, nullptr, 0, 0, lw, h, SWP_NOZORDER);
        SetWindowPos(picture_, nullptr, lw + gap, 0, w - lw - gap, h, SWP_NOZORDER);
        LayoutRows();
    }
    void OnChanged(Ctl&) override { if (picture_) InvalidateRect(picture_, nullptr, FALSE); }
};

Page* NewHeadPage(int index) { return new HeadPage(index); }

} // namespace app
