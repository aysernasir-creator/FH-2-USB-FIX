// SPDX-License-Identifier: MIT
// Host-side unit tests for the cars INI line parsers (g++ -std=c++17 tests/test_cars.cpp).
#include <cstdio>
#include "../src/common/cars_ini.h"

using namespace fh2;

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++failures; } } while (0)

int main() {
    CarAssistLine l;
    CHECK(ParseCarAssistLine(L"123=on,off,auto,automatic", l) && l.id == 123 && l.abs == CarOvr::On && l.tc == CarOvr::Off &&
          l.stm == CarOvr::Auto && l.clutch == CarOvr::Automatic);
    CHECK(ParseCarAssistLine(L"5=on", l) && l.id == 5 && l.abs == CarOvr::On && l.tc == CarOvr::Auto && l.clutch == CarOvr::Auto);
    CHECK(ParseCarAssistLine(L"5=,off", l) && l.abs == CarOvr::Auto && l.tc == CarOvr::Off);
    CHECK(ParseCarAssistLine(L"5=", l) && l.abs == CarOvr::Auto);
    CHECK(ParseCarAssistLine(L"7= ON , Off ,\tOFF , On ", l) && l.abs == CarOvr::On && l.tc == CarOvr::Off && l.stm == CarOvr::Off && l.clutch == CarOvr::On);
    CHECK(ParseCarAssistLine(L"7=automatic,,,AUTOMATIC", l) && l.abs == CarOvr::Auto && l.clutch == CarOvr::Automatic);
    CHECK(ParseCarAssistLine(L"7=on,on,on,off,on", l) && l.clutch == CarOvr::Off);   // extra field ignored
    CHECK(!ParseCarAssistLine(L"x=on", l));
    CHECK(!ParseCarAssistLine(L"0=on", l));
    CHECK(!ParseCarAssistLine(L"=on", l));
    CHECK(!ParseCarAssistLine(L"123 on", l));
    CHECK(!ParseCarAssistLine(L"12a=off", l));
    CHECK(!ParseCarAssistLine(L"-5=on", l));
    CHECK(ParseCarAssistLine(L" 5 =on", l) && l.id == 5);

    unsigned id = 0, deg = 0;
    CHECK(ParseSteeringLockLine(L"123=540", id, deg) && id == 123 && deg == 540);
    CHECK(ParseSteeringLockLine(L"123= 540 ", id, deg) && deg == 540);
    CHECK(!ParseSteeringLockLine(L"123=540x", id, deg));
    CHECK(!ParseSteeringLockLine(L"12a=540", id, deg));
    CHECK(!ParseSteeringLockLine(L"123=89", id, deg));
    CHECK(!ParseSteeringLockLine(L"123=3601", id, deg));
    CHECK(ParseSteeringLockLine(L"9=90", id, deg) && ParseSteeringLockLine(L"9=3600", id, deg));
    CHECK(!ParseSteeringLockLine(L"0=540", id, deg));
    CHECK(!ParseSteeringLockLine(L"123=", id, deg));
    CHECK(!ParseSteeringLockLine(L"123", id, deg));

    if (failures) { std::printf("%d failure(s)\n", failures); return 1; }
    std::printf("cars ini tests passed\n");
    return 0;
}
