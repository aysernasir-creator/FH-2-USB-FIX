// SPDX-License-Identifier: MIT
// Facts about Forza Horizon 2 1.0.0.64 (forza_durango_release_final.exe,
// TimeDateStamp 0x565501ED) recovered by static analysis. Every value here is
// verified at runtime before anything is patched; on any mismatch the plugin
// stays dormant and the game runs untouched.
#pragma once
#include <cstdint>

namespace fh2 {

// --- Camera-layer input evaluation -----------------------------------------
// CameraLayer::EvaluateInput (rva 0x1037DF0) switches on layer->InputType via a
// jump table at rva 0x1038558. Cases 28..32 are the head-tracking inputs:
//
//   case HeadTrackingX     (28) rva 0x10382C8: v = KinectHeadPos(mgr).x   -> stubbed, always 0
//   case HeadTrackingY     (29) rva 0x10382E2: v = KinectHeadPos(mgr).y   -> stubbed, always 0
//   case HeadTrackingZ     (30) rva 0x10382FD: v = KinectHeadPos(mgr).z   -> stubbed, always 0
//   case HeadTrackingYaw   (31) rva 0x1038318: v = KinectLean(mgr,0).x * 180/pi
//   case HeadTrackingPitch (32) rva 0x103834C: v = KinectLean(mgr,0).y * 180/pi (always 0)
//
// Inside the case code RSI points at the layer being evaluated and the result
// is stored to [rsi]. We redirect the five calls to our own thunks.

constexpr uint32_t kCaseBlockRva  = 0x10382C8;   // start of case 28
constexpr uint32_t kCaseBlockSize = 0x1038381 - 0x10382C8;
constexpr uint32_t kJumpTableRva  = 0x1038558;

struct CallSite {
    uint32_t caseRva;    // jump-table target for this input type
    uint32_t rva;        // rva of the E8 call instruction
    uint32_t calleeRva;  // expected original target
    int      axis;       // component the case code reads from the returned vector
    bool     rotation;   // true: case multiplies result by 180/pi
    int      inputType;  // camera input enum
};

constexpr uint32_t kHeadPosStubRva = 0x082CF70;  // returns a zero vector (ICF-folded stub)
constexpr uint32_t kKinectLeanRva  = 0x12659B0;  // body-lean angle from Kinect body data

constexpr CallSite kSites[5] = {
    {0x10382C8, 0x10382D4, kHeadPosStubRva, 0, false, 28},
    {0x10382E2, 0x10382EE, kHeadPosStubRva, 1, false, 29},
    {0x10382FD, 0x1038309, kHeadPosStubRva, 2, false, 30},
    {0x1038318, 0x103832A, kKinectLeanRva,  0, true,  31},
    {0x103834C, 0x103835E, kKinectLeanRva,  1, true,  32},
};

// --- Camera layer object (as built from CameraPhysics.xml <Layer .../>) ------
namespace layer {
constexpr int kInputValue   = 0x00;  // float, written by EvaluateInput
constexpr int kInputType    = 0x48;  // int, enum CameraInput
constexpr int kNoiseType    = 0x4C;  // int, 0 = None (multiplier 1.0)
constexpr int kOutputType   = 0x50;  // int, enum CameraOutput
constexpr int kClampMin     = 0x58;  // float InputClampMin
constexpr int kClampMax     = 0x5C;  // float InputClampMax
constexpr int kInputAbs     = 0x60;  // byte, take |input| before mapping
constexpr int kMapping      = 0xAC;  // int, InputToParamMapping (0 One,1 Linear,2 Abs,3 Three,4 Five)
constexpr int kParam        = 0xE0;  // float[4] Param.x/y/z/w (16-byte aligned)
}

enum CameraInput : int {
    HeadTrackingX = 28, HeadTrackingY = 29, HeadTrackingZ = 30,
    HeadTrackingYaw = 31, HeadTrackingPitch = 32,
};

enum CameraOutput : int {
    CarSpaceYPROffset = 0, CarSpaceYPRImpulse = 1,
    CarSpaceXYZOffset = 2, CarSpaceXYZImpulse = 3,
    CameraSpaceYPROffset = 4, CameraSpaceYPRImpulse = 5,
    CameraSpaceXYZOffset = 6, CameraSpaceXYZImpulse = 7,
};

constexpr int kMappingLinear = 1;

// Stock head-tracking layers, recognised by their original Param.x:
//   DriverCam (cockpit): Yaw->CameraSpaceYPROffset Param.x=12, X->CarSpaceXYZOffset Param.x=0.03
//   Hood / BumperHigh  : Yaw->CameraSpaceYPROffset Param.x=15, Bumper X Param.x=0.09
constexpr float kCockpitYawParam = 12.0f;
constexpr float kCockpitXParam   = 0.03f;

} // namespace fh2
