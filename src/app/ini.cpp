// SPDX-License-Identifier: MIT
// Settings file access for the settings window (FH2SimImmersion.ini).
#include "ini.h"
#include "../common/default_ini.h"
#include "../common/ini_migrate.h"

namespace app {

std::wstring Widen(const std::string& s) {
    if (s.empty()) return std::wstring();
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), w.data(), n);
    return w;
}
std::string Narrow(const std::wstring& w) {
    if (w.empty()) return std::string();
    int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), s.data(), n, nullptr, nullptr);
    return s;
}

std::wstring ExeDir() {
    wchar_t p[MAX_PATH];
    DWORD n = GetModuleFileNameW(nullptr, p, MAX_PATH);
    std::wstring s(p, n);
    return s.substr(0, s.find_last_of(L"\\/"));
}
std::wstring IniPath()     { return ExeDir() + L"\\FH2SimImmersion.ini"; }
std::wstring CarsIniPath() { return ExeDir() + L"\\FH2SimImmersion_cars.ini"; }
std::wstring LogPath()     { return ExeDir() + L"\\FH2SimImmersion.log"; }
std::wstring DllPath()     { return ExeDir() + L"\\FH2SimImmersion.dll"; }

const std::vector<IniEntry>& Defaults() {
    static const std::vector<IniEntry> d = ParseIniText(fh2::kDefaultIni);
    return d;
}

const IniEntry* FindDefault(const char* section, const char* key) {
    for (auto& e : Defaults()) if (e.section == section && e.key == key) return &e;
    return nullptr;
}

void EnsureIni() { fh2::EnsureCurrentIni(IniPath()); }

std::string ReadSetting(const char* section, const char* key) {
    const IniEntry* d = FindDefault(section, key);
    std::wstring def = Widen(d ? d->value : std::string());
    std::vector<wchar_t> buf(4096);
    GetPrivateProfileStringW(Widen(section).c_str(), Widen(key).c_str(), def.c_str(), buf.data(),
                             (DWORD)buf.size(), IniPath().c_str());
    return trimStr(Narrow(buf.data()));
}

bool WriteSetting(const char* section, const char* key, const std::string& value) {
    return WritePrivateProfileStringW(Widen(section).c_str(), Widen(key).c_str(), Widen(value).c_str(),
                                      IniPath().c_str()) != 0;
}

} // namespace app
