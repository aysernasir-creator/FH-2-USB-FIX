// SPDX-License-Identifier: MIT
// FH2SimImmersion.exe - settings window and launcher.
//
//   FH2SimImmersion.exe                       open the window; plugin is injected into any azimuth.exe that starts
//   FH2SimImmersion.exe "E:\...\azimuth.exe"  also start that Azimuth with the plugin
//
// Nothing is written to the Azimuth or game folders. Settings live in FH2SimImmersion.ini next to this program.
#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>
#include <memory>
#include <string>
#include <vector>
#include "ini.h"
#include "injector.h"
#include "page.h"
#include "pages.h"

#ifndef FH2SI_VERSION
#define FH2SI_VERSION "dev"
#endif

namespace app {

enum { IDC_TAB = 100, IDC_APPLY, IDC_REVERT, IDC_DEFAULTS, IDC_STATUS };
const UINT WM_APP_STATUS = WM_APP + 1;
const UINT_PTR kTimer = 1;
const wchar_t* kClass = L"FH2SimImmersionApp";

static HWND g_tab, g_status, g_apply, g_revert, g_defaults;
static std::vector<std::unique_ptr<Page>> g_pages;
static int g_cur = 0;
static Injector g_injector;

void SetStatus(const std::wstring& text) { if (g_status) SetWindowTextW(g_status, text.c_str()); }

static UINT dpiOfWindow(HWND h) {
    using Fn = UINT(WINAPI*)(HWND);
    static Fn f = (Fn)GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetDpiForWindow");
    if (f && h) { UINT d = f(h); if (d) return d; }
    HDC dc = GetDC(nullptr);
    UINT d = GetDeviceCaps(dc, LOGPIXELSX);
    ReleaseDC(nullptr, dc);
    return d ? d : 96;
}

static void makeFonts() {
    if (G.font) DeleteObject(G.font);
    if (G.bold) DeleteObject(G.bold);
    NONCLIENTMETRICSW ncm{}; ncm.cbSize = sizeof(ncm);
    using Fn = BOOL(WINAPI*)(UINT, UINT, PVOID, UINT, UINT);
    static Fn spi = (Fn)GetProcAddress(GetModuleHandleW(L"user32.dll"), "SystemParametersInfoForDpi");
    bool ok = spi ? spi(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0, G.dpi) != 0
                  : SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0) != 0;
    if (!ok) { wcscpy(ncm.lfMessageFont.lfFaceName, L"Segoe UI"); ncm.lfMessageFont.lfHeight = -MulDiv(9, (int)G.dpi, 72); }
    G.font = CreateFontIndirectW(&ncm.lfMessageFont);
    LOGFONTW b = ncm.lfMessageFont; b.lfWeight = FW_SEMIBOLD;
    G.bold = CreateFontIndirectW(&b);
}

static void redrawAll(HWND hwnd) {
    RedrawWindow(hwnd, nullptr, nullptr, RDW_ERASE | RDW_FRAME | RDW_INVALIDATE | RDW_ALLCHILDREN);
}

static void showPage(int i) {
    if (i < 0 || i >= (int)g_pages.size()) return;
    // (WM_SETREDRAW FALSE hides the window for IsWindowVisible, so the page style bit is tested instead)
    SendMessageW(G.main, WM_SETREDRAW, FALSE, 0);
    for (int k = 0; k < (int)g_pages.size(); ++k)
        if (k != i && (GetWindowLongPtrW(g_pages[k]->host(), GWL_STYLE) & WS_VISIBLE)) { ShowWindow(g_pages[k]->host(), SW_HIDE); g_pages[k]->OnShow(false); }
    g_cur = i;
    ShowWindow(g_pages[i]->host(), SW_SHOW);
    g_pages[i]->OnShow(true);
    TabCtrl_SetCurSel(g_tab, i);
    SendMessageW(G.main, WM_SETREDRAW, TRUE, 0);
    redrawAll(G.main);
}

