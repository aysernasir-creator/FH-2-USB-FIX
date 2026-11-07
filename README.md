# FH2 Sim Immersion for Azimuth

**Status: 0.9.6.** Head tracking tested with Beam Eye Tracker (FreeTrack input).
Steering wheel support, draw distance, per-car realism assists, steering lock, and in-game cockpit wheel are experimental features.

Realism and immersion add-ons for Forza Horizon 2 running in Azimuth:

* **Webcam head tracking**: see [Head tracking](#head-tracking) below.
* **Steering wheel support (experimental)**: Logitech G29 and similar wheels via DirectInput. See [Steering wheel (experimental)](#steering-wheel-experimental).
* **Per-car realism assists (experimental)**: ABS, traction control, stability control, and clutch based on the real car's equipment. See [Realistic assists](#realistic-assists-experimental).
* **Steering lock and in-game wheel (experimental)**: real car's steering lock, max wheel rotation capping or scaling, soft lock force feedback, and an in-game wheel that follows the real one. See [Steering lock and the in-game wheel](#steering-lock-and-the-in-game-wheel).
* **Draw distance**: raise how far the game draws scenery, traffic, shadows and props. See [Draw distance](#draw-distance).

**Unofficial.** Not affiliated with Azimuth / Almost There Games, Microsoft or Playground Games. The Azimuth developer does not support modified setups: **close FH2SimImmersion.exe before reporting any Azimuth bug.**

**Supported:** Forza Horizon 2 **1.0.0.64** (with or without Storm Island) on Azimuth 0.9.x.
On any other game build the plugin detects the difference, changes nothing, and logs why.

## How it works

`FH2SimImmersion.exe` is a small launcher. While it runs, it loads `FH2SimImmersion.dll` into any `azimuth.exe` that starts. The plugin waits until the game image is mapped, checks that it is the known build (by hash and by an opcode skeleton), and only then patches it in memory. Nothing in the Azimuth or game folders is edited, and this repository contains no game code or data.

* **Head tracking.** The game evaluates a head-tracking input for each camera layer every frame (`HeadTrackingX/Y/Z/Yaw/Pitch` in CameraPhysics.xml). In this build the Kinect side of those inputs is almost empty: X, Y and Z come from a function that always returns zero, pitch is always zero, and yaw is only body lean, capped at about 12 degrees. The plugin redirects those five input reads and, for the cockpit layers, supplies your head pose from FreeTrack shared memory (Beam Eye Tracker, OpenTrack) or OpenTrack UDP. Rotation goes through the cockpit `HeadTrackingYaw -> CameraSpaceYPROffset` layer and position through the cockpit `HeadTrackingX -> CarSpaceXYZOffset` layer. The game applies them with its own layer maths and spring smoothing; the layer ranges are widened in memory (Camera.zip is not modified).
* **Steering wheel.** The game supports the Logitech G920 on Xbox, but Azimuth only passes gamepads to it. The plugin adds a virtual Xbox wheel to the game's controller list and feeds it from your DirectInput wheel. The game then uses its own wheel controls and steering. The game's force feedback programs (steering torque, centring spring, damping, rumble) are emulated and played on the real wheel as one DirectInput force.
* **Assists.** The plugin replaces the getters the game uses to read the ABS, traction control, stability control and shifting-mode choices for the car you drive. Each car gets what its real counterpart had, from `data/car_assists.csv`, or a per-car override. Your menu choices stay untouched for everything the plugin does not decide.
* **Steering lock.** The game steers every car through one fixed rotation. With the real lock-to-lock rotation of each car (`data/car_assists.csv`), the plugin changes how many degrees of your wheel make full in-game steering, caps or scales it to what your wheel can turn, builds a soft end stop with force feedback, and can make the in-game steering wheel (and the cockpit model) turn as far as the real one.

## Install

1. **Set up a head tracker** (optional, but head tracking will not work without one). Either of these works; when both are running, FreeTrack is used:
   * **Beam Eye Tracker:** Game Connection -> **Head Tracking Device Emulation**, with Driver set to `Compatibility`. Beam then publishes the FreeTrack/TrackIR shared memory, and the plugin reads it directly.
   * **[OpenTrack](https://github.com/opentrack/opentrack/releases)** with a webcam:
     * **Input:** `neuralnet tracker`.
     * **Output:** `freetrack 2.0 Enhanced`, or `UDP over network` (`127.0.0.1`, port `4242`).
     * **Filter:** `Accela`.
     * Bind a key to **Center**.

2. **Plug in the wheel** (if using one) before starting the game. Set the rotation in Logitech G HUB (900 degrees recommended) and disable any G HUB game profile that remaps the wheel.

3. **Unzip this folder** anywhere **outside** the Azimuth folder, e.g. `C:\Games\FH2SimImmersion`. It contains `FH2SimImmersion.exe`, `FH2SimImmersion.dll`, this README and the license. `FH2SimImmersion.ini` (and `FH2SimImmersion.log`) are created next to them.

4. **Start FH2SimImmersion.exe.** It opens the settings window and, while the window is open, loads the plugin into any `azimuth.exe` that starts. Use either:
   * **A:** Start Azimuth as usual (or press **Start Azimuth** on the **Main** tab after choosing `azimuth.exe` with **Browse...**). Leave the window open while you play.
   * **B:** Drag `azimuth.exe` onto `FH2SimImmersion.exe`, or use a shortcut: `FH2SimImmersion.exe "E:\XOne\Forza Horizon 2 Azimuth 0.9.10\azimuth-windows-0.9.10\azimuth.exe"`. It starts Azimuth with the plugin; the window stays open after Azimuth exits.
     Starting `FH2SimImmersion.exe` a second time brings the open window to the front.

5. **Drive in the cockpit camera** to use head tracking. (Steering wheel works in any camera.)

Windows Defender may flag FH2SimImmersion.exe because it loads a DLL into another process (what injectors do). If it does, allow it, or use the ASI loader option below.

### Alternative without the injector

Copy Ultimate ASI Loader's `dinput8.dll` next to `azimuth.exe`, renamed to `winmm.dll`, and copy `FH2SimImmersion.dll` there renamed to `FH2SimImmersion.asi`. This does put files in the Azimuth folder.

### Uninstall

Close FH2SimImmersion.exe and delete its folder. Nothing else was changed. Upgrading from FH2HeadTracking: delete the old FH2HeadTracking.exe/.dll; your FH2HeadTracking.ini is renamed automatically.

## Settings window

`FH2SimImmersion.exe` is the settings window. Change values, then press **Apply**. They are saved in `FH2SimImmersion.ini` next to it and take effect the next time the game starts (the plugin reads the file when it loads). **Revert** reloads the saved values of the current tab, and **Defaults (this tab)** puts the default values in the current tab's controls (press Apply to keep them). Hover a setting for its explanation.

![Settings window - TODO: add screenshot](docs/screenshots/settings-window.png)
* **Head tracking:** tracker input and network, smoothing, every rotation and position gain, invert and limit, the hotkeys and the recenter buttons. The picture shows a head from above, the side and the front with the axis you are editing highlighted.
* **Wheel (experimental):** device, axes, buttons, H-shifter and all force feedback settings. **Test input** reads the wheel without the game and shows its axes and button numbers.
* **Simulation:** field of view per camera, realistic assists, the steering wheel group (real steering lock, maximum wheel rotation, in-game wheel, hiding the driver) and a list of all known cars with what the real car had (ABS, TC, STM, clutch, gearbox, lock). Select a car and choose how its ABS / traction control is forced (auto, on or off), including whether it has a clutch or always shifts automatically, and optionally a steering lock override.
* **Draw distance:** a slider per distance with a picture of the distances to scale.
* **Main:** the path of `azimuth.exe`, Start Azimuth, the diagnostics switch, and a status box with what the launcher did and the last lines of `FH2SimImmersion.log`.

The INI file still works when edited by hand, and everything the window does is also described below.

## Head tracking

The plugin reads your head pose from one of two sources (`Source=auto` picks FreeTrack when its shared memory exists, otherwise UDP):

* **FreeTrack 2.0 shared memory**, written by Beam Eye Tracker (Head Tracking Device Emulation) or by OpenTrack's `freetrack 2.0 Enhanced` output.
* **OpenTrack UDP** on `127.0.0.1`, port `4242` (`UdpPort`).

The pose has yaw, pitch and roll (rotation) plus lean left/right (X), up/down (Y) and forward/back (Z). The plugin centres and smooths it on its own thread and hands it to the game's own head-tracking camera layers, the ones built for Kinect. Rotation is applied through the cockpit `CameraSpaceYPROffset` layer and position through the `CarSpaceXYZOffset` layer, so you get the game's own camera physics. Head tracking acts in the **cockpit camera**.

Set up your tracker as described under [Install](#install), look straight ahead and recenter (see [Controls](#controls)). Then adjust, if needed (see [Tuning](#tuning)):

* **Direction:** the `...Invert` keys per axis, and `RotOrder` / `PosOrder` to swap axes.
* **Strength:** the curves in your tracker, or `YawGain`, `PitchGain` and so on.
* **Range:** `Limit` per axis; the defaults keep the view inside the cabin.
* **Left/right balance:** `YawLeftGain` / `YawRightGain`.
* **Smoothing:** `Smoothing` (for example `0.2` against jitter).

## Controls

| Action | Default |
|---|---|
| Recenter | **L3 + R3** on an Xbox controller (both sticks clicked together) |
| Toggle head tracking | `End`, or `Ctrl+Shift+Y` |

Both clicks also reach the game, as usual. You can also recenter in your tracker (Beam / OpenTrack). All of these are remappable in `FH2SimImmersion.ini`:

* `RecenterButtons`, e.g. `BACK+START` (also on the Head tracking tab).
* `WheelRecenterButtons`: steering wheel buttons held together to recenter (default `4+5`, both G29 shift paddles).
* `RecenterKey` / `ChordRecenterKey` to add a keyboard key. The file is created next to the DLL on the first run. Edit it, then restart the game.

## Steering wheel (experimental)

The game supports the Logitech G920 on Xbox, but Azimuth only passes gamepads to it. The plugin adds a virtual Xbox wheel to the game's controller list and feeds it from your DirectInput wheel. The game then uses its own wheel controls and steering. Tested with a Logitech G29, with pedals and the Driving Force Shifter.

### What works

* Steering, throttle, brake and clutch.
* A/B/X/Y, View, Menu, D-pad and the shift paddles.
* The H-shifter (set the game's transmission to **Manual with clutch**).
* Force feedback: the game's steering torque, centring spring, damping and rumble, played as one DirectInput force.

### Not supported

* Buttons the G920 does not have (G29 L2/R2, L3/R3, +/-, dial, Enter, PS) do nothing.

### Setup

1. Plug in the wheel before starting the game. Set the rotation in Logitech G HUB (900 degrees recommended) and disable any G HUB game profile that remaps the wheel.
2. Start FH2SimImmersion.exe and the game as usual. The wheel is on by default; set `Enabled=0` under `[Wheel]` in `FH2SimImmersion.ini` to turn it off.
3. The log shows `wheel: using "..."` and `wheel: virtual wheel added to the controller list`.

### Configuration

`[Wheel]` settings in `FH2SimImmersion.ini`:

* `DeviceMatch`: part of the device name (default `G29`). Leave it empty to use the first wheel found.
* `WheelRange`: the rotation set in G HUB, in degrees.
* `SteeringAxis`, `ThrottleAxis`, `BrakeAxis`, `ClutchAxis`, `PedalsInverted`, `PedalDeadzone`: axis mapping. The defaults fit the G29.
* `ButtonA` ... `ButtonRB`: DirectInput button numbers (0-based) for the game's buttons.
* `HShifter=1` with `GearButtons` / `ReverseButton` for the shifter.
* `UserMode=none` if the game ignores the wheel or acts oddly in menus.
* `FFB=0` turns force feedback off. `FFBGain` sets its strength (1.0 = the game's maximum force is the wheel's maximum). `FFBInvert=1` reverses it: use this if the wheel pushes away from the centre in menus instead of returning to it.

### Force feedback tuning

* `FFBTorqueGain`: the game's steering torque (1.0 = game default).
* `FFBSpringGain`: the centring spring.
* `FFBSlipSpringFloor`: share of the centring spring kept when the rear slides.
* `FFBSlipGain` (0 = off), `FFBSlipStart` / `FFBSlipFull` and `FFBSlipInvert`: the counter-steer force in a slide and its rear slip range. `FFBSlipInvert=1` is the default: the force pushes the wheel toward the direction of travel, which is correct for the G29. If it turns into the slide instead, set `FFBSlipInvert=0`. If the wheel feels too light off-road, raise `FFBTorqueGain` or `FFBSlipSpringFloor`.
* `FFBAlignGain` (0.6, 0 = off), `FFBAlignStart` / `FFBAlignFull` (0.05 / 0.5): a self-aligning force that turns the wheel toward the direction of travel while the front tyres slide (understeer, spins, handbrake turns). It follows `FFBSlipInvert`.
* `FFBRumbleGain` (1.5, 0 - 5): scales the road-surface and impact vibration (kerbs, cobblestones, gravel). 1.0 = game default.
* `FFBLockStrength` / `FFBLockRampDeg`: the soft lock, see [Steering lock](#steering-lock-and-the-in-game-wheel).
* **Handbrake:** the game reads the handbrake only as a button (its wheel layouts use **B**, layout 4 uses **A**). Set `HandbrakeButton` (a DirectInput button number, -1 = none) and/or `HandbrakeAxis` (an axis name, NONE = none; `HandbrakeAxisInvert=1` if it reads maximum when released, `HandbrakeThreshold` = travel from 0.05 to 0.95 that counts as pulled). While the handbrake is pulled the plugin presses the game button named by `HandbrakeGameButton` (A, B, X, Y, LB or RB; default B). The settings window has a Handbrake group and a lever in the wheel picture.

To find axis and button numbers, set `Diagnostics=1` (under `[Input]`). Every 2 seconds the log shows a `wheel diag raw:` line with every axis value and the pressed buttons.

## Field of view

The `[Simulation]` section widens (or narrows) the field of view of the cockpit, driver, hood and bumper cameras: `FovCockpit`, `FovDriver`, `FovHood` and `FovBumper`. Each value is the number of degrees added to the game's own field of view; `0` leaves the game default alone. For example `FovCockpit=10`. The log shows the game's value once per camera as `fov: ... base`.

## Draw distance

The `[DrawDistance]` section raises how far the game draws things. All values are **metres**. `0` (the default) leaves the game's own value alone, and a value at or below the game's own changes nothing: the plugin only ever raises a distance (`new = max(game value, setting)`).

Nothing in the game files is edited; the values are changed in memory while the game loads each track. In the settings window (**Draw distance** tab) every setting is a slider that starts at the game's own value (far left = game default).

![Draw distance tab - TODO: add screenshot](docs/screenshots/draw-distance-tab.png)

| Key | What it controls | Game value | Range |
|---|---|---|---|
| `ShadowDistance` | how far shadows are drawn | 85 m | up to 1000 |
| `WorldDistance` | scenery: terrain, buildings, trees, everything not listed below | 360 m (200 m on some tracks) | up to 3000 |
| `PropsDistance` | props that can be knocked over: fences, signs, bollards | 450 m | up to 3000 |
| `CrowdDistance` | spectators (their model and fade distances follow) | 250 m | up to 1000 |
| `GrassDistance` | grass (the start of its fade follows) | 100 m | up to 500 |
| `TrafficDistance` | traffic cars: fully visible up to 50 m less, faded out at this distance; above 150 m the medium detail level of traffic also reaches this far | 150 m | up to 500 |
| `RacerDistance` | racer cars: distance of the farthest car detail level; the nearer levels (15 / 25 / 50 m in the game) scale with it | 75 m | up to 1000 |

The game has **no separate terrain and city distances**: all scenery is culled by one distance (`WorldDistance`), and only props that can be knocked over have their own (`PropsDistance`).

Higher values cost performance, most of all `WorldDistance`, `ShadowDistance` and `PropsDistance`; raise them one at a time and watch the frame rate. Settings are read when the game starts. The log shows what was changed (`drawdist: ...` lines): the track values each time a track's settings load, `drawdist: traffic fade ...` and `drawdist: car LOD distances ...`. If the game build differs from the one the offsets were taken from, the log says `mismatch; unchanged` and that part does nothing.

## Realistic assists (experimental)

Forza lets you switch ABS, traction control and stability control on or off for every car. With this option each car gets what its real counterpart had: a 1960s Ferrari drives without any of them, a modern supercar with all three. The assists are independent: each has its own switch and its own research data. The plugin overrides the game's assist settings for the car you are driving, so your menu choices stay untouched for everything it does not decide.

### Configuration

The `[Simulation]` section has the keys:

* `RealisticABS=1`, `RealisticTC=1` and `RealisticSTM=1` turn the feature on for ABS, traction control and stability control, each on its own (default `0`: the game setting is used for every car).
* `OptionalAssists=game|on|off` is what to do for cars where the real car had the aid only as an option: `game` keeps your game setting (default), `on` or `off` forces it. It applies to all three assists and the clutch.
* `RealisticClutch=1` (default `0`) gives cars that had a clutch the game's "manual with clutch" shifting and cars without one (sequential or dual-clutch gearboxes) "manual without clutch". It only changes the choice of a player who drives manual: if you drive with automatic gears in the game, the car stays automatic. Cars where the clutch was optional follow `OptionalAssists`.
* `RealisticAutomatic=1` (default `0`) makes automatic-only cars always shift automatically, whatever you chose in the game: cars whose real gearbox was a traditional automatic without a manual mode (`at`), a CVT (`cvt`) or a single-speed electric drive (`ev`). The Gearbox column of the car list in the Simulation tab shows the real gearbox of each car. It works on its own or together with `RealisticClutch`.

### Per-car overrides

Per-car overrides go in an optional file `FH2SimImmersion_cars.ini` next to the DLL:

```
[Cars]
; car Id = ABS,TC,STM,CLUTCH    each on, off or auto (auto = use the research data)
; for CLUTCH, on = the car has a clutch, off = it has none, automatic = always shift automatically
1105=on,auto,off,on
```

Old lines with two or three fields (`Id=ABS,TC`, `Id=ABS,TC,STM`) still work; the missing ones are `auto`. A clutch override of `on` or `off` cancels `RealisticAutomatic` for that car.

The data comes from research of each real car, checked by hand, and is in `data/car_assists.csv` (Id, name, year, ABS, TC, STM, clutch, gearbox, steering lock and notes for every car). It can contain mistakes: corrections are welcome. Cars marked unknown, and cars that are not in the list, keep your game setting.

To check that it works, look in `FH2SimImmersion.log` for `car: <id> <name> (ABS ..., TC ..., STM ..., clutch ..., gearbox ...)` when a race starts, and `assists: ABS forced ... for <name>` (likewise `TC`, `stability control`, `automatic shifting` and `clutch`). With `Diagnostics=1` the log also shows `assists diag:` lines with how often the game asked for the settings.

![Simulation tab with car list - TODO: add screenshot](docs/screenshots/simulation-tab.png)

## Steering lock and the in-game wheel

Every real car turns its steering wheel a different amount from lock to lock: about 540 degrees for a race car, 900 to 1080 for most road cars, 1500 or more for a truck or an old car. The game steers every car through one fixed rotation. These options let your wheel turn like the real one.

**Important:** Set the rotation in Logitech G HUB to the same value as `WheelRange` in the Wheel tab (900 for the G29), or the degrees shown below will not be right.

### Per-car lock

`RealisticSteeringLock=1` (`[Simulation]`, default `0`) makes full in-game steering happen at the real car's lock-to-lock rotation instead of the game's own rotation setting. The data is in `data/car_assists.csv` (column `SteeringLockDeg`) and in the **Lock** column of the car list in the Simulation tab. The data can contain mistakes. To correct a car, select it in the car list and type the degrees in **Lock override**, or edit `FH2SimImmersion_cars.ini` by hand:

```
[SteeringLock]
; car Id = lock to lock in degrees (90..3600)
1105=720
```

Unknown cars use the game's own rotation. In the log, `steering lock: car <id> <name> real <deg> deg -> wheel <deg> deg (display <deg>, <source>)` shows what was chosen for the car you drive.

### Maximum wheel rotation

`MaxWheelRotation` (degrees, `0` = `WheelRange`) is the most your wheel may turn from lock to lock (the G29: 900). A car with a bigger real lock cannot be turned that far, so `SteeringLockMode` decides what happens:

* `cap` (default): cars whose lock is more than `MaxWheelRotation` use `MaxWheelRotation`; the others stay 1:1. With 900: a 1080 degree truck uses 900, a 540 degree race car stays 540.
* `scale`: every car is scaled so the car with the largest lock in the list (1908 degrees) uses `MaxWheelRotation`, and all cars keep their proportions. With 900: 1908 becomes 900, 540 becomes 255.

With `RealisticSteeringLock=0` only the maximum applies to the game's own rotation. With both options off the steering is the same as in 0.9.5.

### Soft lock

When the real wheel can turn further than the car's lock, force feedback builds up an end stop at the lock (`[Wheel]`):

* `FFBLockStrength` (default `1.0`, `0` = off, `1` = the wheel's full force).
* `FFBLockRampDeg` (default `3`) the degrees past the lock over which it builds up.

It also damps the wheel inside the end stop and works in menus too. It does not apply when the lock is as big as the wheel's own range.

### In-game wheel

`RealCockpitWheel=1` makes the steering wheel of the cockpit model turn as far as the real one (the display lock of the car, up to its real lock) instead of at most 90 degrees each way. The driver's arms are only animated for 90 degrees each way, so use `HideDriverInCockpit=1` with it: the driver (arms and hands) is hidden while the cockpit camera is active and shown again in the other cameras. `CockpitWheelDirection` (`1` or `-1`, default `1`) reverses the in-game wheel if it turns the wrong way.

These need the wheel and the plugin's car tracking; the log shows `cockpit: in-game wheel follows the real wheel`, `cockpit: driver hidden` and `cockpit: driver shown`.

All of these are in the Simulation tab, group Steering wheel; the soft lock is on the Wheel tab.

## If Azimuth adds wheel or head-tracking support

Azimuth 0.9.10 has neither. If a later version of Azimuth provides its own steering wheel, the plugin hides it and uses its own wheel with force feedback (`OverrideAzimuthWheel=1` in `[Wheel]`, the default); set it to `0` to use Azimuth's wheel and turn this plugin's wheel off. In the same way `OverrideAzimuthHeadTracking=1` in `[General]` (default) keeps this plugin in charge of the game's head-tracking camera; with `0` the plugin leaves that camera to Azimuth and its own head tracking is off. Both switches are also in the settings window (Wheel tab, group Advanced; Head tracking tab).

## Checking that it works

`FH2SimImmersion.log` is written next to the DLL. A good run looks like this:

```
FH2 Sim Immersion 0.9.6 loaded into pid ... (...\azimuth.exe)
waiting for the Forza Horizon 2 image to be mapped...
game image found at 0000...
patch: 5 head-tracking inputs redirected (thunks at ...)
freetrack: shared memory found
input: using freetrack
layer ... input 31 output 4 Param.x 12.000 -> rotation (cockpit)
layer ... input 28 output 2 Param.x 0.030 -> position (cockpit)
```

The `layer ...` lines only appear once you are driving in cockpit view.

## Building from source

```
src/dll/        plugin loaded into azimuth.exe (hook, tracker, config, wheel, assists, draw distance)
src/common/     shared headers: default settings text, car database (generated from data/car_assists.csv)
src/app/        FH2SimImmersion.exe: settings window and launcher (Win32, GDI pictures, app.rc/app.manifest)
tests/          unit tests, integration harness, fake azimuth.exe
tools/          gen_car_db.py (generates src/common/car_db.h)
docs/           reverse-engineering notes
```

Clone the repository, then use either build:

* **Windows (Visual Studio 2022 or MSYS2 MinGW64):** CMake.
  ```
  cmake -S . -B build -A x64
  cmake --build build --config Release
  ```
* **Cross-build with `./build.sh`** on Linux (or WSL / MSYS2): needs `x86_64-w64-mingw32-g++` and `windres` (mingw-w64), a native `g++`, `python3` and `zip`.

### Development requirements

* CMake, or mingw-w64 (via MSYS2 on Windows) for the Windows binaries.
* Native g++ and python3 for the unit tests and the car database generator.
* Wine, only to run the integration harness on Linux.

### Build

```bash
./build.sh
```

It regenerates `src/common/car_db.h` from `data/car_assists.csv`, builds and runs the unit tests (pose, force feedback, settings parser, steering lock), builds the DLL, the app and the harness, and packages the result:

```
dist/FH2SimImmersion-0.9.6.zip
```

The zip contains `FH2SimImmersion.dll`, `FH2SimImmersion.exe`, this README and `LICENSE.txt`.

### Integration test (harness)

`harness.exe` needs your own copy of the game executable; none is included. It maps the real game executable into memory the way a loader would, loads the DLL (which must find, verify and patch the image), sends OpenTrack packets and runs the game's own `CameraLayer::EvaluateInput` against fake layers. Add `--deep` to exercise the fallback memory scan. It runs on Windows or Wine; the result is printed to the console, and the DLL's own log (`FH2SimImmersion.log`) shows the patch and layer lines:

```
harness.exe "<game>\Mount\forza_durango_release_final.exe" build\FH2SimImmersion.dll [--deep]
```

How the game was analysed, and every address and offset used, is documented in [docs/REVERSE_ENGINEERING.md](docs/REVERSE_ENGINEERING.md) and `src/dll/game_layout.h`. Code is matched by hash and by an opcode skeleton.

## Tuning

### Head tracking

The defaults have been tested with Beam. Use these settings if your tracker behaves differently:

* **Direction:** if an axis moves the wrong way, set its `Invert` to `1`: `YawInvert`, `PitchInvert`, `RollInvert`, `XInvert`, `YInvert`, `ZInvert`. To swap axes, use `RotOrder=` / `PosOrder=`.
* **Strength:** set the curves in your tracker (Beam's sensitivity, or OpenTrack's Mapping window), or use `YawGain`, `PitchGain` and so on here.
* **Left/right balance:** `YawLeftGain` and `YawRightGain` add strength to one side only (for example `YawLeftGain=1.3` if looking left feels shorter than looking right). If left and right are swapped, set `YawLeftIsPositive=0`.
* **Range:** `Limit` caps the camera movement. The defaults keep the view inside the cabin. `Limit=0` turns an axis off.

## Troubleshooting

* **No log file:** the DLL was not loaded. Make sure FH2SimImmersion.exe was running before or while Azimuth started, and that antivirus (Windows Defender) did not block it.
* **"this game build is not recognised":** you have a different FH2 version than 1.0.0.64. Nothing was changed.
* **"bind ... failed":** another program is already using UDP port 4242 (e.g. a second tracker). Change `UdpPort` here and in OpenTrack.
* **No head tracking:** check the tracker is running with the output described under Install, that the log shows `freetrack: shared memory found` or UDP packets, and that you are in the cockpit camera. Recenter, and check the toggle (`End`) did not switch it off.
* **Patched, but no `layer` lines in cockpit view:** the game is not evaluating its head-tracking layers in your setup. Please report it, with the log.
* **The view moves much more than your head, or keeps turning after you face forward (Beam):** Beam's "Immersive Camera" already amplifies head rotation and adds eye-gaze extension, so the view keeps panning while your eyes rest near the edge of the screen. Lower the camera sensitivity or turn off eye-based extension in Beam, or reduce `YawGain`/`PitchGain` here. Set `Diagnostics=1` and the log shows what arrives every 2 seconds (`diag freetrack: in yaw ...`). If `in yaw` keeps growing while your head is still, the extra movement comes from the tracker.
* **Jitter:** improve the lighting on your face, use OpenTrack's Accela filter, or set `Smoothing=0.2`.
* **Recenter does not work:** with no Xbox controller, use the wheel recenter buttons (`WheelRecenterButtons`, default both G29 paddles), the keyboard key, or recenter in your tracker.
* **Wheel not found / no `wheel:` lines in the log:** plug it in before starting the game, check `Enabled=1` and `DeviceMatch` (empty uses the first wheel found), and close other programs that grab the wheel exclusively.
* **Wheel is ignored or acts oddly in menus:** try `UserMode=none`. If it pushes away from the centre in menus, set `FFBInvert=1`.
* **Wrong or reversed pedals/axes:** set `Diagnostics=1`, read the `wheel diag raw:` lines (or use **Test input**) and fix the axis and `PedalsInverted` settings.
* **Steering degrees feel wrong:** `WheelRange` must equal the rotation set in G HUB.
* **Part of the plugin does nothing and the log says `mismatch; unchanged`:** the game build differs from the one the offsets were taken from.
* **Azimuth bug:** close FH2SimImmersion.exe and reproduce it before reporting it to the Azimuth developer.

## License

MIT License. See [LICENSE](LICENSE) for details.

Forza Horizon 2 is a trademark of Microsoft. This project is unofficial and not affiliated with Microsoft, Playground Games, Turn 10 or Almost There Games (Azimuth).

## Contributing

Corrections to the car data (`data/car_assists.csv`) are welcome. Please open an issue or submit a pull request.
