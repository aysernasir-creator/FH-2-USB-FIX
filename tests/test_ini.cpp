// SPDX-License-Identifier: MIT
// Unit test for the default-INI parser used by the settings window and for the settings tables.
// Build natively: g++ -std=c++17 tests/test_ini.cpp -o build/test_ini
#include "../src/common/default_ini.h"
#include "../src/common/ini_parse.h"
#include "../src/app/settings_tables.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <set>

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++failures; } } while (0)

int main() {
    auto defs = app::ParseIniText(fh2::kDefaultIni);
    CHECK(defs.size() > 80);

    // Independent count of "Key=Value" lines and sections in the raw text.
    size_t keyLines = 0;
    std::set<std::string> sections;
    for (const char* p = fh2::kDefaultIni; *p;) {
        const char* e = std::strchr(p, '\n');
        std::string line(p, e ? e : p + std::strlen(p));
        p = e ? e + 1 : p + line.size();
        if (line.empty() || line[0] == ';') continue;
        if (line[0] == '[') sections.insert(line.substr(1, line.find(']') - 1));
        else if (line.find('=') != std::string::npos) ++keyLines;
    }
    CHECK(keyLines == defs.size());

    std::set<std::string> seen;
    std::map<std::string, std::string> val;
    for (auto& e : defs) {
        CHECK(!e.section.empty());
        CHECK(!e.key.empty());
        CHECK(sections.count(e.section) == 1);
        CHECK(seen.insert(e.section + "." + e.key).second);      // no duplicates
        val[e.section + "." + e.key] = e.value;
    }
    CHECK(val["Input.Source"] == "auto");
    CHECK(val["Network.UdpPort"] == "4242");
    CHECK(val["Hotkeys.ToggleKey"] == "0x23");
    CHECK(val["Wheel.GearButtons"] == "12,13,14,15,16,17,-1");
    CHECK(val["Wheel.FFBTorqueGain"] == "2.0");
    CHECK(val["Simulation.OptionalAssists"] == "game");
    CHECK(val["Simulation.RealisticSTM"] == "0");
    CHECK(val["Simulation.RealisticClutch"] == "0");
    CHECK(val["Simulation.RealisticAutomatic"] == "0");
    CHECK(val["Wheel.OverrideAzimuthWheel"] == "1");
    CHECK(val["General.OverrideAzimuthHeadTracking"] == "1");
    CHECK(val.count("Launcher.AzimuthPath") == 1 && val["Launcher.AzimuthPath"].empty());
    CHECK(val["Rotation.YawLimit"] == "90");

    // Comments: own block, inherited block, section header block.
    for (auto& e : defs) {
        if (e.section == "Input" && e.key == "Diagnostics") CHECK(e.comment.find("troubleshooting") != std::string::npos);
        if (e.section == "Wheel" && e.key == "FFBSlipFull") CHECK(e.comment.find("Rear slip") != std::string::npos);
        if (e.section == "Rotation" && e.key == "YawGain") CHECK(e.comment.find("Gain multiplies") != std::string::npos);
        if (e.section == "Input" && e.key == "ConfigVersion") CHECK(e.comment.empty());
    }

    // ConfigVersion in the text equals the constant used by the plugin and the app.
    CHECK(atoi(val["Input.ConfigVersion"].c_str()) == fh2::kConfigVersion);

    // Every key of the default text is editable on exactly one tab (ConfigVersion excepted).
    std::map<std::string, int> covered;
    for (auto& t : app::kAllTables)
        for (size_t i = 0; i < t.count; ++i) {
            const app::Setting& s = t.rows[i];
            std::string id = std::string(s.section) + "." + s.key;
            ++covered[id];
            CHECK(val.count(id) == 1);                              // table row refers to a real key
            if (s.type == app::SType::Enum) {
                CHECK(s.values != nullptr);
                // the default value is one of the enum choices
                std::string vs = std::string("|") + s.values + "|", d = "|" + val[id] + "|";
                CHECK(vs.find(d) != std::string::npos);
            }
            if (s.type == app::SType::Int || s.type == app::SType::Float) {
                if (s.max > s.min) {
                    double d = atof(val[id].c_str());
                    CHECK(d >= s.min && d <= s.max);                // default inside the editable range
                }
            }
        }
    for (auto& e : defs) {
        std::string id = e.section + "." + e.key;
        if (id == "Input.ConfigVersion") { CHECK(covered.count(id) == 0); continue; }
        if (covered[id] != 1) { std::printf("key %s is on %d tabs\n", id.c_str(), covered[id]); ++failures; }
    }

    if (failures) { std::printf("%d failure(s)\n", failures); return 1; }
    std::printf("test_ini: all checks passed (%zu keys)\n", defs.size());
    return 0;
}
