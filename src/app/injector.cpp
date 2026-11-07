// SPDX-License-Identifier: MIT
// Injector: finds azimuth.exe and loads FH2SimImmersion.dll into it.
#include "injector.h"
#include <tlhelp32.h>
#include <psapi.h>
#include <algorithm>
#include <set>
#include <vector>

namespace app {

static std::vector<DWORD> findAzimuth() {
    std::vector<DWORD> out;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return out;
    PROCESSENTRY32W pe; pe.dwSize = sizeof(pe);
    if (Process32FirstW(snap, &pe)) do {
        if (_wcsicmp(pe.szExeFile, L"azimuth.exe") == 0) out.push_back(pe.th32ProcessID);
    } while (Process32NextW(snap, &pe));
    CloseHandle(snap);
    return out;
}

static bool hasModule(HANDLE proc, const std::wstring& dllPath) {
    HMODULE mods[1024]; DWORD need = 0;
    if (!EnumProcessModulesEx(proc, mods, sizeof(mods), &need, LIST_MODULES_64BIT)) return false;
    need = std::min<DWORD>(need, sizeof(mods));   // more modules than fit: check the ones returned
    for (DWORD i = 0; i < need / sizeof(HMODULE); ++i) {
        wchar_t name[MAX_PATH];
        if (GetModuleFileNameExW(proc, mods[i], name, MAX_PATH) && _wcsicmp(name, dllPath.c_str()) == 0) return true;
    }
    return false;
}

static std::wstring fmt(const wchar_t* f, DWORD a, DWORD b = 0) {
    wchar_t buf[256];
    _snwprintf(buf, 256, f, a, b);
    return buf;
}

static bool inject(DWORD pid, const std::wstring& dllPath, const Injector::StatusFn& say) {
    HANDLE proc = OpenProcess(PROCESS_CREATE_THREAD | PROCESS_QUERY_INFORMATION | PROCESS_VM_OPERATION |
                              PROCESS_VM_WRITE | PROCESS_VM_READ, FALSE, pid);
    if (!proc) { say(fmt(L"could not open azimuth.exe (pid %lu), error %lu", pid, GetLastError())); return false; }
    if (hasModule(proc, dllPath)) { say(fmt(L"pid %lu already has FH2 Sim Immersion", pid)); CloseHandle(proc); return true; }
    size_t bytes = (dllPath.size() + 1) * sizeof(wchar_t);
    void* remote = VirtualAllocEx(proc, nullptr, bytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    bool ok = false;
    bool stillLoading = false;   // timed out: the remote LoadLibraryW may still read the path, so keep it allocated
    if (remote && WriteProcessMemory(proc, remote, dllPath.c_str(), bytes, nullptr)) {
        auto loadLib = reinterpret_cast<LPTHREAD_START_ROUTINE>(
            GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "LoadLibraryW"));
        HANDLE t = CreateRemoteThread(proc, nullptr, 0, loadLib, remote, 0, nullptr);
        if (t) {
            DWORD w = WaitForSingleObject(t, 15000);
            if (w == WAIT_OBJECT_0) {
                DWORD code = 0; GetExitCodeThread(t, &code);
                ok = code != 0;
            } else {
                stillLoading = true;
            }
            CloseHandle(t);
        }
    }
    if (remote && !stillLoading) VirtualFreeEx(proc, remote, 0, MEM_RELEASE);
    CloseHandle(proc);
    if (stillLoading) say(fmt(L"loading into azimuth.exe (pid %lu) did not finish within 15 s", pid));
    else say(ok ? fmt(L"FH2 Sim Immersion loaded into azimuth.exe (injected into pid %lu)", pid)
                : fmt(L"FAILED to load into azimuth.exe (pid %lu) - antivirus? run as the same user as Azimuth", pid));
    return ok;
}

void Injector::Start(const std::wstring& dllPath, StatusFn status) {
    Stop();
    dll_ = dllPath;
    status_ = std::move(status);
    stop_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    thread_ = CreateThread(nullptr, 0, ThreadMain, this, 0, nullptr);
}

void Injector::Stop() {
    if (thread_) {
        SetEvent(stop_);
        WaitForSingleObject(thread_, 20000);
        CloseHandle(thread_); thread_ = nullptr;
    }
    if (stop_) { CloseHandle(stop_); stop_ = nullptr; }
}

DWORD WINAPI Injector::ThreadMain(LPVOID self) { static_cast<Injector*>(self)->Run(); return 0; }

void Injector::Run() {
    if (GetFileAttributesW(dll_.c_str()) == INVALID_FILE_ATTRIBUTES) {
        status_(L"FH2SimImmersion.dll not found next to this program: " + dll_);
        return;
    }
    status_(L"Watching for azimuth.exe...");
    std::set<DWORD> done;
    for (;;) {
        std::vector<DWORD> pids = findAzimuth();
        for (auto it = done.begin(); it != done.end();) {   // forget exited processes so a reused PID is injected
            if (std::find(pids.begin(), pids.end(), *it) == pids.end()) it = done.erase(it);
            else ++it;
        }
        for (DWORD pid : pids) {
            if (done.count(pid)) continue;
            if (WaitForSingleObject(stop_, 1000) == WAIT_OBJECT_0) return;   // let the new process finish starting up
            inject(pid, dll_, status_);
            done.insert(pid);
        }
        if (WaitForSingleObject(stop_, 1000) == WAIT_OBJECT_0) return;
    }
}

bool StartAzimuth(const std::wstring& exe, std::wstring& error) {
    std::wstring dir = exe.substr(0, exe.find_last_of(L"\\/"));
    STARTUPINFOW si{}; si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    std::wstring cmd = L"\"" + exe + L"\"";
    if (!CreateProcessW(exe.c_str(), cmd.data(), nullptr, nullptr, FALSE, 0, nullptr, dir.c_str(), &si, &pi)) {
        error = fmt(L"Could not start Azimuth (error %lu)", GetLastError());
        return false;
    }
    CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
    return true;
}

} // namespace app
