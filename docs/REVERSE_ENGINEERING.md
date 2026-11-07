# Reverse-engineering notes

Target: Forza Horizon 2 **1.0.0.64**, `forza_durango_release_final.exe` (TimeDateStamp `0x565501ED`,
SizeOfImage `0x3A18000`), running inside Azimuth 0.9.x. All addresses below are RVAs.
This repository contains no game code or data. The plugin identifies the code it patches by
hash and by an opcode skeleton.

## Why the Kinect path was not emulated

The game has a Kinect head-tracking feature: profile settings `HeadTracking/*`, camera
inputs `HeadTrackingX/Y/Z/Yaw/Pitch`, and layers in `Camera.zip/CameraPhysics.xml`. But
the shipped data path behind it is almost empty:

* The Kinect manager (global pointer at `0x367A650`) reads `KinectSensor` → `BodyFrameReader`
  → `GetAndRefreshBodyData`.
  * Per body it reads IsTracked, TrackingId, Lean, and only joints **SpineBase (0)** and
    **Head (3)**.
  * Body update: `0x1267BC0`; smoothing: `0x1268120`.
* `0x12659B0` turns that into a single lean angle: atan2(Head − SpineBase), or Lean.X.
  This is what the yaw input receives.
* The X/Y/Z inputs call `0x082CF70`, which returns a zero vector (an identical-code-folded
  stub). Pitch reads the `.y` of the lean result, which is always 0.

So even a perfect fake Kinect would only produce a small, lean-driven yaw.

## Camera layer input evaluation

`CameraLayer::EvaluateInput` is at `0x1037DF0` (rcx = layer, rdx = car, xmm3 = dt).
It dispatches on `layer->InputType` (`+0x48`) through the jump table at `0x1038558` (45 cases).

| Case | Input | Case code | Call site | Callee | Reads |
|---|---|---|---|---|---|
| 28 | HeadTrackingX | `0x10382C8` | `0x10382D4` | `0x082CF70` | `[rax]` |
| 29 | HeadTrackingY | `0x10382E2` | `0x10382EE` | `0x082CF70` | `[rax+4]` |
| 30 | HeadTrackingZ | `0x10382FD` | `0x1038309` | `0x082CF70` | `[rax+8]` |
| 31 | HeadTrackingYaw | `0x1038318` | `0x103832A` | `0x12659B0` | `[rax] * 180 * (1/π)` |
| 32 | HeadTrackingPitch | `0x103834C` | `0x103835E` | `0x12659B0` | `[rax+4] * 180 * (1/π)` |

In every case RSI is the layer, and the result is stored to `[rsi]`.
The layer update loop (`0x1037C7E` → `0x103BA40`) walks every layer (stride `0x110`) with no
head-tracking gate.

## Layer object

| Offset | Field |
|---|---|
| `+0x00` | input value (float) |
| `+0x10` | output vector (vec4) |
| `+0x48` | InputType (enum below) |
| `+0x4C` | NoiseType (0 None → multiplier 1.0) |
| `+0x50` | OutputType |
| `+0x58` / `+0x5C` | InputClampMin / InputClampMax |
| `+0x60` | take \|input\| flag |
| `+0xAC` | curve: mapping (0 ConstantOne, 1 Linear, 2 Abs, 3 ThreePoint, 4 FivePoint), inputs[5] `+0x04`, outputs[5] `+0x18`, mirrored `+0x2C` |
| `+0xE0` | Param vec4 |

The output is `curve(clamp(input)) * Param`, after a transform at `0x103BC20`:

* YPR output types (0, 1, 4, 5): Param is in degrees and is multiplied by π/180.
* XYZ output types pass through, in metres.

Springs are applied after that.

Enums, recovered from the name tables:

* **Inputs:** 25–27 HeadHomeOffset X/Y/Z; 28–32 HeadTracking X/Y/Z/Yaw/Pitch.
* **Outputs:** 0 CarSpaceYPROffset, 2 CarSpaceXYZOffset, 4 CameraSpaceYPROffset,
  6 CameraSpaceXYZOffset.

## Stock head-tracking layers

| Camera | Input | Output | Param |
|---|---|---|---|
| DriverCam (cockpit) | Yaw | CameraSpaceYPROffset | x = 12 |
| DriverCam (cockpit) | X | CarSpaceXYZOffset | x = 0.03 |
| DriverCam (cockpit) | Y | CarSpaceXYZOffset | y = 1 |
| Hood | Yaw | CameraSpaceYPROffset | x = 15 |
| BumperHigh | Yaw | CameraSpaceYPROffset | x = 15 |
| BumperHigh | X | CarSpaceXYZOffset | x = 0.09 |
| FollowCam / FollowCam2 | Yaw | various | disabled |

## What the plugin does

1. Finds the image. Fast path: an allocation base whose case block (`0x10382C8`, 185 bytes)
   hashes to `0x94F0E52A52E2CD1B`. Otherwise it scans executable memory every ~5 s.
2. Verifies jump-table entries 28–32 and the five `E8` call targets. On any mismatch it stays
   dormant.
3. Allocates thunks within ±2 GB of the image:
   `mov r8d, axis; mov r9, rsi; mov rax, hook; jmp rax`.
4. Suspends other threads and rewrites the five rel32 call displacements.
5. In the hook:
   * The cockpit rotation layer (original Param.x = 12) gets Param = (yaw, pitch, roll) in
     degrees.
   * The cockpit position layer (original Param.x = 0.03) gets Param = (x, y, z) in metres.
   * Both get input 1.0, mapping Linear and clamp ±2.
   * Every other head-tracking layer gets input 0.
   * Original Params are fingerprinted per layer and re-checked if the game reloads camera data.

## Azimuth

Azimuth loads the game image into `azimuth.exe` and implements its WinRT/kernelx APIs. It
records modifications (`modifications:` in its log, "The crash happened in modified code").
The developer does not support modified setups.
