#!/usr/bin/env bash
# Cross-builds the plugin and launcher with mingw-w64, runs the unit tests and
# packages dist/FH2SimImmersion-<version>.zip.
# Version: $VERSION if set, otherwise project(VERSION) from CMakeLists.txt.
set -euo pipefail
cd "$(dirname "$0")"
VERSION="${VERSION:-$(sed -nE 's/^project\([^)]*VERSION ([0-9][0-9.]*).*/\1/p' CMakeLists.txt)}"
[ -n "$VERSION" ] || { echo "could not determine version" >&2; exit 1; }
CXX=x86_64-w64-mingw32-g++
FLAGS="-std=c++17 -O2 -Wall -Wextra -Wno-unused-parameter -Wno-cast-function-type -static -static-libgcc -static-libstdc++ -s"
PKG=dist/FH2SimImmersion

rm -rf build dist && mkdir -p build "$PKG"

# src/common/car_db.h is generated from data/car_assists.csv (and committed).
if command -v python3 >/dev/null 2>&1; then
    python3 tools/gen_car_db.py data/car_assists.csv build/car_db.h.new
    if [ -f src/common/car_db.h ] && ! cmp -s build/car_db.h.new src/common/car_db.h; then
        echo "warning: src/common/car_db.h was out of date with data/car_assists.csv; regenerated" >&2
    fi
    cp build/car_db.h.new src/common/car_db.h
elif [ ! -f src/common/car_db.h ]; then
    echo "error: src/common/car_db.h is missing and python3 is not available to generate it (tools/gen_car_db.py)" >&2
    exit 1
fi
bash tests/run_tests.sh

$CXX $FLAGS -DFH2SI_VERSION="\"$VERSION\"" -shared -o build/FH2SimImmersion.dll \
    src/dll/dllmain.cpp src/dll/config.cpp src/dll/tracker.cpp src/dll/hook.cpp \
    src/dll/patch_util.cpp src/dll/wheel_device.cpp src/dll/wheel_com.cpp src/dll/wheel_hook.cpp src/dll/wheel_ffb.cpp src/dll/fov_hook.cpp src/dll/assist_hook.cpp src/dll/drawdist_hook.cpp src/dll/cockpit_hook.cpp \
    -lws2_32 -ldinput8 -ldxguid -lole32 -Wl,--subsystem,windows
# settings window + launcher (plain Win32, comctl32 v6 via the manifest in app.rc)
x86_64-w64-mingw32-windres -I src/app -O coff src/app/app.rc -o build/app_res.o
$CXX $FLAGS -DFH2SI_VERSION="\"$VERSION\"" -mwindows -municode -o build/FH2SimImmersion.exe \
    src/app/main.cpp src/app/page.cpp src/app/draw.cpp src/app/ini.cpp src/app/injector.cpp \
    src/app/pages_head.cpp src/app/pages_wheel.cpp src/app/pages_sim.cpp src/app/pages_draw.cpp src/app/pages_main.cpp build/app_res.o \
    -lcomctl32 -lpsapi -lshlwapi -lole32 -lshell32 -luuid -lgdi32 -ldinput8 -ldxguid
# integration harness (needs your own copy of the game executable to run)
$CXX $FLAGS -municode -mavx -o build/harness.exe tests/harness.cpp -lws2_32

cp build/FH2SimImmersion.dll build/FH2SimImmersion.exe "$PKG"/
cp README.md "$PKG"/README.md
cp LICENSE "$PKG"/LICENSE.txt
(cd dist && zip -qr "FH2SimImmersion-$VERSION.zip" FH2SimImmersion)
echo "built dist/FH2SimImmersion-$VERSION.zip"