static void layoutAll(HWND hwnd) {
    SendMessageW(hwnd, WM_SETREDRAW, FALSE, 0);
    RECT rc; GetClientRect(hwnd, &rc);
    int bar = S(46), m = S(8);
    SetWindowPos(g_tab, nullptr, m, m, rc.right - 2 * m, rc.bottom - bar - m, SWP_NOZORDER);
    RECT disp{0, 0, rc.right - 2 * m, rc.bottom - bar - m};
    TabCtrl_AdjustRect(g_tab, FALSE, &disp);
    OffsetRect(&disp, m, m);
    InflateRect(&disp, -S(2), -S(2));
    for (auto& p : g_pages) p->Place(disp);
    int bh = S(30), by = rc.bottom - bar + S(8), bw = S(110);
    int x = rc.right - m;
    SetWindowPos(g_defaults, nullptr, x - S(170), by, S(170), bh, SWP_NOZORDER);
    SetWindowPos(g_revert, nullptr, x - S(170) - S(8) - bw, by, bw, bh, SWP_NOZORDER);
    SetWindowPos(g_apply, nullptr, x - S(170) - 2 * (S(8) + bw) + 0, by, bw, bh, SWP_NOZORDER);
    SetWindowPos(g_status, nullptr, m + S(2), by, std::max(S(60), x - S(170) - 2 * (S(8) + bw) - S(8) - m), bh, SWP_NOZORDER);
    SendMessageW(hwnd, WM_SETREDRAW, TRUE, 0);
    redrawAll(hwnd);
}

static void applyFontsToWindow(HWND hwnd) {
    EnumChildWindows(hwnd, [](HWND c, LPARAM) -> BOOL {
        // pages set their own fonts (headers are bold); only the shell controls here
        if (c == g_tab || c == g_status || c == g_apply || c == g_revert || c == g_defaults)
            SendMessageW(c, WM_SETFONT, (WPARAM)G.font, TRUE);
        return TRUE;
    }, 0);
}

static void doApply() {
    bool ok = true;
    for (auto& p : g_pages) ok = p->Save() && ok;
    SetStatus(ok ? L"Saved. Changes take effect the next time the game starts."
                 : L"Could not write a settings file. Is the folder writable?");
}

static MainPageApi* mainApi() { return g_pages.size() > 4 ? AsMainApi(g_pages[4].get()) : nullptr; }

static void startWithPath(const std::wstring& path) {
    if (MainPageApi* api = mainApi()) {
        api->SetAzimuthPath(path, true);
        api->StartAzimuthNow();
    }
    showPage(4);
}

