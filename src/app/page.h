// SPDX-License-Identifier: MIT
// Base class of the tab pages: builds scrollable rows of controls from a Setting table, loads and
// saves them through the INI, keeps tooltips, and tracks which control is focused or hovered so the
// page picture can highlight the matching part.
#pragma once
#include <windows.h>
#include <commctrl.h>
#include <deque>
#include <string>
#include <vector>
#include "settings.h"

namespace app {

struct Globals {
    HINSTANCE inst = nullptr;
    UINT      dpi = 96;
    HFONT     font = nullptr, bold = nullptr;
    HWND      main = nullptr;
    HBRUSH    white = nullptr;
};
extern Globals G;
inline int S(int v) { return MulDiv(v, (int)G.dpi, 96); }

void SetStatus(const std::wstring& text);          // status line of the main window (main.cpp)
void RegisterPageClasses();                         // "FH2Panel", "FH2Picture"

struct Ctl {
    const Setting* s = nullptr;
    HWND label = nullptr, main = nullptr, slider = nullptr;
    std::string loaded;     // raw INI value when loaded
    std::string shown;      // value after the control took it (clamped / normalised)
    int id = 0;
};

class Page {
public:
    explicit Page(int index) : index_(index) {}
    virtual ~Page() {}
    virtual const wchar_t* Title() const = 0;
    virtual void Create(HWND parent);
    HWND host() const { return host_; }
    void Place(const RECT& rc);                     // rc in the parent's client coordinates
    virtual void Load();
    virtual bool Save();                            // writes changed values; false on error
    virtual void Defaults();
    virtual void OnShow(bool visible) {}
    virtual void Tick();                            // ~20 Hz while the page is visible
    virtual void OnDpi();                           // fonts / layout after a DPI change

    // window procedures of the page's windows (forwarded by the "FH2Panel" / "FH2Picture" classes)
    LRESULT PanelProc(HWND h, UINT m, WPARAM w, LPARAM l);
    virtual void PaintPicture(HDC dc, const RECT& rc) {}

protected:
    virtual void Build() = 0;                       // create the controls (host_ exists)
    virtual void Layout(int w, int h) = 0;          // position everything in the host
    virtual LRESULT OnNotify(NMHDR* nm, bool& handled) { handled = false; return 0; }
    virtual void OnCommand(int id, int code, HWND ctl) {}
    virtual void OnChanged(Ctl& c) {}               // a setting control changed (user or Load)
    virtual void OnPart(const std::string& part) {} // focus / hover moved to another picture part

    // controls
    void  BuildRows(const Setting* t, size_t n);    // scrolling rows in scroller_
    Ctl&  MakeCtl(HWND parent, const Setting& s, bool withLabel);
    void  AddTip(HWND w, const std::wstring& text);
    void  LayoutRows();                              // positions the rows inside scroller_
    int   RowsHeight() const { return rowsHeight_; }
    Ctl*  Find(const char* section, const char* key);
    Ctl*  FindByWnd(HWND w);
    virtual std::string GetValue(Ctl& c);
    virtual void SetValue(Ctl& c, const std::string& v);
    double Num(const char* section, const char* key, double def);   // current numeric value of a control
    HWND  MakePicture();
    HWND  MakeStatic(HWND parent, const wchar_t* text, int id = -1);
    void  ApplyFonts();
    void  EnsureVisible(HWND w);

    int   index_;
    HWND  host_ = nullptr, scroller_ = nullptr, tips_ = nullptr, picture_ = nullptr;
    std::vector<Ctl> ctls_;
    bool  loading_ = false;
    std::string part_;                               // picture part of the focused/hovered control
    int   scrollY_ = 0;

    struct Item { int ctl; HWND header; };
    std::vector<Item> items_;
    std::vector<HWND> headers_;
    int   rowsHeight_ = 0;
    int   nextId_ = 0;
    int   sliderEditW_ = 52;                         // width (96 dpi) of the edit next to a slider
    HWND  lastFocus_ = nullptr;
private:
    std::deque<std::wstring> tipText_;
    int PositionRows(int w);
};

} // namespace app
