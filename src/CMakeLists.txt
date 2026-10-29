cmake_minimum_required(VERSION 3.25)
project(DishonoredVR LANGUAGES CXX)

if((NOT MSVC) OR (NOT (CMAKE_SIZEOF_VOID_P EQUAL 8)))
    message(FATAL_ERROR "Build with MSVC x64")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CONFIGURATION_TYPES Release RelWithDebInfo)
# The C/C++ runtime is linked INTO both DLLs (/MT, vcpkg triplet x64-windows-static). With /MD the mod used the
# msvcp140/vcruntime140 installed on the player's machine: a machine with an older Visual C++ redistributable than
# this toolset expects can crash at load (the 2024 std::mutex change), and the game shares ucrtbase with us, so a
# setlocale() by anyone in the process would change how our fprintf/fscanf write and read decimals.
set(CMAKE_MSVC_RUNTIME_LIBRARY MultiThreaded)
add_compile_definitions(_CRT_SECURE_NO_WARNINGS NOMINMAX WIN32_LEAN_AND_MEAN UNICODE _UNICODE)

# The mod's version: the release number below plus the commit it was built from (shown in the log and in the DLLs'
# file properties, so a tester's log says exactly which build it came from).
set(DVR_VERSION_MAJOR 0)
set(DVR_VERSION_MINOR 5)
set(DVR_VERSION_PATCH 0)
set(DVR_VERSION "${DVR_VERSION_MAJOR}.${DVR_VERSION_MINOR}.${DVR_VERSION_PATCH}")
set(DVR_GIT "nogit")
find_package(Git QUIET)
if(GIT_FOUND)
    execute_process(COMMAND "${GIT_EXECUTABLE}" describe --always --dirty --abbrev=7
        WORKING_DIRECTORY "${CMAKE_CURRENT_LIST_DIR}" OUTPUT_VARIABLE DVR_GIT OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
    if(NOT DVR_GIT)
        set(DVR_GIT "nogit")
    endif()
endif()
configure_file("${CMAKE_CURRENT_LIST_DIR}/Src/Version.h.in" "${CMAKE_BINARY_DIR}/generated/Version.h" @ONLY)
configure_file("${CMAKE_CURRENT_LIST_DIR}/Src/Version.rc.in" "${CMAKE_BINARY_DIR}/generated/Version.rc" @ONLY)
include_directories("${CMAKE_BINARY_DIR}/generated")
set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} /Zi /Gy")
set(CMAKE_SHARED_LINKER_FLAGS_RELEASE "${CMAKE_SHARED_LINKER_FLAGS_RELEASE} /debug /OPT:REF /OPT:ICF")

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}/CMake")
find_package(Detours MODULE REQUIRED)
find_package(fmt CONFIG REQUIRED)
find_package(imgui CONFIG REQUIRED)
find_package(OpenXR CONFIG REQUIRED)

set(OUT_DIR "${CMAKE_BINARY_DIR}/bin")

# --- dinput8 proxy: tiny, forwards DirectInput8Create and loads the mod DLL ---
add_library(dinput8 SHARED Proxy/dinput8.cpp Proxy/dinput8.def "${CMAKE_BINARY_DIR}/generated/Version.rc")
target_compile_definitions(dinput8 PRIVATE DVR_RC_PROXY)
set_target_properties(dinput8 PROPERTIES OUTPUT_NAME dinput8 PREFIX ""
    RUNTIME_OUTPUT_DIRECTORY "${OUT_DIR}" RUNTIME_OUTPUT_DIRECTORY_RELEASE "${OUT_DIR}")
# The DLLs name their PDB by file name only, not by the absolute path of the build machine (which ends up in every
# shipped binary otherwise); dbghelp finds it in the folder it is pointed at (tools/symbolize.py).
# The name is written out: "%_PDB%" went through the Visual Studio generator's escaping and the DLLs carried
# "%%%DishonoredVR.pdb%%%", a name dbghelp never finds (tools/symbolize.py resolved nothing; measured 2026-10-23).
target_link_options(dinput8 PRIVATE "/PDBALTPATH:dinput8.pdb")

