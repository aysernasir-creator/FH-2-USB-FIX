#!/usr/bin/env bash
# Host-side unit tests (also run by build.sh and .github/workflows/test.yml).
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p build
g++ -std=c++17 -O1 -Wall tests/test_pose.cpp -o build/test_pose && ./build/test_pose
g++ -std=c++17 -O1 -Wall tests/test_ffb.cpp src/dll/wheel_ffb.cpp -o build/test_ffb && ./build/test_ffb
g++ -std=c++17 -O1 -Wall tests/test_ini.cpp -o build/test_ini && ./build/test_ini
g++ -std=c++17 -O1 -Wall tests/test_lock.cpp src/dll/wheel_ffb.cpp -o build/test_lock && ./build/test_lock
g++ -std=c++17 -O1 -Wall tests/test_cars.cpp -o build/test_cars && ./build/test_cars
if command -v python3 >/dev/null 2>&1; then
    python3 tools/gen_car_db.py data/car_assists.csv build/car_db.h.new
    diff -q --strip-trailing-cr build/car_db.h.new src/common/car_db.h || { echo "src/common/car_db.h is out of date: run tools/gen_car_db.py" >&2; exit 1; }
    printf 'Id,MediaName,Year,CarName,ABS,TC,STM,Clutch,Gearbox,SteeringLockDeg,Notes\n1,M,2000,C,no,no,no,no,manual,900,\n' > build/bad_car.csv
    if python3 tools/gen_car_db.py build/bad_car.csv build/bad_car.h 2>/dev/null; then echo "gen_car_db accepted a clutch that does not fit the gearbox" >&2; exit 1; fi
    echo "car data tests passed"
fi
