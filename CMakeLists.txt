cmake_minimum_required(VERSION 3.16)
project(fh2_azimuth_sim_immersion VERSION 0.9.6 LANGUAGES CXX RC)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
  message(FATAL_ERROR "Build for x64: Azimuth is a 64-bit process.")
endif()

add_compile_options(-Wall -Wextra -Wno-unused-parameter -Wno-cast-function-type)
add_link_options(-static -static-libgcc -static-libstdc++)

# The plugin loaded into azimuth.exe
add_library(FH2SimImmersion SHARED
  src/dll/dllmain.cpp
  src/dll/config.cpp
  src/dll/tracker.cpp
  src/dll/hook.cpp
  src/dll/patch_util.cpp
  src/dll/wheel_device.cpp
  src/dll/wheel_com.cpp
  src/dll/wheel_hook.cpp
  src/dll/fov_hook.cpp
  src/dll/assist_hook.cpp
  src/dll/drawdist_hook.cpp
  src/dll/cockpit_hook.cpp
  src/dll/wheel_ffb.cpp)
target_link_libraries(FH2SimImmersion PRIVATE ws2_32 dinput8 dxguid ole32)
target_compile_definitions(FH2SimImmersion PRIVATE FH2SI_VERSION="${PROJECT_VERSION}")
set_target_properties(FH2SimImmersion PROPERTIES PREFIX "")

# The settings window / launcher (plain Win32; comctl32 v6 and DPI awareness come from the manifest in app.rc)
add_executable(FH2SimImmersionApp WIN32
  src/app/main.cpp
  src/app/page.cpp
  src/app/draw.cpp
  src/app/ini.cpp
  src/app/injector.cpp
  src/app/pages_head.cpp
  src/app/pages_wheel.cpp
  src/app/pages_sim.cpp
  src/app/pages_draw.cpp
  src/app/pages_main.cpp
  src/app/app.rc)
set_target_properties(FH2SimImmersionApp PROPERTIES OUTPUT_NAME FH2SimImmersion)
target_compile_definitions(FH2SimImmersionApp PRIVATE FH2SI_VERSION="${PROJECT_VERSION}" UNICODE _UNICODE)
target_link_libraries(FH2SimImmersionApp PRIVATE comctl32 psapi shlwapi ole32 shell32 uuid gdi32 dinput8 dxguid)
target_link_options(FH2SimImmersionApp PRIVATE -municode -mwindows)

# Integration test (needs your own copy of the game executable to run)
add_executable(harness tests/harness.cpp)
target_link_libraries(harness PRIVATE ws2_32)
target_link_options(harness PRIVATE -municode)
