// SPDX-License-Identifier: MIT
// Base class of the tab pages (see page.h).
#include "page.h"
#include "ini.h"
#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <algorithm>

namespace app {

Globals G;

static LRESULT CALLBACK PanelWndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m == WM_NCCREATE) {
        SetWindowLongPtrW(h, GWLP_USERDATA, (LONG_PTR)((CREATESTRUCTW*)l)->lpCreateParams);
        return DefWindowProcW(h, m, w, l);
    }
    Page* p = (Page*)GetWindowLongPtrW(h, GWLP_USERDATA);
    return p ? p->PanelProc(h, m, w, l) : DefWindowProcW(h, m, w, l);
}

static LRESULT CALLBACK PictureWndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m == WM_NCCREATE) {
        SetWindowLongPtrW(h, GWLP_USERDATA, (LONG_PTR)((CREATESTRUCTW*)l)->lpCreateParams);
        return DefWindowProcW(h, m, w, l);
    }
    Page* p = (Page*)GetWindowLongPtrW(h, GWLP_USERDATA);
    if (m == WM_ERASEBKGND) return 1;
    if (m == WM_PAINT) {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(h, &ps);
        RECT rc; GetClientRect(h, &rc);
        HDC mem = CreateCompatibleDC(dc);
        HBITMAP bmp = CreateCompatibleBitmap(dc, rc.right, rc.bottom);
        HGDIOBJ old = SelectObject(mem, bmp);
        FillRect(mem, &rc, G.white);
        if (p) p->PaintPicture(mem, rc);
        BitBlt(dc, 0, 0, rc.right, rc.bottom, mem, 0, 0, SRCCOPY);
        SelectObject(mem, old); DeleteObject(bmp); DeleteDC(mem);
        EndPaint(h, &ps);
        return 0;
    }
    return DefWindowProcW(h, m, w, l);
}

void RegisterPageClasses() {
    WNDCLASSW wc{};
    wc.hInstance = G.inst;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);       // both classes paint the window colour
    wc.lpfnWndProc = PanelWndProc;
    wc.lpszClassName = L"FH2Panel";
    RegisterClassW(&wc);
    wc.lpfnWndProc = PictureWndProc;
    wc.lpszClassName = L"FH2Picture";
    RegisterClassW(&wc);
}

// ---------------------------------------------------------------- helpers

static std::vector<std::string> splitBar(const char* s) {
    std::vector<std::string> out;
    std::string cur;
    for (; s && *s; ++s) { if (*s == '|') { out.push_back(cur); cur.clear(); } else cur += *s; }
    out.push_back(cur);
    return out;
}
static std::string lower(std::string s) { for (char& c : s) c = (char)tolower((unsigned char)c); return s; }

static int decimalsFor(double step) {
    int d = 0;
    while (d < 4 && std::fabs(step * std::pow(10.0, d) - std::round(step * std::pow(10.0, d))) > 1e-6) ++d;
    return d;
}
static std::string fmtNum(double v, int dec) {
    char b[64];
    snprintf(b, sizeof(b), "%.*f", dec, v);
    return b;
}

// ---------------------------------------------------------------- Page

void Page::Create(HWND parent) {
    host_ = CreateWindowExW(WS_EX_CONTROLPARENT, L"FH2Panel", L"", WS_CHILD | WS_CLIPCHILDREN | WS_CLIPSIBLINGS,
                            0, 0, 10, 10, parent, nullptr, G.inst, this);
    tips_ = CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASSW, nullptr, WS_POPUP | TTS_ALWAYSTIP | TTS_NOPREFIX,
                            0, 0, 0, 0, host_, nullptr, G.inst, nullptr);
    SendMessageW(tips_, TTM_SETMAXTIPWIDTH, 0, S(420));
    SendMessageW(tips_, TTM_SETDELAYTIME, TTDT_AUTOPOP, 20000);
    Build();
    ApplyFonts();
}

void Page::Place(const RECT& rc) {
    SetWindowPos(host_, nullptr, rc.left, rc.top, rc.right - rc.left, rc.bottom - rc.top,
                 SWP_NOZORDER | SWP_NOACTIVATE);
    Layout(rc.right - rc.left, rc.bottom - rc.top);
}

