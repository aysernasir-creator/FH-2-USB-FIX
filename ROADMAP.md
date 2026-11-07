# FH2 Sim Immersion for Azimuth — Roadmap

## Status: 0.9.6 — Core features complete, in-game verification pending

All major features are implemented and built. This document tracks what remains to complete the project.

## Pending tasks

### 1. In-game verification of features (0.9.2–0.9.6)

Test each feature in Forza Horizon 2 running in Azimuth 0.9.10.

#### FFB improvements (0.9.2)
- [ ] Slip invert default on: counter-steer force in slides
- [ ] Front-tyre self-aligning force (understeer, spins, handbrake turns)
- [ ] Rumble gain: road surface and impact vibration (kerbs, cobblestones, gravel)
- [ ] Real handbrake mapped to game button B

#### Draw distance (0.9.3)
- [ ] Shadow distance slider works (85–1000 m)
- [ ] World objects distance (scenery, buildings, trees)
- [ ] Props distance (fences, signs, bollards that can be knocked over)
- [ ] Crowd distance (spectators)
- [ ] Grass distance (fade in/out)
- [ ] Traffic distance (car detail levels)
- [ ] Racer car detail distance
- [ ] Performance: test with various settings and watch frame rate

#### Per-car realistic clutch (0.9.4)
- [ ] Manual cars without clutch show "manual without clutch" (sequential, dual-clutch)
- [ ] Cars with clutch show "manual with clutch"
- [ ] Cars marked "mixed" follow OptionalAssists setting
- [ ] Azimuth wheel override switch (`OverrideAzimuthWheel=1`) works
- [ ] Azimuth head-tracking override switch (`OverrideAzimuthHeadTracking=1`) works
- [ ] **Note:** The game may only read the shifting mode at race start

#### Automatic-only cars (0.9.5)
- [ ] Traditional automatics (gearbox type `at`) always shift automatically
- [ ] CVT cars (gearbox `cvt`) always shift automatically
- [ ] Single-speed electric cars (gearbox `ev`) always shift automatically
- [ ] Explicit clutch override (`automatic`) forces automatic shifting for that car
- [ ] Clutch override `on`/`off` cancels automatic forcing

#### Steering lock and in-game wheel (0.9.6)
- [ ] Per-car real steering lock: full in-game steering at real car's lock-to-lock rotation
- [ ] MaxWheelRotation cap mode: cars with bigger lock use the cap, others stay 1:1
- [ ] MaxWheelRotation scale mode: all cars scaled proportionally to fit the cap
- [ ] Soft lock FFB: end-stop force builds up past the lock, with damping
- [ ] In-game steering wheel turns like the real one (display lock, not fixed ±90°)
- [ ] Hide driver arms in cockpit camera when RealCockpitWheel is on
- [ ] **Critical:** `CockpitWheelDirection` may need flipping (1 or -1) depending on wheel direction

### 2. Driver arms animation research

Can the driver's arms follow the wheel beyond ±90°?

- [ ] Investigate the arms animation: does it use a game clamp or is it data-driven?
- [ ] Check if the angle/(π/2) formula in the game is a hard limit or patchable
- [ ] If patchable, estimate effort and decide whether to implement for post-launch

### 3. README screenshots

Add screenshots to `docs/screenshots/` to document the settings window.

- [ ] Capture `docs/screenshots/settings-window.png` — Settings window with all tabs visible (or collage of tabs)
- [ ] Capture `docs/screenshots/simulation-tab.png` — Simulation tab showing car list with ABS, TC, STM, Clutch, Gearbox, Lock columns
- [ ] Capture `docs/screenshots/draw-distance-tab.png` — Draw distance tab with sliders and distance visualization
- [ ] Verify links in README point to the correct paths

## Completed

### Version 0.9.0–0.9.1
- [x] Head tracking: FreeTrack/OpenTrack UDP input → game's Kinect head-tracking camera layers
- [x] Steering wheel: virtual Xbox wheel from DirectInput G29, with pedals and H-shifter
- [x] Wheel without an Xbox controller; paddle recenter buttons
- [x] Realistic ABS / TC / stability control per car (354 cars researched)
- [x] Settings window (all 5 tabs: Head tracking, Wheel, Simulation, Draw distance, Main)

### Version 0.9.2
- [x] FFB improvements: slip invert, aligning force, rumble gain, handbrake
- [x] GUI repaint fix for Wine/Xvfb

### Version 0.9.3
- [x] Draw distance settings tab (shadows, world, props, crowd, grass, traffic, racer LOD)

### Version 0.9.4
- [x] Per-car realistic clutch (169 yes / 102 mixed / 78 no / 5 unknown)
- [x] Azimuth wheel and head-tracking override switches

### Version 0.9.5
- [x] Automatic-only cars shift automatically (7 cars: traditional autos + Tesla)
- [x] Gearbox column in car list

### Version 0.9.6
- [x] Per-car steering lock (354 cars, checked by hand)
- [x] Max wheel rotation with cap/scale modes
- [x] Soft lock FFB end-stop with damping
- [x] In-game wheel turns like the real one
- [x] Hide driver arms in cockpit
- [x] Car tracking fix for STM/clutch/automatic features

### License and docs
- [x] MIT license
- [x] SPDX headers on all source files
- [x] Complete README rewrite with all features documented

## Notes

- **Game version:** FH2 1.0.0.64 + Storm Island 1.0.0.36 on Azimuth 0.9.10 (read-only)
- **Build:** CMake + mingw-w64; `./build.sh` on Windows; unit tests all pass
- **Per-car data:** `data/car_assists.csv` (354 cars with ABS, TC, STM, clutch, gearbox, steering lock)
- **Overrides:** `FH2SimImmersion_cars.ini` (`[Cars]` for assists, `[SteeringLock]` for lock degrees)
- **Log:** `FH2SimImmersion.log` next to the DLL shows what the plugin did
- **Important:** Close `FH2SimImmersion.exe` before reporting any Azimuth bugs (Azimuth developer does not support modified setups)

## How to contribute

1. Test the features listed above in-game
2. Report bugs or odd behaviour in `FH2SimImmersion.log`
3. Corrections to car data in `data/car_assists.csv` are welcome
4. Screenshots for the README: save to `docs/screenshots/` with descriptive names

