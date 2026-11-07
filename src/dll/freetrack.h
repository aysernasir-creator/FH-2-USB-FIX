// SPDX-License-Identifier: MIT
// FreeTrack 2.0 shared-memory layout, as written by OpenTrack's "freetrack 2.0
// Enhanced" protocol and by TrackIR/FreeTrack emulators such as Beam Eye Tracker.
// Layout follows OpenTrack's freetrackclient/fttypes.h.
#pragma once
#include <cstdint>
#include "pose.h"

namespace fh2 {

constexpr wchar_t kFreeTrackHeap[] = L"FT_SharedMem";

struct FTData {
    uint32_t DataID;              // incremented on every update
    int32_t  CamWidth, CamHeight;
    float    Yaw, Pitch, Roll;    // radians; yaw positive left, pitch positive up, roll positive left
    float    X, Y, Z;             // millimetres
    float    RawYaw, RawPitch, RawRoll, RawX, RawY, RawZ;
    float    X1, Y1, X2, Y2, X3, Y3, X4, Y4;
};

struct FTHeap {
    FTData  data;
    int32_t GameID;
    int32_t table_ints[2];
    int32_t GameID2;
};

static_assert(sizeof(FTData) == 4 * 23, "FTData layout");

// Converts to OpenTrack's UDP conventions (degrees, centimetres). OpenTrack writes
// yaw = -yaw, pitch = -pitch, roll = +roll (radians) and translation * 10 (cm -> mm),
// so this is the exact inverse: both input paths then share one mapping.
inline RawPose freetrackToOpentrack(const FTData& d) {
    constexpr double r2d = 57.29577951308232;
    RawPose p;
    p.yaw   = -d.Yaw * r2d;
    p.pitch = -d.Pitch * r2d;
    p.roll  =  d.Roll * r2d;
    p.x = d.X / 10.0;
    p.y = d.Y / 10.0;
    p.z = d.Z / 10.0;
    return p;
}

} // namespace fh2
