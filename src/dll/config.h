// SPDX-License-Identifier: MIT
// Plugin settings structures (head tracking, wheel, simulation, draw distance).
#pragma once
#include <windows.h>
#include <string>
#include <utility>
#include <vector>
#include "pose.h"
#include "wheel_ffb.h"

namespace fh2 {

enum class Source { Auto, Udp, FreeTrack };

enum class WheelAxis { X, Y, Z, RX, RY, RZ, Slider0, Slider1, None };
enum class WheelUserMode { Auto, None };

struct WheelConfig {
    bool        enabled = true;
    bool        overrideAzimuthWheel = true;   // hide a wheel provided by Azimuth itself, use ours
    std::string deviceMatch = "G29";
    // Separate USB devices for the pedals, the shifter and the handbrake (part of the device name).
    // Empty = read them from the wheel device itself. ThrottleAxis/BrakeAxis/ClutchAxis then refer to the
    // pedals device, GearButtons/ReverseButton to the shifter device, HandbrakeButton/Axis to the handbrake device.
    std::string pedalsMatch, shifterMatch, handbrakeMatch;
    int         wheelRange = 900;
    WheelAxis   steeringAxis = WheelAxis::X, throttleAxis = WheelAxis::Y,
                brakeAxis = WheelAxis::RZ, clutchAxis = WheelAxis::Slider0;
    bool        steeringInvert = false;
    bool        pedalsInverted = true;
    float       pedalDeadzone = 0.02f;
    int         buttonA = 0, buttonB = 2, buttonX = 1, buttonY = 3;
    int         buttonView = 8, buttonMenu = 9, buttonLB = 5, buttonRB = 4;
    bool        hShifter = false;
    int         gearButtons[7] = {12, 13, 14, 15, 16, 17, -1};
    int         reverseButton = 18;
    int         handbrakeButton = -1;
    WheelAxis   handbrakeAxis = WheelAxis::None;
    bool        handbrakeAxisInvert = false;
    float       handbrakeThreshold = 0.3f;
    char        handbrakeGameButton = 'B';   // A, B, X, Y, L (LB) or R (RB)
    WheelUserMode userMode = WheelUserMode::Auto;
    bool        forceRescan = true;
    int         vendorId = 0x046D, productId = 0xC261;
    int         configFlag14 = 1;
    bool        testMode = false;
    bool        ffbEnabled = true;
    float       ffbGain = 1.0f;
    bool        ffbInvert = false;
    FfbTuning   ffbTuning;
    float       ffbLockStrength = 1.0f, ffbLockRampDeg = 3.0f;   // soft lock at the end of the steering lock
};

enum class AssistMode { Game, On, Off };     // Game = keep the player's game setting

struct CarAssistOverride {                   // from FH2SimImmersion_cars.ini
    unsigned id = 0;
    bool     absSet = false, tcSet = false, stmSet = false, clutchSet = false;  // false = auto (use the research data)
    AssistMode abs = AssistMode::Game, tc = AssistMode::Game, stm = AssistMode::Game, clutch = AssistMode::Game;  // clutch: On = has a clutch
    bool     forceAutomatic = false;         // 4th field "automatic": always shift automatically (also sets clutchSet, clutch = Off)
};

struct SimConfig {
    float fovCockpit = 0, fovDriver = 0, fovHood = 0, fovBumper = 0;   // extra degrees, 0 = game default
    bool       realisticAbs = false, realisticTc = false, realisticStm = false, realisticClutch = false, realisticAutomatic = false;
    AssistMode optionalAssists = AssistMode::Game;   // cars where the real car had it as an option
    std::vector<CarAssistOverride> carOverrides;
    // steering lock
    bool realisticSteeringLock = false;     // per-car real lock to lock rotation
    int  maxWheelRotation = 0;              // degrees, 0 = WheelRange
    bool lockScale = false;                 // SteeringLockMode=scale (false = cap)
    bool realCockpitWheel = false;          // the in-game wheel turns like the real one
    bool hideDriverInCockpit = false;
    int  cockpitWheelDirection = 1;         // 1 or -1
    std::vector<std::pair<unsigned, unsigned>> steeringLocks;   // [SteeringLock] car Id -> degrees (from the cars INI)
};

struct DrawDistConfig {                      // metres; 0 = game default
    float shadow = 0, world = 0, props = 0, crowd = 0, grass = 0, traffic = 0, racer = 0;
};

struct Config {
    // [Input]
    Source source = Source::Auto;
    bool   diagnostics = true;
    // [Network]
    int          udpPort = 4242;
    std::string  bindAddress = "127.0.0.1";
    // [General]
    bool enableOnStartup = true;
    bool overrideAzimuthHeadTracking = true;   // false = leave the head-tracking camera to Azimuth
    bool cockpitOnly     = true;     // only DriverCam layers; false = also hood/bumper
    int  staleTimeoutMs  = 500;
    double smoothing     = 0.0;      // 0..1
    // [Hotkeys] Windows virtual key codes
    int toggleKey = VK_END, recenterKey = 0;
    int chordToggleKey = 'Y', chordRecenterKey = 0;     // used with Ctrl+Shift; 0 = none
    unsigned recenterButtons = 0x00C0;                  // XInput mask, default L3+R3
    int wheelRecenterButtons[4] = {4, 5, 0, 0};         // DirectInput buttons held together on the wheel
    int wheelRecenterCount = 2;
    // Pose mapping
    MapCfg map;
    // [Wheel]
    WheelConfig wheel;
    // [Simulation]
    SimConfig sim;
    // [DrawDistance]
    DrawDistConfig drawDist;
};

// Loads the INI, writing a commented default file first if it does not exist.
Config loadConfig(const std::wstring& iniPath);

} // namespace fh2
