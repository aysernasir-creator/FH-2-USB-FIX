// SPDX-License-Identifier: MIT
// Watches for azimuth.exe and loads FH2SimImmersion.dll into it. Runs in a background thread
// while the settings window is open. Status lines are reported through a callback (called from
// the background thread).
#pragma once
#include <windows.h>
#include <functional>
#include <string>

namespace app {

class Injector {
public:
    using StatusFn = std::function<void(const std::wstring&)>;
    Injector() = default;
    ~Injector() { Stop(); }
    void Start(const std::wstring& dllPath, StatusFn status);
    void Stop();
private:
    static DWORD WINAPI ThreadMain(LPVOID self);
    void Run();
    HANDLE thread_ = nullptr, stop_ = nullptr;
    std::wstring dll_;
    StatusFn status_;
};

// Starts azimuth.exe in its own folder. On failure returns false and fills `error`.
bool StartAzimuth(const std::wstring& exe, std::wstring& error);

} // namespace app