# --- the mod ---
set(MOD_SOURCES
    Src/FrameProfile.cpp Src/FrameProfile.h
    Src/Main.cpp
    Src/Log.cpp        Src/Log.h
    Src/GameInfo.cpp   Src/GameInfo.h
    Src/CrashLog.cpp   Src/CrashLog.h
    Src/Watchdog.cpp   Src/Watchdog.h
    Src/CVars.cpp      Src/CVars.h
    Src/D3D11Hook.cpp  Src/D3D11Hook.h
    Src/RenderRes.cpp  Src/RenderRes.h
    Src/Overlay.cpp    Src/Overlay.h
    Src/WristHud.cpp   Src/WristHud.h
    Src/StereoTaa.cpp  Src/StereoTaa.h
    Src/TemporalCapture.cpp Src/TemporalCapture.h
    Src/GpuListTiming.cpp Src/GpuListTiming.h
    Src/TemporalBoundary.cpp Src/TemporalBoundary.h
    Src/DlssLive.cpp Src/DlssLive.h Src/DlssStageTiming.cpp Src/DlssStageTiming.h
    Src/StereoSync.cpp Src/StereoSync.h
    Src/LiveWheel.cpp Src/LiveWheel.h Src/WheelSelection.h
    Src/WheelRenderer.cpp Src/WheelRenderer.h Src/WheelModel.h
    Src/MetaAsw.cpp Src/MetaAsw.h
    Src/ScreenMode.cpp Src/ScreenMode.h
    Src/BinkVideo.cpp  Src/BinkVideo.h
    Src/XRProbe.cpp    Src/XRProbe.h
    Src/XRSession.cpp  Src/XRSession.h
    Src/XRCapture.cpp  Src/XRCapture.h
    Src/VRRuntime.cpp  Src/VRRuntime.h
    Src/Commands.cpp   Src/Commands.h
    Src/Game.cpp       Src/Game.h
    Src/CameraHook.cpp Src/CameraHook.h
    Src/PlayerViewHook.cpp Src/PlayerViewHook.h
    Src/ViewMatrixHook.cpp Src/ViewMatrixHook.h
    Src/XInputHook.cpp Src/XInputHook.h
    Src/PadProbe.cpp   Src/PadProbe.h
    Src/KbmInject.cpp  Src/KbmInject.h
    Src/HandAimHook.cpp Src/HandAimHook.h
    Src/BodyHide.cpp   Src/BodyHide.h
    Src/WeaponPose.cpp Src/WeaponPose.h
    Src/HandFx.cpp     Src/HandFx.h
    Src/Watchpoint.cpp Src/Watchpoint.h
    Src/GpuTrace.cpp   Src/GpuTrace.h Src/ArmCalibration.h Src/ArmHandback.h
    Src/Reticle.cpp    Src/Reticle.h
    Src/PlayerState.cpp Src/PlayerState.h
    Src/MeleeAim.cpp   Src/MeleeAim.h
    Src/SwordGesture.cpp Src/SwordGesture.h
    Src/SwordContact.cpp Src/SwordContact.h
    Src/NpcSkel.cpp Src/NpcSkel.h
    Src/ThrowGesture.cpp Src/ThrowGesture.h
    Src/GrabHand.cpp   Src/GrabHand.h
    Src/PointerRay.cpp Src/PointerRay.h
    Src/GunRay.cpp     Src/GunRay.h
    Src/BodyView.cpp   Src/BodyView.h
    Src/PipeTrace.cpp  Src/PipeTrace.h
    Src/TestAids.cpp   Src/TestAids.h
    Src/Settings.cpp   Src/Settings.h
    Src/KeyBinds.cpp   Src/KeyBinds.h
    Src/SysInfo.cpp    Src/SysInfo.h
    Src/Seeds.cpp      Src/Seeds.h
    "${CMAKE_BINARY_DIR}/generated/Version.rc"
    Src/VRMath.h
    Src/pch.h
)
add_library(DishonoredVR SHARED ${MOD_SOURCES} Src/Psvr2Triggers.cpp)
target_precompile_headers(DishonoredVR PRIVATE Src/pch.h)
target_link_libraries(DishonoredVR PRIVATE
    fmt::fmt imgui::imgui OpenXR::openxr_loader
    Detours::Detours
    d3d11 dxgi version windowscodecs shell32 ole32 advapi32)
