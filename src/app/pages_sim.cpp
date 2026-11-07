// SPDX-License-Identifier: MIT
// Tab 3: simulation: field of view, realistic assists, steering wheel, per-car assist and steering lock overrides.
#include "pages.h"
#include <algorithm>
#include <array>
#include <cwctype>
#include <map>
#include "ini.h"
#include "settings_tables.h"
#include "../common/car_db.h"
#include "../common/cars_ini.h"

namespace app {

namespace {
const int kIdFilter = 9200, kIdList = 9201, kIdAbs = 9202, kIdTc = 9203, kIdStm = 9204, kIdClutch = 9205, kIdLock = 9206;
using Triple = std::array<int, 4>;      // ABS, TC, STM, clutch: 0 = auto, 1 = on, 2 = off, 3 = automatic (clutch only)
const Triple kAuto{0, 0, 0, 0};

const wchar_t* yn(char c) {
    switch (c) { case 'y': return L"yes"; case 'n': return L"no"; case 'o': return L"optional"; default: return L"unknown"; }
}
const wchar_t* ovr(int v) { return v == 1 ? L"on" : v == 2 ? L"off" : v == 3 ? L"automatic" : L"auto"; }
const wchar_t* gearboxText(char c) {
    switch (c) {
        case 'm': return L"manual"; case 'x': return L"manual or auto"; case 'a': return L"automatic only";
        case 't': return L"auto, manual mode"; case 'd': return L"dual-clutch"; case 's': return L"automated manual";
        case 'c': return L"CVT"; case 'e': return L"electric"; case 'q': return L"sequential"; default: return L"unknown";
    }
}
std::wstring lowerW(std::wstring s) { for (auto& c : s) c = (wchar_t)towlower(c); return s; }
}

class SimPage : public Page {
public:
    using Page::Page;
    const wchar_t* Title() const override { return L"Simulation"; }

