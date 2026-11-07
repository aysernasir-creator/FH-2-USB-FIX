// SPDX-License-Identifier: MIT
// The settings of every tab. Adding a row to a table adds a control; tests/test_ini.cpp checks
// that every key of the default INI text appears in exactly one table.
#pragma once
#include "settings.h"

namespace app {

inline constexpr Setting kHeadSettings[] = {
    ST_BOOL ("General", "EnableOnStartup", "Head tracking on at game start", "Head tracking", "rot"),
    ST_ENUM ("Input", "Source", "Tracker input", "Head tracking", "rot", "auto|freetrack|udp"),
    ST_BOOL ("General", "OverrideAzimuthHeadTracking", "Override Azimuth head tracking if it adds its own", "Head tracking", "rot"),
    ST_BOOL ("General", "CockpitOnly", "Cockpit camera only", "Head tracking", "rot"),
    ST_FLOAT("General", "Smoothing", "Smoothing (0 - 1)", "Head tracking", "rot", 0, 1, 0.05),
    ST_INT  ("General", "StaleTimeoutMs", "Lost-signal timeout (ms)", "Head tracking", "rot", 50, 60000, 50),

    ST_INT  ("Network", "UdpPort", "UDP port", "Network (OpenTrack UDP)", nullptr, 1, 65535, 1),
    ST_TEXT ("Network", "BindAddress", "Bind address", "Network (OpenTrack UDP)", nullptr),

    ST_FLOAT("Rotation", "YawGain", "Yaw gain", "Rotation", "yaw", 0, 5, 0.1),
    ST_BOOL ("Rotation", "YawInvert", "Yaw invert", "Rotation", "yaw"),
    ST_FLOAT("Rotation", "YawLimit", "Yaw limit (degrees)", "Rotation", "yaw", 0, 180, 5),
    ST_FLOAT("Rotation", "YawLeftGain", "Yaw gain, looking left", "Rotation", "yaw", 0, 5, 0.1),
    ST_FLOAT("Rotation", "YawRightGain", "Yaw gain, looking right", "Rotation", "yaw", 0, 5, 0.1),
    ST_BOOL ("Rotation", "YawLeftIsPositive", "Left is positive yaw", "Rotation", "yaw"),
    ST_FLOAT("Rotation", "PitchGain", "Pitch gain", "Rotation", "pitch", 0, 5, 0.1),
    ST_BOOL ("Rotation", "PitchInvert", "Pitch invert", "Rotation", "pitch"),
    ST_FLOAT("Rotation", "PitchLimit", "Pitch limit (degrees)", "Rotation", "pitch", 0, 90, 5),
    ST_FLOAT("Rotation", "RollGain", "Roll gain", "Rotation", "roll", 0, 5, 0.1),
    ST_BOOL ("Rotation", "RollInvert", "Roll invert", "Rotation", "roll"),
    ST_FLOAT("Rotation", "RollLimit", "Roll limit (degrees)", "Rotation", "roll", 0, 90, 5),

    ST_BOOL ("Position", "Enabled", "Lean (position) enabled", "Position (lean)", "pos"),
    ST_FLOAT("Position", "XGain", "X (left/right) gain", "Position (lean)", "x", 0, 5, 0.1),
    ST_BOOL ("Position", "XInvert", "X invert", "Position (lean)", "x"),
    ST_FLOAT("Position", "XLimit", "X limit (metres)", "Position (lean)", "x", 0, 0.5, 0.01),
    ST_FLOAT("Position", "YGain", "Y (up/down) gain", "Position (lean)", "y", 0, 5, 0.1),
    ST_BOOL ("Position", "YInvert", "Y invert", "Position (lean)", "y"),
    ST_FLOAT("Position", "YLimit", "Y limit (metres)", "Position (lean)", "y", 0, 0.5, 0.01),
    ST_FLOAT("Position", "ZGain", "Z (forward/back) gain", "Position (lean)", "z", 0, 5, 0.1),
    ST_BOOL ("Position", "ZInvert", "Z invert", "Position (lean)", "z"),
    ST_FLOAT("Position", "ZLimit", "Z limit (metres)", "Position (lean)", "z", 0, 0.5, 0.01),

    ST_TEXT   ("Hotkeys", "ToggleKey", "Toggle key (virtual key, hex)", "Hotkeys and recenter", nullptr),
    ST_TEXT   ("Hotkeys", "RecenterKey", "Recenter key (0 = none)", "Hotkeys and recenter", nullptr),
    ST_TEXT   ("Hotkeys", "ChordToggleKey", "Ctrl+Shift+ toggle key", "Hotkeys and recenter", nullptr),
    ST_TEXT   ("Hotkeys", "ChordRecenterKey", "Ctrl+Shift+ recenter key", "Hotkeys and recenter", nullptr),
    ST_BUTTONS("Hotkeys", "RecenterButtons", "Controller recenter buttons", "Hotkeys and recenter", nullptr),
    ST_BUTTONS("Hotkeys", "WheelRecenterButtons", "Wheel recenter buttons", "Hotkeys and recenter", nullptr),

    ST_ENUM("Advanced", "RotOrder", "Rotation order (yaw, pitch, roll)", "Advanced", "rot", "0,1,2|0,2,1|1,0,2|1,2,0|2,0,1|2,1,0"),
    ST_ENUM("Advanced", "PosOrder", "Position order (X, Y, Z)", "Advanced", "pos", "0,1,2|0,2,1|1,0,2|1,2,0|2,0,1|2,1,0"),
};

#define AXES "X|Y|Z|RX|RY|RZ|SLIDER0|SLIDER1|NONE"

inline constexpr Setting kWheelSettings[] = {
    ST_BOOL ("Wheel", "Enabled", "Steering wheel support", "Wheel", nullptr),
    ST_TEXT ("Wheel", "DeviceMatch", "Device name contains", "Wheel", nullptr),
    ST_TEXT ("Wheel", "PedalsDevice", "Pedals device name contains (empty = the wheel)", "Wheel", nullptr),
    ST_TEXT ("Wheel", "ShifterDevice", "Shifter device name contains (empty = the wheel)", "Wheel", nullptr),
    ST_TEXT ("Wheel", "HandbrakeDevice", "Handbrake device name contains (empty = the wheel)", "Wheel", nullptr),
    ST_INT  ("Wheel", "WheelRange", "Wheel range (degrees)", "Wheel", "steer", 40, 65535, 10),

    ST_ENUM ("Wheel", "SteeringAxis", "Steering axis", "Axes", "steer", AXES),
    ST_BOOL ("Wheel", "SteeringInvert", "Steering invert", "Axes", "steer"),
    ST_ENUM ("Wheel", "ThrottleAxis", "Throttle axis", "Axes", "throttle", AXES),
    ST_ENUM ("Wheel", "BrakeAxis", "Brake axis", "Axes", "brake", AXES),
    ST_ENUM ("Wheel", "ClutchAxis", "Clutch axis", "Axes", "clutch", AXES),
    ST_BOOL ("Wheel", "PedalsInverted", "Pedals read maximum when released", "Axes", "pedals"),
    ST_FLOAT("Wheel", "PedalDeadzone", "Pedal deadzone (0 - 0.9)", "Axes", "pedals", 0, 0.9, 0.01),

    ST_INT  ("Wheel", "ButtonA", "Button A", "Buttons (DirectInput numbers, -1 = none)", "btnA", -1, 127, 1),
    ST_INT  ("Wheel", "ButtonB", "Button B", "Buttons (DirectInput numbers, -1 = none)", "btnB", -1, 127, 1),
    ST_INT  ("Wheel", "ButtonX", "Button X", "Buttons (DirectInput numbers, -1 = none)", "btnX", -1, 127, 1),
    ST_INT  ("Wheel", "ButtonY", "Button Y", "Buttons (DirectInput numbers, -1 = none)", "btnY", -1, 127, 1),
    ST_INT  ("Wheel", "ButtonView", "Button View", "Buttons (DirectInput numbers, -1 = none)", "view", -1, 127, 1),
    ST_INT  ("Wheel", "ButtonMenu", "Button Menu", "Buttons (DirectInput numbers, -1 = none)", "menu", -1, 127, 1),
    ST_INT  ("Wheel", "ButtonLB", "Button LB (left paddle)", "Buttons (DirectInput numbers, -1 = none)", "lb", -1, 127, 1),
    ST_INT  ("Wheel", "ButtonRB", "Button RB (right paddle)", "Buttons (DirectInput numbers, -1 = none)", "rb", -1, 127, 1),

    ST_BOOL ("Wheel", "HShifter", "H-shifter connected", "H-shifter", "shifter"),
    ST_TEXT ("Wheel", "GearButtons", "Gear buttons 1-6 and 7th", "H-shifter", "shifter"),
    ST_INT  ("Wheel", "ReverseButton", "Reverse button", "H-shifter", "shifter", -1, 127, 1),

    ST_INT  ("Wheel", "HandbrakeButton", "Handbrake button (-1 = none)", "Handbrake", "handbrake", -1, 127, 1),
    ST_ENUM ("Wheel", "HandbrakeAxis", "Handbrake axis", "Handbrake", "handbrake", AXES),
    ST_BOOL ("Wheel", "HandbrakeAxisInvert", "Axis reads maximum when released", "Handbrake", "handbrake"),
    ST_FLOAT("Wheel", "HandbrakeThreshold", "Pulled at axis travel (0.05 - 0.95)", "Handbrake", "handbrake", 0.05, 0.95, 0.05),
    ST_ENUM ("Wheel", "HandbrakeGameButton", "Game button it presses", "Handbrake", "handbrake", "A|B|X|Y|LB|RB"),

    ST_ENUM ("Wheel", "UserMode", "Player of the wheel", "Player", nullptr, "auto|none"),
    ST_BOOL ("Wheel", "ForceRescan", "Re-scan controllers once", "Player", nullptr),

    ST_BOOL ("Wheel", "FFB", "Force feedback", "Force feedback", "ffb"),
    ST_FLOAT("Wheel", "FFBGain", "Strength", "Force feedback", "ffb", 0, 4, 0.05),
    ST_BOOL ("Wheel", "FFBInvert", "Invert force direction", "Force feedback", "ffb"),
    ST_FLOAT("Wheel", "FFBTorqueGain", "Steering torque", "Force feedback", "ffb", 0, 5, 0.1),
    ST_FLOAT("Wheel", "FFBSpringGain", "Centring spring", "Force feedback", "ffb", 0, 5, 0.1),
    ST_FLOAT("Wheel", "FFBSlipSpringFloor", "Spring kept in a slide (0 - 1)", "Force feedback", "ffb", 0, 1, 0.05),
    ST_FLOAT("Wheel", "FFBSlipGain", "Counter-steer force", "Force feedback", "ffb", 0, 5, 0.1),
    ST_FLOAT("Wheel", "FFBSlipStart", "Counter-steer starts at slip", "Force feedback", "ffb", 0, 2, 0.01),
    ST_FLOAT("Wheel", "FFBSlipFull", "Counter-steer full at slip", "Force feedback", "ffb", 0, 2, 0.01),
    ST_BOOL ("Wheel", "FFBSlipInvert", "Counter-steer toward travel (G29)", "Force feedback", "ffb"),
    ST_FLOAT("Wheel", "FFBAlignGain", "Front-slip aligning force", "Force feedback", "ffb", 0, 5, 0.1),
    ST_FLOAT("Wheel", "FFBAlignStart", "Aligning force starts at front slip", "Force feedback", "ffb", 0, 2, 0.01),
    ST_FLOAT("Wheel", "FFBAlignFull", "Aligning force full at front slip", "Force feedback", "ffb", 0, 2, 0.01),
    ST_FLOAT("Wheel", "FFBRumbleGain", "Road vibration strength", "Force feedback", "ffb", 0, 5, 0.1),
    ST_FLOAT("Wheel", "FFBLockStrength", "Soft lock strength", "Force feedback", "ffb", 0, 1, 0.05),
    ST_FLOAT("Wheel", "FFBLockRampDeg", "Soft lock ramp (degrees)", "Force feedback", "ffb", 0.5, 30, 0.5),

    ST_TEXT ("Wheel", "VendorId", "Reported vendor id (hex)", "Advanced", nullptr),
    ST_TEXT ("Wheel", "ProductId", "Reported product id (hex)", "Advanced", nullptr),
    ST_INT  ("Wheel", "ConfigFlag14", "ConfigFlag14", "Advanced", nullptr, 0, 255, 1),
    ST_BOOL ("Wheel", "OverrideAzimuthWheel", "Override Azimuth's wheel if it adds one", "Advanced", nullptr),
    ST_BOOL ("Wheel", "TestMode", "Test mode", "Advanced", nullptr),
};

inline constexpr Setting kSimSettings[] = {
    ST_SLIDER("Simulation", "FovCockpit", "Cockpit camera (extra degrees)", "Field of view", nullptr, -30, 60, 1),
    ST_SLIDER("Simulation", "FovDriver", "Driver camera (extra degrees)", "Field of view", nullptr, -30, 60, 1),
    ST_SLIDER("Simulation", "FovHood", "Hood camera (extra degrees)", "Field of view", nullptr, -30, 60, 1),
    ST_SLIDER("Simulation", "FovBumper", "Bumper camera (extra degrees)", "Field of view", nullptr, -30, 60, 1),

    ST_BOOL ("Simulation", "RealisticABS", "Realistic ABS (per car)", "Realistic assists", nullptr),
    ST_BOOL ("Simulation", "RealisticTC", "Realistic traction control (per car)", "Realistic assists", nullptr),
    ST_BOOL ("Simulation", "RealisticSTM", "Realistic stability control (per car)", "Realistic assists", nullptr),
    ST_BOOL ("Simulation", "RealisticClutch", "Realistic clutch (per car)", "Realistic assists", nullptr),
    {"Simulation", "RealisticAutomatic", SType::Bool, "Automatic-only cars shift automatically (per car)",
     "Cars that only had a traditional automatic gearbox (no manual mode) or are single-speed electric always use automatic shifting.",
     0, 0, 0, nullptr, "Realistic assists", nullptr, false},
    ST_ENUM ("Simulation", "OptionalAssists", "Cars where the aid was optional", "Realistic assists", nullptr, "game|on|off"),

    {"Simulation", "RealisticSteeringLock", SType::Bool, "Real steering lock (per car)",
     "Full steering happens at the real car's lock-to-lock rotation (see the Lock column of the car list) instead of the game's own rotation setting.",
     0, 0, 0, nullptr, "Steering wheel", nullptr, false},
    {"Simulation", "MaxWheelRotation", SType::Int, "Max wheel rotation (degrees, 0 = WheelRange)",
     "The most your wheel may turn, lock to lock, in degrees (G29: 900). 0 = the WheelRange of the Wheel tab; larger values are limited to WheelRange.",
     0, 65535, 10, nullptr, "Steering wheel", nullptr, false},
    {"Simulation", "SteeringLockMode", SType::Enum, "Steering lock mode (cap / scale)",
     "cap: only cars with a lock above the maximum use the maximum, the others stay 1:1 (maximum 900: a 1080 degree truck uses 900, a 540 degree race car stays 540). "
     "scale: every car is scaled so the car with the largest lock uses the maximum (maximum 900: 1908 becomes 900, 540 becomes 255).",
     0, 0, 0, "cap|scale", "Steering wheel", nullptr, false},
    {"Simulation", "RealCockpitWheel", SType::Bool, "In-game wheel turns like the real one",
     "The steering wheel of the cockpit model turns as far as the real car's wheel (instead of at most 90 degrees each way). Use it with hiding the driver's arms.",
     0, 0, 0, nullptr, "Steering wheel", nullptr, false},
    {"Simulation", "HideDriverInCockpit", SType::Bool, "Hide the driver's arms in the cockpit view",
     "Hides the driver (arms and hands) while the cockpit camera is active; the other cameras show the driver as usual.",
     0, 0, 0, nullptr, "Steering wheel", nullptr, false},
    {"Simulation", "CockpitWheelDirection", SType::Enum, "In-game wheel direction (1 or -1)",
     "Direction of the in-game wheel for the option above: 1 is normal, -1 only if the in-game wheel turns the wrong way.",
     0, 0, 0, "1|-1", "Advanced", nullptr, false},
};

// Draw distance (metres). The INI stores 0 = game default; the page shows a slider from the game's own
// value up to the maximum (pages_draw.cpp), the table range here is the range of the stored value.
inline constexpr Setting kDrawDistSettings[] = {
    ST_SLIDER("DrawDistance", "ShadowDistance",  "Shadows (game: 85 m)",         "Draw distance (metres)", "shadow",  0, 1000, 1),
    ST_SLIDER("DrawDistance", "WorldDistance",   "World objects (game: 360 m)",  "Draw distance (metres)", "world",   0, 3000, 1),
    ST_SLIDER("DrawDistance", "PropsDistance",   "Props (game: 450 m)",          "Draw distance (metres)", "props",   0, 3000, 1),
    ST_SLIDER("DrawDistance", "CrowdDistance",   "Crowd (game: 250 m)",          "Draw distance (metres)", "crowd",   0, 1000, 1),
    ST_SLIDER("DrawDistance", "GrassDistance",   "Grass (game: 100 m)",          "Draw distance (metres)", "grass",   0, 500, 1),
    ST_SLIDER("DrawDistance", "TrafficDistance", "Traffic cars (game: 150 m)",   "Draw distance (metres)", "traffic", 0, 500, 1),
    ST_SLIDER("DrawDistance", "RacerDistance",   "Racer cars, full detail (game: 75 m)", "Draw distance (metres)", "racer", 0, 1000, 1),
};

inline constexpr Setting kMainSettings[] = {
    ST_TEXT ("Launcher", "AzimuthPath", "Azimuth (azimuth.exe)", "Azimuth", nullptr),
    ST_BOOL ("Input", "Diagnostics", "Diagnostics: write the received pose and wheel state to the log", "Diagnostics", nullptr),
};

struct SettingTable { const Setting* rows; size_t count; };
inline constexpr SettingTable kAllTables[] = {
    {kHeadSettings, sizeof(kHeadSettings) / sizeof(Setting)},
    {kWheelSettings, sizeof(kWheelSettings) / sizeof(Setting)},
    {kSimSettings, sizeof(kSimSettings) / sizeof(Setting)},
    {kDrawDistSettings, sizeof(kDrawDistSettings) / sizeof(Setting)},
    {kMainSettings, sizeof(kMainSettings) / sizeof(Setting)},
};

} // namespace app
