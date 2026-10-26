cmake_minimum_required(VERSION 3.25)
project(bfvr LANGUAGES C CXX)

# The DLL must match the game process: bf3.exe is x86, bf4.exe is x64.
set(BFVR_GAME "bf4" CACHE STRING "Target game: bf3 (x86) or bf4 (x64)")
set_property(CACHE BFVR_GAME PROPERTY STRINGS bf3 bf4)
if(BFVR_GAME STREQUAL "bf3")
	set(bfvr_pointer_size 4)
elseif(BFVR_GAME STREQUAL "bf4")
	set(bfvr_pointer_size 8)
else()
	message(FATAL_ERROR "BFVR_GAME must be bf3 or bf4, got '${BFVR_GAME}'.")
endif()
if(NOT CMAKE_SIZEOF_VOID_P EQUAL bfvr_pointer_size)
	message(FATAL_ERROR "${BFVR_GAME} needs a ${bfvr_pointer_size}-byte pointer build: use the matching preset (cmake --preset ${BFVR_GAME}).")
endif()

option(BFVR_BUILD_TESTS "Build the standalone smoke tests" ON)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# Static CRT: the proxy must not depend on a redistributable being present.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# --- Third-party -------------------------------------------------------------
include(FetchContent)

# OpenXR loader, linked statically so no openxr_loader.dll has to sit beside the game.
set(DYNAMIC_LOADER OFF CACHE BOOL "" FORCE)
set(BUILD_API_LAYERS OFF CACHE BOOL "" FORCE)
set(BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(BUILD_CONFORMANCE_TESTS OFF CACHE BOOL "" FORCE)
set(BUILD_SDK_TESTS OFF CACHE BOOL "" FORCE)
FetchContent_Declare(openxr
	GIT_REPOSITORY https://github.com/KhronosGroup/OpenXR-SDK.git
	GIT_TAG release-1.1.63
	GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(openxr)
# The SDK forces the DLL CRT on its static loader; match ours instead.
set_target_properties(openxr_loader PROPERTIES MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# MinHook sources are compiled directly to keep its CRT and options under our control.
FetchContent_Declare(minhook
	GIT_REPOSITORY https://github.com/TsudaKageyu/minhook.git
	GIT_TAG c3fcafdc10146beb5919319d0683e44e3c30d537 # v1.3.4
)
FetchContent_GetProperties(minhook)
if(NOT minhook_POPULATED)
	FetchContent_Populate(minhook)
endif()
if(CMAKE_SIZEOF_VOID_P EQUAL 8)
	set(minhook_hde ${minhook_SOURCE_DIR}/src/hde/hde64.c)
else()
	set(minhook_hde ${minhook_SOURCE_DIR}/src/hde/hde32.c)
endif()
add_library(minhook STATIC
	${minhook_SOURCE_DIR}/src/buffer.c
	${minhook_SOURCE_DIR}/src/hook.c
	${minhook_SOURCE_DIR}/src/trampoline.c
	${minhook_hde}
)
target_include_directories(minhook PUBLIC ${minhook_SOURCE_DIR}/include)

# --- bf-vr -----------------------------------------------------------------
add_library(bfvr_common STATIC
	src/common/config.cpp
	src/common/log.cpp
	src/common/settings.cpp
	src/common/settings_store.cpp
)
target_include_directories(bfvr_common PUBLIC src)
target_compile_definitions(bfvr_common PUBLIC UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN BFVR_GAME_NAME="${BFVR_GAME}")
target_compile_options(bfvr_common PUBLIC /W4 /permissive-)

# Everything that runs inside the game: hooks, OpenXR, start-up.
add_library(bfvr_runtime STATIC
	src/control/control_pipe.cpp
	src/control/crash_log.cpp
	src/control/frame_trace.cpp
	src/control/thread_stacks.cpp
	src/control/watchpoint.cpp
	src/core/core.cpp
	src/hooks/dxgi_probe.cpp
	src/hooks/game_window.cpp
	src/hooks/nvapi_hook.cpp
	src/hooks/present_hook.cpp
	src/hooks/swap_chain_size.cpp
	src/hooks/xinput_hook.cpp
	src/input/gamepad.cpp
	src/input/gestures.cpp
	src/input/pointer.cpp
	src/input/right_stick.cpp
	src/input/virtual_pad.cpp
	src/input/vr_controls.cpp
	src/safety/mp_guard.cpp
	src/body/arm_ik.cpp
	src/body/arm_pose.cpp
	src/stereo/camera_hook.cpp
	src/stereo/constant_patch.cpp
	src/stereo/eye_math.cpp
	src/stereo/eye_pass.cpp
	src/stereo/hud_capture.cpp
	src/stereo/hud_layout.cpp
	src/stereo/tracking.cpp
	src/stereo/world_pass.cpp
	src/xr/controller_actions.cpp
	src/xr/runtime_selection.cpp
	src/xr/theater.cpp
)
target_link_libraries(bfvr_runtime PUBLIC bfvr_common PRIVATE minhook OpenXR::openxr_loader OpenXR::headers d3d11 dxgi dxguid windowscodecs ole32)

# Loaded by the game as dinput8.dll; forwards to the system DLL and starts the runtime.
add_library(bfvr_loader SHARED
	src/loader/dllmain.cpp
	src/loader/dinput8.def
)
target_link_libraries(bfvr_loader PRIVATE bfvr_runtime)
set_target_properties(bfvr_loader PROPERTIES OUTPUT_NAME dinput8)

if(BFVR_BUILD_TESTS)
	# Drives the Present hook and theater mode from a plain D3D11 window, without the game.
	add_executable(bfvr_smoke_theater tests/smoke_theater.cpp)
	target_link_libraries(bfvr_smoke_theater PRIVATE bfvr_runtime d3d11 dxgi)

	# Arm IK solver on a synthetic chain; exits non-zero on failure.
	add_executable(bfvr_arm_ik_test tests/arm_ik_test.cpp src/body/arm_ik.cpp)
	target_link_libraries(bfvr_arm_ik_test PRIVATE bfvr_common)

	# Per-eye maths: pose composition, rendered frustum, complete pairs; exits non-zero on failure.
	add_executable(bfvr_eye_math_test tests/eye_math_test.cpp src/stereo/eye_math.cpp src/stereo/constant_patch.cpp src/stereo/hud_layout.cpp)
	target_link_libraries(bfvr_eye_math_test PRIVATE bfvr_common)

	# Settings registry: ini load, pipe text, write-back; exits non-zero on failure.
	add_executable(bfvr_settings_test tests/settings_test.cpp)
	target_link_libraries(bfvr_settings_test PRIVATE bfvr_common)

	# Right-stick turn/stance state machine; exits non-zero on failure.
	add_executable(bfvr_right_stick_test tests/right_stick_test.cpp src/input/right_stick.cpp)
	target_link_libraries(bfvr_right_stick_test PRIVATE bfvr_common)

	# HUD gesture detectors and the laser-pointer hit on synthetic poses; exits non-zero on failure.
	add_executable(bfvr_gestures_test tests/gestures_test.cpp src/input/gestures.cpp src/input/pointer.cpp src/body/arm_ik.cpp)
	target_link_libraries(bfvr_gestures_test PRIVATE bfvr_common)

	# Offline tests (ctest --preset bf4-debug); the smoke theater needs an XR runtime and is not one.
	enable_testing()
	add_test(NAME arm_ik COMMAND bfvr_arm_ik_test)
	add_test(NAME eye_math COMMAND bfvr_eye_math_test)
	add_test(NAME settings COMMAND bfvr_settings_test)
	add_test(NAME right_stick COMMAND bfvr_right_stick_test)
	add_test(NAME gestures COMMAND bfvr_gestures_test)
endif()