    void Load() override {
        Page::Load();
        LoadCars();
        Refilter();
        UpdateCombos();
    }
    bool Save() override {
        bool ok = Page::Save();
        return SaveCars() && ok;
    }

protected:
    void Build() override {
        BuildRows(kSimSettings, sizeof(kSimSettings) / sizeof(Setting));
        filterLabel_ = MakeStatic(host_, L"Cars - filter:");
        filter_ = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
                                  0, 0, 10, 10, host_, (HMENU)(INT_PTR)kIdFilter, G.inst, nullptr);
        SendMessageW(filter_, EM_SETCUEBANNER, TRUE, (LPARAM)L"id, name or year");
        count_ = MakeStatic(host_, L"");
        list_ = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"",
                                WS_CHILD | WS_VISIBLE | WS_TABSTOP | LVS_REPORT | LVS_OWNERDATA | LVS_SINGLESEL | LVS_SHOWSELALWAYS,
                                0, 0, 10, 10, host_, (HMENU)(INT_PTR)kIdList, G.inst, nullptr);
        ListView_SetExtendedListViewStyle(list_, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_GRIDLINES);
        static const wchar_t* heads[] = {L"Id", L"Car", L"Year", L"Real ABS", L"Real TC", L"Real STM", L"Real clutch", L"Gearbox", L"Lock", L"ABS ovr.", L"TC ovr.", L"STM ovr.", L"Clutch ovr."};
        for (int i = 0; i < 13; ++i) {
            LVCOLUMNW col{};
            col.mask = LVCF_TEXT | LVCF_FMT;
            col.fmt = LVCFMT_LEFT;
            col.pszText = const_cast<wchar_t*>(heads[i]);
            SendMessageW(list_, LVM_INSERTCOLUMNW, i, (LPARAM)&col);
        }
        absLabel_ = MakeStatic(host_, L"ABS override:");
        tcLabel_ = MakeStatic(host_, L"TC override:");
        stmLabel_ = MakeStatic(host_, L"STM override:");
        clutchLabel_ = MakeStatic(host_, L"Clutch override:");
        for (int k = 0; k < 4; ++k) {
            HWND cb = CreateWindowExW(0, L"COMBOBOX", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL | CBS_DROPDOWNLIST,
                                      0, 0, 10, 10, host_, (HMENU)(INT_PTR)(k == 0 ? kIdAbs : k == 1 ? kIdTc : k == 2 ? kIdStm : kIdClutch), G.inst, nullptr);
            for (const wchar_t* t : {L"auto", L"on", L"off"}) SendMessageW(cb, CB_ADDSTRING, 0, (LPARAM)t);
            if (k == 3) SendMessageW(cb, CB_ADDSTRING, 0, (LPARAM)L"automatic");
            (k == 0 ? absCombo_ : k == 1 ? tcCombo_ : k == 2 ? stmCombo_ : clutchCombo_) = cb;
        }
        lockLabel_ = MakeStatic(host_, L"Lock override:");
        lockEdit_ = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL | ES_NUMBER,
                                    0, 0, 10, 10, host_, (HMENU)(INT_PTR)kIdLock, G.inst, nullptr);
        SendMessageW(lockEdit_, EM_LIMITTEXT, 4, 0);
        SendMessageW(lockEdit_, EM_SETCUEBANNER, FALSE, (LPARAM)L"degrees");
        hint_ = MakeStatic(host_, L"auto = car data");
        AddTip(list_, L"Real ABS / TC / STM / clutch: what the real car had (yes, no, optional, unknown). Gearbox: the real car's gearbox. ovr. = override. "
                      L"Lock: lock-to-lock steering wheel rotation of the real car. \"ovr\" = your lock override. "
                      L"Overrides are saved in FH2SimImmersion_cars.ini ([Cars] and [SteeringLock], key = car Id).");
        AddTip(lockEdit_, L"Lock-to-lock steering wheel rotation of the selected car in degrees (90 - 3600); empty = use the research data. Saved as [SteeringLock] Id=degrees in FH2SimImmersion_cars.ini.");
        AddTip(absCombo_, L"auto = use the research data (and the settings above); on / off = force it for this car.");
        AddTip(tcCombo_, L"auto = use the research data (and the settings above); on / off = force it for this car.");
        AddTip(stmCombo_, L"auto = use the research data (and the settings above); on / off = force it for this car.");
        AddTip(clutchCombo_, L"auto = use the research data; on = the car has a clutch; off = it has none; automatic = always shift automatically.");
        EnableWindow(absCombo_, FALSE); EnableWindow(tcCombo_, FALSE); EnableWindow(stmCombo_, FALSE);
        EnableWindow(clutchCombo_, FALSE);
        EnableWindow(lockEdit_, FALSE);
    }

    void Layout(int w, int h) override {
        int need = S(22);
        for (auto& it : items_) need += it.ctl < 0 ? S(32) : S(28);
        int topH = std::min(need, std::max(S(120), h * 60 / 100));
        SetWindowPos(scroller_, nullptr, 0, 0, w, topH, SWP_NOZORDER);
        LayoutRows();
        int y = topH + S(8);
        SetWindowPos(filterLabel_, nullptr, S(8), y, S(100), S(24), SWP_NOZORDER);
        SetWindowPos(filter_, nullptr, S(112), y + S(1), S(240), S(22), SWP_NOZORDER);
        SetWindowPos(count_, nullptr, S(364), y, std::max(S(50), w - S(372)), S(24), SWP_NOZORDER);
        int ly = y + S(30), bottom = h - S(36);   // combos sit in the strip under the list
        SetWindowPos(list_, nullptr, 0, ly, w, std::max(S(60), bottom - ly), SWP_NOZORDER);
        static const int cw[] = {30, 144, 38, 60, 52, 58, 70, 92, 48, 56, 52, 58, 64};
        for (int i = 0; i < 13; ++i) ListView_SetColumnWidth(list_, i, S(cw[i]));
        int by = h - S(30);
        SetWindowPos(absLabel_, nullptr, S(8), by, S(80), S(24), SWP_NOZORDER);
        SetWindowPos(absCombo_, nullptr, S(90), by, S(62), S(200), SWP_NOZORDER);
        SetWindowPos(tcLabel_, nullptr, S(160), by, S(72), S(24), SWP_NOZORDER);
        SetWindowPos(tcCombo_, nullptr, S(234), by, S(62), S(200), SWP_NOZORDER);
        SetWindowPos(stmLabel_, nullptr, S(304), by, S(80), S(24), SWP_NOZORDER);
        SetWindowPos(stmCombo_, nullptr, S(386), by, S(62), S(200), SWP_NOZORDER);
        SetWindowPos(clutchLabel_, nullptr, S(456), by, S(86), S(24), SWP_NOZORDER);
        SetWindowPos(clutchCombo_, nullptr, S(544), by, S(78), S(200), SWP_NOZORDER);
        SetWindowPos(lockLabel_, nullptr, S(634), by, S(78), S(24), SWP_NOZORDER);
        SetWindowPos(lockEdit_, nullptr, S(714), by, S(50), S(22), SWP_NOZORDER);
        SetWindowPos(hint_, nullptr, S(772), by, std::max(S(50), w - S(780)), S(24), SWP_NOZORDER);
    }

    void OnCommand(int id, int code, HWND) override {
        if (id == kIdFilter && code == EN_CHANGE) { Refilter(); return; }
        if (id == kIdLock && code == EN_CHANGE && !settingLock_) {
            int sel = Selected();
            if (sel < 0) return;
            unsigned car = shown_[sel]->id;
            wchar_t buf[16];
            GetWindowTextW(lockEdit_, buf, 16);
            unsigned long v = wcstoul(buf, nullptr, 10);
            if (buf[0] == 0 || v == 0) lockCur_.erase(car); else lockCur_[car] = (unsigned)std::min(std::max(v, 90ul), 3600ul);   // what Apply saves
            ListView_RedrawItems(list_, sel, sel);
            Refresh();
            SetStatus(L"Changes not saved yet. Press Apply.");
            return;
        }
        if ((id == kIdAbs || id == kIdTc || id == kIdStm || id == kIdClutch) && code == CBN_SELCHANGE) {
            int sel = Selected();
            if (sel < 0) return;
            unsigned car = shown_[sel]->id;
            Triple p{(int)SendMessageW(absCombo_, CB_GETCURSEL, 0, 0), (int)SendMessageW(tcCombo_, CB_GETCURSEL, 0, 0),
                     (int)SendMessageW(stmCombo_, CB_GETCURSEL, 0, 0), (int)SendMessageW(clutchCombo_, CB_GETCURSEL, 0, 0)};
            for (int& v : p) v = std::max(v, 0);
            if (p == kAuto) cur_.erase(car); else cur_[car] = p;
            ListView_RedrawItems(list_, sel, sel);
            SetStatus(L"Changes not saved yet. Press Apply.");
        }
    }

    LRESULT OnNotify(NMHDR* nm, bool& handled) override {
        handled = false;
        if (nm->hwndFrom != list_) return 0;
        if (nm->code == LVN_GETDISPINFOW) {
            handled = true;
            auto* di = (NMLVDISPINFOW*)nm;
            if (!(di->item.mask & LVIF_TEXT) || di->item.iItem < 0 || di->item.iItem >= (int)shown_.size()) return 0;
            const fh2::CarInfo& c = *shown_[di->item.iItem];
            std::wstring t;
            auto it = cur_.find(c.id);
            Triple p = it == cur_.end() ? kAuto : it->second;
            switch (di->item.iSubItem) {
            case 0: t = std::to_wstring(c.id); break;
            case 1: t = Widen(c.name); break;
            case 2: t = std::to_wstring(c.year); break;
            case 3: t = yn(c.abs); break;
            case 4: t = yn(c.tc); break;
            case 5: t = yn(c.stm); break;
            case 6: t = yn(c.clutch); break;
            case 7: t = gearboxText(c.gearbox); break;
            case 8: {
                auto lo = lockCur_.find(c.id);
                if (lo != lockCur_.end()) t = std::to_wstring(lo->second) + L" ovr";
                else t = std::to_wstring(c.lockDeg);
                break;
            }
            case 9: t = ovr(p[0]); break;
            case 10: t = ovr(p[1]); break;
            case 11: t = ovr(p[2]); break;
            case 12: t = ovr(p[3]); break;
            }
            wcsncpy(di->item.pszText, t.c_str(), di->item.cchTextMax - 1);
            di->item.pszText[di->item.cchTextMax - 1] = 0;
            return 0;
        }
        if (nm->code == LVN_ITEMCHANGED) {
            handled = true;
            auto* nl = (NMLISTVIEW*)nm;
            if (nl->uChanged & LVIF_STATE) UpdateCombos();
            return 0;
        }
        return 0;
    }