void Page::OnDpi() {
    SendMessageW(tips_, TTM_SETMAXTIPWIDTH, 0, S(420));
    ApplyFonts();
    RECT rc; GetClientRect(host_, &rc);
    Layout(rc.right, rc.bottom);
}

void Page::ApplyFonts() {
    EnumChildWindows(host_, [](HWND c, LPARAM) -> BOOL {
        SendMessageW(c, WM_SETFONT, (WPARAM)G.font, TRUE);
        return TRUE;
    }, 0);
    for (HWND h : headers_) SendMessageW(h, WM_SETFONT, (WPARAM)G.bold, TRUE);
}

HWND Page::MakeStatic(HWND parent, const wchar_t* text, int id) {
    return CreateWindowExW(0, L"STATIC", text, WS_CHILD | WS_VISIBLE | SS_LEFT | SS_NOPREFIX | SS_CENTERIMAGE,
                           0, 0, 10, 10, parent, id >= 0 ? (HMENU)(INT_PTR)id : nullptr, G.inst, nullptr);
}

HWND Page::MakePicture() {
    picture_ = CreateWindowExW(WS_EX_CLIENTEDGE, L"FH2Picture", L"", WS_CHILD | WS_VISIBLE, 0, 0, 10, 10,
                               host_, nullptr, G.inst, this);
    return picture_;
}

void Page::AddTip(HWND w, const std::wstring& text) {
    if (!w || text.empty()) return;
    tipText_.push_back(text);
    TTTOOLINFOW ti{};
    ti.cbSize = sizeof(ti);
    ti.uFlags = TTF_IDISHWND | TTF_SUBCLASS;
    ti.hwnd = host_;
    ti.uId = (UINT_PTR)w;
    ti.lpszText = const_cast<wchar_t*>(tipText_.back().c_str());
    SendMessageW(tips_, TTM_ADDTOOLW, 0, (LPARAM)&ti);
}

// Combo boxes and trackbars must not change value on the mouse wheel: the page scrolls instead.
// dwRef = the window that receives the wheel message (the page's scroller).
static LRESULT CALLBACK NoWheelProc(HWND h, UINT m, WPARAM w, LPARAM l, UINT_PTR, DWORD_PTR dwRef) {
    if (m == WM_MOUSEWHEEL || m == WM_MOUSEHWHEEL) {
        HWND combo = h;
        wchar_t cls[32] = L"";
        GetClassNameW(h, cls, 32);
        if (_wcsicmp(cls, L"COMBOBOX") != 0) combo = GetParent(h);      // the edit child of a dropdown combo
        bool combo_ = combo && GetClassNameW(combo, cls, 32) && _wcsicmp(cls, L"COMBOBOX") == 0;
        if (!combo_ || !SendMessageW(combo, CB_GETDROPPEDSTATE, 0, 0)) {
            SendMessageW((HWND)dwRef, m, w, l);
            return 0;
        }
    }
    return DefSubclassProc(h, m, w, l);
}
static void noWheel(HWND w, HWND target) {
    if (!w) return;
    SetWindowSubclass(w, NoWheelProc, 1, (DWORD_PTR)target);
    for (HWND c = GetWindow(w, GW_CHILD); c; c = GetWindow(c, GW_HWNDNEXT)) SetWindowSubclass(c, NoWheelProc, 1, (DWORD_PTR)target);
}

