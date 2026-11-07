// SPDX-License-Identifier: MIT
// Tiny parser for the default settings text (src/common/default_ini.h): [Section] / Key=Value lines,
// plus the comment lines above each key (used as tooltip text). Pure C++, no Windows headers.
#pragma once
#include <string>
#include <vector>

namespace app {

struct IniEntry {
    std::string section, key, value;
    std::string comment;     // comment lines directly above the key, joined with spaces
};

inline std::string trimStr(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n"), b = s.find_last_not_of(" \t\r\n");
    return a == std::string::npos ? std::string() : s.substr(a, b - a + 1);
}

// A comment block ends at a blank line. A key without its own comment block inherits the last
// comment of its section (or the comment block placed directly above the [Section] line), so a
// group of related keys shares one explanation.
inline std::vector<IniEntry> ParseIniText(const char* text) {
    std::vector<IniEntry> out;
    std::string section, pending, last;
    const char* p = text;
    while (*p) {
        const char* e = p;
        while (*e && *e != '\n') ++e;
        std::string line = trimStr(std::string(p, e));
        p = *e ? e + 1 : e;
        if (line.empty()) { pending.clear(); continue; }
        if (line[0] == ';') {
            std::string c = trimStr(line.substr(1));
            if (!pending.empty()) pending += ' ';
            pending += c;
            continue;
        }
        if (line[0] == '[') {
            size_t r = line.find(']');
            section = line.substr(1, r == std::string::npos ? std::string::npos : r - 1);
            last = pending;               // comment right above the header
            pending.clear();
            continue;
        }
        size_t eq = line.find('=');
        if (eq == std::string::npos) { pending.clear(); continue; }
        IniEntry en;
        en.section = section;
        en.key = trimStr(line.substr(0, eq));
        en.value = trimStr(line.substr(eq + 1));
        if (!pending.empty()) { last = pending; pending.clear(); }
        en.comment = last;
        out.push_back(en);
    }
    return out;
}

} // namespace app