private:
    HWND filterLabel_ = nullptr, filter_ = nullptr, count_ = nullptr, list_ = nullptr;
    HWND lockLabel_ = nullptr, lockEdit_ = nullptr;
    bool settingLock_ = false;
    std::map<unsigned, unsigned> lockCur_, lockOrig_;     // [SteeringLock] car Id -> degrees
    HWND absLabel_ = nullptr, tcLabel_ = nullptr, stmLabel_ = nullptr, clutchLabel_ = nullptr, absCombo_ = nullptr,
         tcCombo_ = nullptr, stmCombo_ = nullptr, clutchCombo_ = nullptr, hint_ = nullptr;
    std::vector<const fh2::CarInfo*> shown_;
    std::map<unsigned, Triple> cur_, orig_;

    size_t OverrideCount() const {
        size_t n = cur_.size();
        for (auto& kv : lockCur_) if (!cur_.count(kv.first)) ++n;
        return n;
    }

    int Selected() {
        int i = ListView_GetNextItem(list_, -1, LVNI_SELECTED);
        return i >= 0 && i < (int)shown_.size() ? i : -1;
    }

    void Refilter() {
        wchar_t buf[256];
        GetWindowTextW(filter_, buf, 256);
        std::wstring f = lowerW(buf);
        unsigned keepId = 0;
        if (int s = Selected(); s >= 0) keepId = shown_[s]->id;
        shown_.clear();
        for (size_t i = 0; i < fh2::kCarCount; ++i) {
            const fh2::CarInfo& c = fh2::kCars[i];
            if (!f.empty()) {
                std::wstring hay = lowerW(std::to_wstring(c.id) + L" " + Widen(c.name) + L" " + Widen(c.media) + L" " +
                                          std::to_wstring(c.year));
                if (hay.find(f) == std::wstring::npos) continue;
            }
            shown_.push_back(&c);
        }
        ListView_SetItemCountEx(list_, (int)shown_.size(), LVSICF_NOINVALIDATEALL);
        ListView_SetItemState(list_, -1, 0, LVIS_SELECTED | LVIS_FOCUSED);
        for (size_t i = 0; i < shown_.size(); ++i)
            if (shown_[i]->id == keepId) { ListView_SetItemState(list_, (int)i, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED); break; }
        InvalidateRect(list_, nullptr, TRUE);
        SetWindowTextW(count_, (std::to_wstring(shown_.size()) + L" of " + std::to_wstring(fh2::kCarCount) + L" cars, " +
                                std::to_wstring(OverrideCount()) + L" with an override").c_str());
        UpdateCombos();
    }

    void Refresh() {
        InvalidateRect(list_, nullptr, TRUE);
        SetWindowTextW(count_, (std::to_wstring(shown_.size()) + L" of " + std::to_wstring(fh2::kCarCount) + L" cars, " +
                                std::to_wstring(OverrideCount()) + L" with an override").c_str());
    }

    void UpdateCombos() {
        int sel = Selected();
        EnableWindow(absCombo_, sel >= 0);
        EnableWindow(tcCombo_, sel >= 0);
        EnableWindow(stmCombo_, sel >= 0);
        EnableWindow(clutchCombo_, sel >= 0);
        Triple p = kAuto;
        if (sel >= 0) { auto it = cur_.find(shown_[sel]->id); if (it != cur_.end()) p = it->second; }
        SendMessageW(absCombo_, CB_SETCURSEL, sel >= 0 ? p[0] : -1, 0);
        SendMessageW(tcCombo_, CB_SETCURSEL, sel >= 0 ? p[1] : -1, 0);
        SendMessageW(stmCombo_, CB_SETCURSEL, sel >= 0 ? p[2] : -1, 0);
        SendMessageW(clutchCombo_, CB_SETCURSEL, sel >= 0 ? p[3] : -1, 0);
        EnableWindow(lockEdit_, sel >= 0);
        settingLock_ = true;
        std::wstring lv;
        if (sel >= 0) { auto it = lockCur_.find(shown_[sel]->id); if (it != lockCur_.end()) lv = std::to_wstring(it->second); }
        SetWindowTextW(lockEdit_, lv.c_str());
        settingLock_ = false;
        Refresh();
    }

    void LoadCars() {
        cur_.clear();
        std::wstring path = CarsIniPath();
        if (GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES) {
            std::vector<wchar_t> buf(32768);
            DWORD n = GetPrivateProfileSectionW(L"Cars", buf.data(), (DWORD)buf.size(), path.c_str());
            for (const wchar_t* p = buf.data(); n && *p; p += wcslen(p) + 1) {
                std::wstring line = p;
                fh2::CarAssistLine l;
                if (!fh2::ParseCarAssistLine(line, l)) continue;
                Triple pr{(int)l.abs, (int)l.tc, (int)l.stm, (int)l.clutch};   // CarOvr values = page ints (0 auto, 1 on, 2 off, 3 automatic)
                if (pr != kAuto) cur_.emplace(l.id, pr);   // first line of a duplicated id wins, as in the plugin
            }
        }
        orig_ = cur_;
        lockCur_.clear();
        if (GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES) {
            std::vector<wchar_t> buf(32768);
            DWORD n = GetPrivateProfileSectionW(L"SteeringLock", buf.data(), (DWORD)buf.size(), path.c_str());
            for (const wchar_t* p = buf.data(); n && *p; p += wcslen(p) + 1) {
                std::wstring line = p;
                unsigned id = 0, deg = 0;
                if (!fh2::ParseSteeringLockLine(line, id, deg)) continue;
                lockCur_.emplace(id, deg);
            }
        }
        lockOrig_ = lockCur_;
    }

    bool SaveCars() {
        std::wstring path = CarsIniPath();
        bool ok = true;
        std::map<unsigned, Triple> all = orig_;
        for (auto& kv : cur_) all[kv.first];
        for (auto& kv : all) {
            auto a = orig_.find(kv.first), b = cur_.find(kv.first);
            Triple o = a == orig_.end() ? kAuto : a->second, n = b == cur_.end() ? kAuto : b->second;
            if (o == n) continue;
            std::wstring key = std::to_wstring(kv.first);
            if (n == kAuto) {
                ok = WritePrivateProfileStringW(L"Cars", key.c_str(), nullptr, path.c_str()) && ok;     // all auto: remove
            } else {
                std::wstring val = std::wstring(ovr(n[0])) + L"," + ovr(n[1]) + L"," + ovr(n[2]) + L"," + ovr(n[3]);
                ok = WritePrivateProfileStringW(L"Cars", key.c_str(), val.c_str(), path.c_str()) && ok;
            }
        }
        if (ok) orig_ = cur_;
        std::map<unsigned, unsigned> allLock = lockOrig_;
        for (auto& kv : lockCur_) allLock[kv.first];
        bool lockOk = true;
        for (auto& kv : allLock) {
            auto a = lockOrig_.find(kv.first), b = lockCur_.find(kv.first);
            unsigned o = a == lockOrig_.end() ? 0 : a->second, n = b == lockCur_.end() ? 0 : b->second;
            if (o == n) continue;
            std::wstring key = std::to_wstring(kv.first);
            if (n == 0) lockOk = WritePrivateProfileStringW(L"SteeringLock", key.c_str(), nullptr, path.c_str()) && lockOk;
            else lockOk = WritePrivateProfileStringW(L"SteeringLock", key.c_str(), std::to_wstring(n).c_str(), path.c_str()) && lockOk;
        }
        if (lockOk) lockOrig_ = lockCur_;
        return ok && lockOk;
    }
};

Page* NewSimPage(int index) { return new SimPage(index); }

} // namespace app