static LRESULT CALLBACK WndProc(HWND hwnd, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
    case WM_CREATE: {
        G.main = hwnd;
        G.dpi = dpiOfWindow(hwnd);
        makeFonts();
        g_tab = CreateWindowExW(0, WC_TABCONTROLW, L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_CLIPSIBLINGS | WS_CLIPCHILDREN,
                                0, 0, 10, 10, hwnd, (HMENU)IDC_TAB, G.inst, nullptr);
        g_pages.emplace_back(NewHeadPage(0));
        g_pages.emplace_back(NewWheelPage(1));
        g_pages.emplace_back(NewSimPage(2));
        g_pages.emplace_back(NewDrawDistPage(3));
        g_pages.emplace_back(NewMainPage(4));
        for (size_t i = 0; i < g_pages.size(); ++i) {
            TCITEMW ti{}; ti.mask = TCIF_TEXT; ti.pszText = const_cast<wchar_t*>(g_pages[i]->Title());
            SendMessageW(g_tab, TCM_INSERTITEMW, i, (LPARAM)&ti);
            g_pages[i]->Create(hwnd);
        }
        g_status = CreateWindowExW(0, L"STATIC", L"", WS_CHILD | WS_VISIBLE | SS_LEFT | SS_CENTERIMAGE | SS_NOPREFIX,
                                   0, 0, 10, 10, hwnd, (HMENU)IDC_STATUS, G.inst, nullptr);
        g_apply = CreateWindowExW(0, L"BUTTON", L"Apply", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
                                  0, 0, 10, 10, hwnd, (HMENU)IDC_APPLY, G.inst, nullptr);
        g_revert = CreateWindowExW(0, L"BUTTON", L"Revert", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
                                   0, 0, 10, 10, hwnd, (HMENU)IDC_REVERT, G.inst, nullptr);
        g_defaults = CreateWindowExW(0, L"BUTTON", L"Defaults (this tab)", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
                                     0, 0, 10, 10, hwnd, (HMENU)IDC_DEFAULTS, G.inst, nullptr);
        SetWindowPos(g_tab, HWND_BOTTOM, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);   // pages draw above the tab body
        applyFontsToWindow(hwnd);
        for (auto& p : g_pages) p->Load();
        SetStatus(L"Settings are read from FH2SimImmersion.ini next to this program.");
        layoutAll(hwnd);
        showPage(0);
        SetTimer(hwnd, kTimer, 50, nullptr);
        return 0;
    }
    case WM_SIZE:
        if (g_tab && w != SIZE_MINIMIZED) layoutAll(hwnd);
        return 0;
    case WM_GETMINMAXINFO: {
        auto* mm = (MINMAXINFO*)l;
        mm->ptMinTrackSize.x = S(760);
        mm->ptMinTrackSize.y = S(520);
        return 0;
    }
    case WM_DPICHANGED: {
        G.dpi = HIWORD(w);
        makeFonts();
        const RECT* r = (const RECT*)l;
        SetWindowPos(hwnd, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top, SWP_NOZORDER | SWP_NOACTIVATE);
        applyFontsToWindow(hwnd);
        for (auto& p : g_pages) p->OnDpi();
        layoutAll(hwnd);
        return 0;
    }
    case WM_NOTIFY:
        if (((NMHDR*)l)->idFrom == IDC_TAB && ((NMHDR*)l)->code == TCN_SELCHANGE) {
            showPage(TabCtrl_GetCurSel(g_tab));
            return 0;
        }
        break;
    case WM_COMMAND:
        switch (LOWORD(w)) {
        case IDC_APPLY: doApply(); return 0;
        case IDC_REVERT: g_pages[g_cur]->Load(); SetStatus(L"Reverted this tab to the saved settings."); return 0;
        case IDC_DEFAULTS: g_pages[g_cur]->Defaults(); return 0;
        case IDOK: case IDCANCEL: return 0;       // Enter / Esc inside the window do nothing
        }
        break;
    case WM_TIMER:
        if (w == kTimer && g_cur < (int)g_pages.size()) g_pages[g_cur]->Tick();
        return 0;
    case WM_APP_STATUS: {
        std::unique_ptr<std::wstring> line((std::wstring*)l);
        if (MainPageApi* api = mainApi()) api->AddStatusLine(*line);
        return 0;
    }
    case WM_COPYDATA: {          // a second instance forwarded "azimuth.exe" to start
        auto* cds = (COPYDATASTRUCT*)l;
        if (cds->dwData == 1 && cds->lpData) startWithPath(std::wstring((const wchar_t*)cds->lpData, wcsnlen((const wchar_t*)cds->lpData, cds->cbData / sizeof(wchar_t))));
        return TRUE;
    }
    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        KillTimer(hwnd, kTimer);
        g_injector.Stop();
        g_pages.clear();
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, m, w, l);
}

} // namespace app

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, PWSTR, int show) {
    using namespace app;
    G.inst = inst;

    HANDLE mutex = CreateMutexW(nullptr, TRUE, L"Local\\FH2SimImmersionApp");
    bool already = GetLastError() == ERROR_ALREADY_EXISTS;
    int argc = 0;
    wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    std::wstring arg = argc > 1 ? argv[1] : L"";
    if (argv) LocalFree(argv);

    if (already) {            // another instance: bring it to the front and exit
        if (HWND other = FindWindowW(kClass, nullptr)) {
            if (IsIconic(other)) ShowWindow(other, SW_RESTORE);
            SetForegroundWindow(other);
            if (!arg.empty()) {
                COPYDATASTRUCT cds{1, (DWORD)((arg.size() + 1) * sizeof(wchar_t)), (PVOID)arg.c_str()};
                SendMessageW(other, WM_COPYDATA, 0, (LPARAM)&cds);
            }
        }
        if (mutex) CloseHandle(mutex);
        return 0;
    }

    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    INITCOMMONCONTROLSEX icc{sizeof(icc), ICC_TAB_CLASSES | ICC_LISTVIEW_CLASSES | ICC_BAR_CLASSES | ICC_STANDARD_CLASSES | ICC_WIN95_CLASSES};
    InitCommonControlsEx(&icc);
    EnsureIni();
    G.white = GetSysColorBrush(COLOR_WINDOW);
    RegisterPageClasses();

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = WndProc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszClassName = kClass;
    wc.hIcon = LoadIconW(inst, MAKEINTRESOURCEW(1));
    RegisterClassExW(&wc);

    UINT d = dpiOfWindow(nullptr);
    std::wstring title = L"FH2 Sim Immersion " + Widen(FH2SI_VERSION) + L" - settings";
    HWND hwnd = CreateWindowExW(WS_EX_CONTROLPARENT, kClass, title.c_str(), WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
                                CW_USEDEFAULT, CW_USEDEFAULT, MulDiv(900, d, 96), MulDiv(640, d, 96),
                                nullptr, nullptr, inst, nullptr);
    if (!hwnd) return 1;
    g_injector.Start(DllPath(), [hwnd](const std::wstring& s) {
        PostMessageW(hwnd, WM_APP_STATUS, 0, (LPARAM)new std::wstring(s));
    });
    ShowWindow(hwnd, show);
    UpdateWindow(hwnd);
    if (!arg.empty()) startWithPath(arg);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (IsDialogMessageW(hwnd, &msg)) continue;
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    CoUninitialize();
    if (mutex) CloseHandle(mutex);
    return (int)msg.wParam;
}