set_target_properties(DishonoredVR PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${OUT_DIR}" RUNTIME_OUTPUT_DIRECTORY_RELEASE "${OUT_DIR}")
target_link_options(DishonoredVR PRIVATE "/PDBALTPATH:DishonoredVR.pdb")   # (see dinput8 above)

option(DVR_BUILD_WHEEL_PREVIEW "Build standalone wheel renderer (does not launch or install into the game)" OFF)
if(DVR_BUILD_WHEEL_PREVIEW)
    add_executable(wheel_preview ../tools/wheel_preview.cpp Src/WheelRenderer.cpp Src/XRCapture.cpp)
    target_include_directories(wheel_preview PRIVATE Src)
    target_link_libraries(wheel_preview PRIVATE imgui::imgui d3d11 dxgi windowscodecs ole32)
    set_target_properties(wheel_preview PROPERTIES RUNTIME_OUTPUT_DIRECTORY_RELEASE "${OUT_DIR}")
endif()

add_executable(dvr_psvr2_bridge Psvr2/Bridge.cpp)
set_target_properties(dvr_psvr2_bridge PROPERTIES RUNTIME_OUTPUT_DIRECTORY_RELEASE "${OUT_DIR}")

# Optional native D3D11 NGX backend. SDK stays local and is never vendored.
set(DVR_NGX_ROOT "" CACHE PATH "Path to the pinned NVIDIA NGX SDK")
if(DVR_NGX_ROOT)
    if(NOT EXISTS "${DVR_NGX_ROOT}/include/nvsdk_ngx.h" OR NOT EXISTS "${DVR_NGX_ROOT}/lib/Windows_x86_64/x64/nvsdk_ngx_s.lib")
        message(FATAL_ERROR "DVR_NGX_ROOT must contain the SDK headers and static-CRT x64 library")
    endif()
    add_library(dvr_dlss_backend STATIC Src/DlssD3D11.cpp)
    target_include_directories(dvr_dlss_backend PUBLIC Src PRIVATE "${DVR_NGX_ROOT}/include")
    target_link_libraries(dvr_dlss_backend PUBLIC d3d11 dxgi "${DVR_NGX_ROOT}/lib/Windows_x86_64/x64/nvsdk_ngx_s.lib")
    target_link_libraries(DishonoredVR PRIVATE dvr_dlss_backend)
    target_compile_definitions(DishonoredVR PRIVATE DVR_HAS_NGX=1)
endif()
add_executable(temporal_frame_test ../tests/temporal_frame_test.cpp)
target_include_directories(temporal_frame_test PRIVATE Src)
enable_testing()
add_test(NAME temporal_frame_contract COMMAND temporal_frame_test)
if(DVR_NGX_ROOT)
    add_executable(dlss_backend_smoke ../tests/dlss_backend_smoke.cpp)
    target_link_libraries(dlss_backend_smoke PRIVATE dvr_dlss_backend)
endif()

if(DVR_NGX_ROOT)
    add_executable(dlss_capture_replay ../tests/dlss_capture_replay.cpp)
    target_link_libraries(dlss_capture_replay PRIVATE dvr_dlss_backend)
endif()
add_library(dvr_afw_backend STATIC Src/AfwD3D11.cpp)
target_include_directories(dvr_afw_backend PUBLIC Src)
target_link_libraries(dvr_afw_backend PUBLIC d3d11 d3dcompiler)
add_executable(afw_backend_smoke ../tests/afw_backend_smoke.cpp)
target_link_libraries(afw_backend_smoke PRIVATE dvr_afw_backend)
add_executable(afw_capture_replay ../tests/afw_capture_replay.cpp)
target_link_libraries(afw_capture_replay PRIVATE dvr_afw_backend)
add_executable(temporal_boundary_smoke ../tests/temporal_boundary_smoke.cpp Src/TemporalBoundary.cpp)
target_include_directories(temporal_boundary_smoke PRIVATE Src)
target_link_libraries(temporal_boundary_smoke PRIVATE d3d11 d3dcompiler)

if(DVR_NGX_ROOT)
    add_executable(dlss_live_smoke ../tests/dlss_live_smoke.cpp Src/DlssLive.cpp Src/TemporalBoundary.cpp Src/DlssStageTiming.cpp)
    target_compile_definitions(dlss_live_smoke PRIVATE DVR_HAS_NGX=1)
    target_link_libraries(dlss_live_smoke PRIVATE dvr_dlss_backend d3dcompiler OpenXR::openxr_loader)
endif()

add_executable(dlss_stage_timing_smoke ../tests/dlss_stage_timing_smoke.cpp Src/DlssStageTiming.cpp)
target_include_directories(dlss_stage_timing_smoke PRIVATE Src)
target_link_libraries(dlss_stage_timing_smoke PRIVATE d3d11)

set(DVR_PDAFW_INCLUDE "" CACHE PATH "Local public PureDark AFW ABI header for optional live backend and probes")
if(DVR_PDAFW_INCLUDE)
    add_executable(puredark_afw_probe ../tools/puredark_afw_probe.cpp)
    target_include_directories(puredark_afw_probe PRIVATE "${DVR_PDAFW_INCLUDE}")
    target_link_libraries(puredark_afw_probe PRIVATE d3d11 d3d12 dxgi)
endif()
if(DVR_PDAFW_INCLUDE)
    add_library(dvr_puredark_backend STATIC Src/AfwPlugin.cpp)
    target_include_directories(dvr_puredark_backend PUBLIC Src PRIVATE "${DVR_PDAFW_INCLUDE}")
    target_link_libraries(dvr_puredark_backend PUBLIC d3d11 d3d12 dxgi)
endif()
if(DVR_PDAFW_INCLUDE)
    add_executable(puredark_backend_smoke ../tests/puredark_backend_smoke.cpp)
    target_link_libraries(puredark_backend_smoke PRIVATE dvr_puredark_backend)
endif()

if(DVR_PDAFW_INCLUDE AND DVR_NGX_ROOT)
    target_sources(DishonoredVR PRIVATE Src/AfwLive.cpp Src/AfwInputs.cpp)
    target_compile_definitions(DishonoredVR PRIVATE DVR_HAS_PDAFW=1)
    target_link_libraries(DishonoredVR PRIVATE dvr_puredark_backend)
endif()
add_executable(afw_inputs_smoke ../tests/afw_inputs_smoke.cpp Src/AfwInputs.cpp Src/DlssStageTiming.cpp)
target_include_directories(afw_inputs_smoke PRIVATE Src)
target_link_libraries(afw_inputs_smoke PRIVATE d3d11 d3dcompiler)

target_compile_definitions(DishonoredVR PRIVATE DVR_FRAME_PROFILE=1)
add_executable(frame_profile_smoke ../tests/frame_profile_smoke.cpp Src/FrameProfile.cpp)
target_include_directories(frame_profile_smoke PRIVATE Src)
target_compile_definitions(frame_profile_smoke PRIVATE DVR_FRAME_PROFILE=1)

target_link_libraries(frame_profile_smoke PRIVATE d3d11 dxgi psapi)

option(DVR_EXPERIMENTAL_SCENE_VRS "Build unaccepted scene VRS diagnostic" OFF)
set(DVR_NVAPI_ROOT "" CACHE PATH "NVIDIA NVAPI SDK directory")
target_sources(DishonoredVR PRIVATE Src/SceneFoveation.cpp)
if(DVR_EXPERIMENTAL_SCENE_VRS AND DVR_NVAPI_ROOT)
 target_compile_definitions(DishonoredVR PRIVATE DVR_HAS_NVAPI)
 target_include_directories(DishonoredVR PRIVATE "${DVR_NVAPI_ROOT}")
 target_link_libraries(DishonoredVR PRIVATE "${DVR_NVAPI_ROOT}/amd64/nvapi64.lib")
endif()
if(DVR_EXPERIMENTAL_SCENE_VRS AND DVR_NVAPI_ROOT)
 add_executable(scene_vrs_driver_smoke ../tests/scene_vrs_driver_smoke.cpp)
 target_include_directories(scene_vrs_driver_smoke PRIVATE "${DVR_NVAPI_ROOT}")
 target_link_libraries(scene_vrs_driver_smoke PRIVATE d3d11 "${DVR_NVAPI_ROOT}/amd64/nvapi64.lib")
endif()