Ctl& Page::MakeCtl(HWND parent, const Setting& s, bool withLabel) {
    ctls_.push_back(Ctl());
    Ctl& c = ctls_.back();
    c.s = &s;
    c.id = (index_ + 1) * 1000 + nextId_++;
    DWORD vis = WS_CHILD | WS_VISIBLE;
    HINSTANCE hi = G.inst;
    std::wstring label = Widen(s.label);
    if (s.type == SType::Bool) {
        c.main = CreateWindowExW(0, L"BUTTON", label.c_str(), vis | WS_TABSTOP | BS_AUTOCHECKBOX,
                                 0, 0, 10, 10, parent, (HMENU)(INT_PTR)c.id, hi, nullptr);
    } else {
        if (withLabel) c.label = MakeStatic(parent, label.c_str());
        if (s.type == SType::Enum) {
            c.main = CreateWindowExW(0, L"COMBOBOX", L"", vis | WS_TABSTOP | WS_VSCROLL | CBS_DROPDOWNLIST,
                                     0, 0, 10, 10, parent, (HMENU)(INT_PTR)c.id, hi, nullptr);
            for (auto& v : splitBar(s.values)) SendMessageW(c.main, CB_ADDSTRING, 0, (LPARAM)Widen(v).c_str());
            noWheel(c.main, scroller_ ? scroller_ : parent);
        } else {
            c.main = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", vis | WS_TABSTOP | ES_AUTOHSCROLL,
                                     0, 0, 10, 10, parent, (HMENU)(INT_PTR)c.id, hi, nullptr);
            if (s.slider) {
                c.slider = CreateWindowExW(0, TRACKBAR_CLASSW, L"", vis | WS_TABSTOP | TBS_HORZ | TBS_AUTOTICKS | TBS_NOTICKS,
                                           0, 0, 10, 10, parent, (HMENU)(INT_PTR)(c.id + 500), hi, nullptr);
                double f = std::pow(10.0, decimalsFor(s.step));
                SendMessageW(c.slider, TBM_SETRANGE, TRUE, MAKELONG((int)std::lround(s.min * f), (int)std::lround(s.max * f)));
                noWheel(c.slider, scroller_ ? scroller_ : parent);
                SendMessageW(c.slider, TBM_SETPAGESIZE, 0, (LPARAM)std::max(1L, std::lround(s.step * f * 5)));
                SendMessageW(c.slider, TBM_SETLINESIZE, 0, (LPARAM)std::max(1L, std::lround(s.step * f)));
            }
        }
    }
    // tooltip: own text, else the comment above the key in the default INI text
    std::string tip = s.tooltip ? s.tooltip : "";
    if (tip.empty()) if (const IniEntry* d = FindDefault(s.section, s.key)) tip = d->comment;
    std::wstring wt = Widen(tip);
    if (!wt.empty()) wt += L"\n";
    wt += L"[" + Widen(s.section) + L"] " + Widen(s.key);
    AddTip(c.label, wt); AddTip(c.main, wt); AddTip(c.slider, wt);
    return c;
}

void Page::BuildRows(const Setting* t, size_t n) {
    scroller_ = CreateWindowExW(WS_EX_CLIENTEDGE, L"FH2Panel", L"",
                                WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_CLIPCHILDREN | WS_CLIPSIBLINGS,
                                0, 0, 10, 10, host_, nullptr, G.inst, this);
    SetWindowLongPtrW(scroller_, GWL_EXSTYLE, GetWindowLongPtrW(scroller_, GWL_EXSTYLE) | WS_EX_CONTROLPARENT);
    ctls_.reserve(ctls_.size() + n + 8);
    const char* group = nullptr;
    for (size_t i = 0; i < n; ++i) {
        const Setting& s = t[i];
        if (!group || strcmp(group, s.group) != 0) {
            group = s.group;
            HWND hd = MakeStatic(scroller_, Widen(group).c_str());
            headers_.push_back(hd);
            items_.push_back({-1, hd});
        }
        MakeCtl(scroller_, s, true);
        items_.push_back({(int)ctls_.size() - 1, nullptr});
    }
}

Ctl* Page::Find(const char* section, const char* key) {
    for (auto& c : ctls_) if (!strcmp(c.s->section, section) && !strcmp(c.s->key, key)) return &c;
    return nullptr;
}

Ctl* Page::FindByWnd(HWND w) {
    if (!w) return nullptr;
    for (auto& c : ctls_) if (c.label == w || c.main == w || c.slider == w) return &c;
    return nullptr;
}

