// SPDX-License-Identifier: MIT
// GDI pictures of the settings window (no image files): head axes and steering wheel parts.
#pragma once
#include <windows.h>
#include <map>
#include <string>
#include <vector>

namespace app {

struct HeadPicState {
    std::string part;                                     // yaw pitch roll x y z, rot / pos = all of them
    double yawLim = 90, pitchLim = 40, rollLim = 20;      // degrees
    double xLim = 0.10, yLim = 0.06, zLim = 0.08;         // metres
};
void DrawHeadPicture(HDC dc, const RECT& rc, const HeadPicState& st, int dpi, HFONT font);

struct WheelLive {
    bool active = false;
    std::wstring device;               // device name or a message
    double steer = 0.5, throttle = 0, brake = 0, clutch = 0, handbrake = 0;   // 0..1
    std::vector<int> pressed;
};
struct WheelPicState {
    std::string part;                  // steer throttle brake clutch pedals btnA btnB btnX btnY view menu lb rb shifter handbrake ffb
    std::map<std::string, std::wstring> label;   // text shown for each part (the controlling setting)
    double rangeDeg = 900;
    WheelLive live;
};
void DrawWheelPicture(HDC dc, const RECT& rc, const WheelPicState& st, int dpi, HFONT font);

struct DrawDistPicState {
    std::string part;                  // shadow world props crowd grass traffic racer
    // effective distances in metres (the game's own value where the setting is the game default)
    double shadow = 85, world = 360, props = 450, crowd = 250, grass = 100, traffic = 150, racer = 75;
};
void DrawDrawDistPicture(HDC dc, const RECT& rc, const DrawDistPicState& st, int dpi, HFONT font);

} // namespace app
