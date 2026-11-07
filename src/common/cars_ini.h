// SPDX-License-Identifier: MIT
// Line parsers for FH2SimImmersion_cars.ini, shared by the plugin and the settings window.
// Pure C++, no Windows headers.
#pragma once
#include <cstdint>
#include <cwchar>
#include <cstdlib>
#include <string>

namespace fh2 {

enum class CarOvr : uint8_t { Auto, On, Off, Automatic };
struct CarAssistLine { unsigned id = 0; CarOvr abs = CarOvr::Auto, tc = CarOvr::Auto, stm = CarOvr::Auto, clutch = CarOvr::Auto; };

namespace carsini_detail {
inline CarOvr word(const std::wstring& s, bool clutch) {
    std::wstring t;
    for (wchar_t c : s) if (c != L' ' && c != L'\t') t += (wchar_t)((c >= L'A' && c <= L'Z') ? c + 32 : c);
    return t == L"on" ? CarOvr::On : t == L"off" ? CarOvr::Off : (clutch && t == L"automatic") ? CarOvr::Automatic : CarOvr::Auto;
}
// Position of '=' and the leading id (> 0: only digits, spaces/tabs around it, then '='), or npos.
inline size_t idAndEq(const std::wstring& line, unsigned& id) {
    size_t eq = line.find(L'=');
    if (eq == std::wstring::npos) return eq;
    size_t s = line.find_first_not_of(L" \t");
    if (s == eq || line[s] < L'0' || line[s] > L'9') return std::wstring::npos;
    wchar_t* end = nullptr;
    unsigned long v = wcstoul(line.c_str() + s, &end, 10);
    while (*end == L' ' || *end == L'\t') ++end;
    if (end != line.c_str() + eq || v == 0) return std::wstring::npos;
    id = (unsigned)v;
    return eq;
}
} // namespace carsini_detail

// "Id=ABS,TC,STM,CLUTCH" (one line of [Cars] as GetPrivateProfileSection returns it). false = no '=', or the id is not a positive decimal number.
inline bool ParseCarAssistLine(const std::wstring& line, CarAssistLine& out) {
    unsigned id = 0;
    size_t eq = carsini_detail::idAndEq(line, id);
    if (eq == std::wstring::npos) return false;
    out = CarAssistLine();
    out.id = id;
    CarOvr* f[4] = {&out.abs, &out.tc, &out.stm, &out.clutch};
    size_t i = eq + 1;
    for (int k = 0; k < 4; ++k) {
        size_t j = line.find(L',', i);   // extra fields after the 4th are ignored
        *f[k] = carsini_detail::word(line.substr(i, j == std::wstring::npos ? j : j - i), k == 3);
        if (j == std::wstring::npos) break;
        i = j + 1;
    }
    return true;
}

// "Id=degrees" of [SteeringLock]: id > 0, degrees 90..3600, nothing but spaces/tabs after the number.
inline bool ParseSteeringLockLine(const std::wstring& line, unsigned& id, unsigned& deg) {
    unsigned i = 0;
    size_t eq = carsini_detail::idAndEq(line, i);
    if (eq == std::wstring::npos) return false;
    const wchar_t* v = line.c_str() + eq + 1;
    wchar_t* e = nullptr;
    unsigned long d = wcstoul(v, &e, 10);
    if (e == v) return false;
    while (*e == L' ' || *e == L'\t') ++e;
    if (*e != 0 || d < 90 || d > 3600) return false;
    id = i; deg = (unsigned)d;
    return true;
}

} // namespace fh2