std::string Page::GetValue(Ctl& c) {
    const Setting& s = *c.s;
    wchar_t buf[512];
    switch (s.type) {
    case SType::Bool:
        return SendMessageW(c.main, BM_GETCHECK, 0, 0) == BST_CHECKED ? "1" : "0";
    case SType::Enum: {
        int i = (int)SendMessageW(c.main, CB_GETCURSEL, 0, 0);
        auto vals = splitBar(s.values);
        return i >= 0 && i < (int)vals.size() ? vals[i] : c.loaded;
    }
    case SType::Int: case SType::Float: {
        GetWindowTextW(c.main, buf, 512);
        std::string t = trimStr(Narrow(buf));
        char* end = nullptr;
        double v = s.type == SType::Int ? (double)strtol(t.c_str(), &end, 0) : strtod(t.c_str(), &end);
        if (t.empty() || (end && *end)) return c.shown.empty() ? c.loaded : c.shown;   // not a number: keep
        if (s.max > s.min) v = std::min(std::max(v, s.min), s.max);
        return s.type == SType::Int ? fmtNum(v, 0) : fmtNum(v, decimalsFor(s.step));
    }
    default:
        GetWindowTextW(c.main, buf, 512);
        return trimStr(Narrow(buf));
    }
}

void Page::SetValue(Ctl& c, const std::string& v) {
    const Setting& s = *c.s;
    bool was = loading_;
    loading_ = true;
    switch (s.type) {
    case SType::Bool:
        SendMessageW(c.main, BM_SETCHECK, strtol(v.c_str(), nullptr, 0) != 0 ? BST_CHECKED : BST_UNCHECKED, 0);
        break;
    case SType::Enum: {
        auto vals = splitBar(s.values);
        int sel = -1;
        for (size_t i = 0; i < vals.size(); ++i) if (lower(vals[i]) == lower(v)) sel = (int)i;
        SendMessageW(c.main, CB_SETCURSEL, sel, 0);
        break;
    }
    case SType::Int: case SType::Float: {
        char* end = nullptr;
        double d = s.type == SType::Int ? (double)strtol(v.c_str(), &end, 0) : strtod(v.c_str(), &end);
        if (s.max > s.min) d = std::min(std::max(d, s.min), s.max);
        int dec = s.type == SType::Int ? 0 : decimalsFor(s.step);
        SetWindowTextW(c.main, Widen(fmtNum(d, dec)).c_str());
        if (c.slider) SendMessageW(c.slider, TBM_SETPOS, TRUE, std::lround(d * std::pow(10.0, dec)));
        break;
    }
    default:
        SetWindowTextW(c.main, Widen(v).c_str());
    }
    loading_ = was;
    OnChanged(c);
}

double Page::Num(const char* section, const char* key, double def) {
    Ctl* c = Find(section, key);
    if (!c) return def;
    wchar_t buf[64];
    GetWindowTextW(c->main, buf, 64);
    wchar_t* end = nullptr;
    double v = wcstod(buf, &end);
    return end == buf ? def : v;
}

void Page::Load() {
    loading_ = true;
    for (auto& c : ctls_) {
        c.loaded = ReadSetting(c.s->section, c.s->key);
        SetValue(c, c.loaded);
        c.shown = GetValue(c);
    }
    loading_ = false;
    if (picture_) InvalidateRect(picture_, nullptr, FALSE);
}

void Page::Defaults() {
    for (auto& c : ctls_) {
        const IniEntry* d = FindDefault(c.s->section, c.s->key);
        SetValue(c, d ? d->value : std::string());
    }
    SetStatus(L"Defaults restored on this tab. Press Apply to save.");
    if (picture_) InvalidateRect(picture_, nullptr, FALSE);
}

static bool sameValue(const Setting& s, const std::string& a, const std::string& b) {
    switch (s.type) {
    case SType::Bool: case SType::Int:
        return strtol(a.c_str(), nullptr, 0) == strtol(b.c_str(), nullptr, 0);
    case SType::Float:
        return std::fabs(strtod(a.c_str(), nullptr) - strtod(b.c_str(), nullptr)) < 1e-9;
    default:
        return a == b;
    }
}

bool Page::Save() {
    bool ok = true;
    for (auto& c : ctls_) {
        std::string nv = GetValue(c);
        if (sameValue(*c.s, nv, c.shown)) continue;
        if (WriteSetting(c.s->section, c.s->key, nv)) { c.loaded = nv; c.shown = nv; }
        else ok = false;
    }
    return ok;
}

// ---------------------------------------------------------------- rows layout

int Page::PositionRows(int W) {
    int n = 0;
    for (auto& it : items_) n += it.ctl >= 0 ? (ctls_[it.ctl].label ? 1 : 0) + 1 + (ctls_[it.ctl].slider ? 1 : 0) : 1;
    HDWP dw = BeginDeferWindowPos(n + 1);
    auto put = [&](HWND h, int x, int y, int w, int hgt) {
        if (!h) return;
        HDWP r = DeferWindowPos(dw, h, nullptr, x, y, w, hgt, SWP_NOZORDER | SWP_NOACTIVATE);
        if (r) dw = r;
    };
    int labelW = std::min(std::max(S(200), W * 46 / 100), S(300));
    int x0 = labelW + S(18);
    int ctlW = std::max(S(80), std::min(S(190), W - x0 - S(14)));
    int y = -scrollY_ + S(4);
    for (auto& it : items_) {
        if (it.ctl < 0) {
            y += S(6);
            put(it.header, S(8), y, W - S(16), S(20));
            y += S(26);
            continue;
        }
        Ctl& c = ctls_[it.ctl];
        const int rowH = S(28);
        if (c.s->type == SType::Bool) {
            put(c.main, S(14), y + S(3), W - S(28), S(22));
        } else {
            put(c.label, S(14), y, labelW, S(24));
            if (c.slider) {
                int ew = S(sliderEditW_), sw = std::max(S(60), W - x0 - ew - S(22));
                put(c.slider, x0, y, sw, S(26));
                put(c.main, x0 + sw + S(6), y + S(1), ew, S(22));
            } else if (c.s->type == SType::Enum) {
                put(c.main, x0, y + S(1), ctlW, S(220));
            } else {
                int w = (c.s->type == SType::Int || c.s->type == SType::Float) ? std::min(ctlW, S(100)) : ctlW;
                put(c.main, x0, y + S(1), w, S(22));
            }
        }
        y += rowH;
    }
    if (dw) EndDeferWindowPos(dw);
    return y + scrollY_ + S(8);
}

void Page::LayoutRows() {
    if (!scroller_) return;
    for (int pass = 0; pass < 3; ++pass) {
        RECT rc; GetClientRect(scroller_, &rc);
        rowsHeight_ = PositionRows(rc.right);
        int maxPos = std::max(0, rowsHeight_ - (int)rc.bottom);
        scrollY_ = std::min(std::max(scrollY_, 0), maxPos);
        SCROLLINFO si{}; si.cbSize = sizeof(si);
        si.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
        si.nMin = 0; si.nMax = rowsHeight_ - 1; si.nPage = rc.bottom; si.nPos = scrollY_;
        SetScrollInfo(scroller_, SB_VERT, &si, TRUE);
    }
    RedrawWindow(scroller_, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN);
}

void Page::EnsureVisible(HWND w) {
    if (!scroller_ || !w || GetParent(w) != scroller_) return;
    RECT r, c; GetWindowRect(w, &r); MapWindowPoints(nullptr, scroller_, (POINT*)&r, 2);
    GetClientRect(scroller_, &c);
    int d = 0;
    if (r.top < 0) d = r.top - S(30);
    else if (r.bottom > c.bottom) d = r.bottom - c.bottom + S(8);
    if (d) { scrollY_ += d; LayoutRows(); }
}

// ---------------------------------------------------------------- focus / hover tracking

void Page::Tick() {
    HWND f = GetFocus();
    POINT pt; GetCursorPos(&pt);
    HWND hov = WindowFromPoint(pt);
    Ctl* c = FindByWnd(hov);
    if (!c) {
        c = FindByWnd(f);
        if (!c && f) c = FindByWnd(GetParent(f));          // the edit part of a combo box
    }
    if (f != lastFocus_) {
        lastFocus_ = f;
        Ctl* fc = FindByWnd(f);
        if (!fc && f) fc = FindByWnd(GetParent(f));
        if (fc) EnsureVisible(fc->main);
    }
    std::string part = c && c->s->part ? c->s->part : "";
    if (part != part_) {
        part_ = part;
        OnPart(part_);
        if (picture_) InvalidateRect(picture_, nullptr, FALSE);
    }
}

// ---------------------------------------------------------------- panel messages

LRESULT Page::PanelProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
    case WM_ERASEBKGND: {
        RECT rc; GetClientRect(h, &rc);
        FillRect((HDC)w, &rc, G.white);
        return 1;
    }
    case WM_CTLCOLORSTATIC: case WM_CTLCOLORBTN:
        SetBkColor((HDC)w, GetSysColor(COLOR_WINDOW));
        return (LRESULT)G.white;
    case WM_COMMAND: {
        int id = LOWORD(w), code = HIWORD(w);
        HWND ctl = (HWND)l;
        if (h == scroller_ || h == host_) {
            bool change = code == BN_CLICKED || code == CBN_SELCHANGE || code == EN_CHANGE;
            Ctl* c = nullptr;
            for (auto& x : ctls_) if (x.id == id) c = &x;
            if (c && change && !loading_) {
                if (c->slider && code == EN_CHANGE) {          // edit -> slider
                    wchar_t buf[64]; GetWindowTextW(c->main, buf, 64);
                    wchar_t* end = nullptr; double v = wcstod(buf, &end);
                    if (end != buf) {
                        bool was = loading_; loading_ = true;
                        SendMessageW(c->slider, TBM_SETPOS, TRUE, std::lround(v * std::pow(10.0, decimalsFor(c->s->step))));
                        loading_ = was;
                    }
                }
                SetStatus(L"Changes not saved yet. Press Apply.");
                OnChanged(*c);
                if (picture_) InvalidateRect(picture_, nullptr, FALSE);
            }
            OnCommand(id, code, ctl);
        }
        return 0;
    }
    case WM_HSCROLL: {
        Ctl* c = FindByWnd((HWND)l);
        if (c && c->slider && !loading_) {
            int dec = decimalsFor(c->s->step);
            double v = (double)SendMessageW(c->slider, TBM_GETPOS, 0, 0) / std::pow(10.0, dec);
            bool was = loading_; loading_ = true;
            SetWindowTextW(c->main, Widen(fmtNum(v, dec)).c_str());
            loading_ = was;
            SetStatus(L"Changes not saved yet. Press Apply.");
            OnChanged(*c);
        }
        return 0;
    }
    case WM_NOTIFY: {
        bool handled = false;
        LRESULT r = OnNotify((NMHDR*)l, handled);
        if (handled) return r;
        break;
    }
    case WM_VSCROLL:
        if (h == scroller_) {
            SCROLLINFO si{}; si.cbSize = sizeof(si); si.fMask = SIF_ALL;
            GetScrollInfo(h, SB_VERT, &si);
            int pos = si.nPos;
            switch (LOWORD(w)) {
            case SB_LINEUP: pos -= S(28); break;
            case SB_LINEDOWN: pos += S(28); break;
            case SB_PAGEUP: pos -= (int)si.nPage; break;
            case SB_PAGEDOWN: pos += (int)si.nPage; break;
            case SB_THUMBTRACK: case SB_THUMBPOSITION: pos = si.nTrackPos; break;
            case SB_TOP: pos = 0; break;
            case SB_BOTTOM: pos = si.nMax; break;
            }
            scrollY_ = pos;
            LayoutRows();
            return 0;
        }
        break;
    case WM_MOUSEWHEEL:
        if (h == scroller_) {
            int d = GET_WHEEL_DELTA_WPARAM(w);
            scrollY_ -= d * S(28) * 3 / WHEEL_DELTA;
            LayoutRows();
            return 0;
        }
        break;
    case WM_SIZE:
        if (h == scroller_) { LayoutRows(); return 0; }
        break;
    }
    return DefWindowProcW(h, m, w, l);
}

} // namespace app
