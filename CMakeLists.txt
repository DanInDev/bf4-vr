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
# CI turns this on so a new warning fails the build; it only applies to bf-vr's own code.
option(BFVR_WARNINGS_AS_ERRORS "Treat compiler warnings in bf-vr code as errors" OFF)

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
if(BFVR_WARNINGS_AS_ERRORS)
	target_compile_options(bfvr_common PUBLIC /WX)
endif()

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
	src/hooks/rumble_hook.cpp
	src/hooks/swap_chain_size.cpp
	src/hooks/xinput_hook.cpp
	src/input/gamepad.cpp
	src/input/fingers.cpp
	src/input/gestures.cpp
	src/input/handedness.cpp
	src/input/pointer.cpp
	src/input/right_stick.cpp
	src/input/virtual_pad.cpp
	src/input/vr_controls.cpp
	src/safety/mp_guard.cpp
	src/body/arm_ik.cpp
	src/body/arm_pose.cpp
	src/body/finger_pose.cpp
	src/body/gun_recoil.cpp
	src/body/torso.cpp
	src/body/grasp_contact.cpp
	src/stereo/camera_hook.cpp
	src/stereo/constant_patch.cpp
	src/stereo/eye_math.cpp
	src/stereo/eye_pass.cpp
	src/stereo/hud_capture.cpp
	src/stereo/hud_layout.cpp
	src/stereo/tracking.cpp
	src/stereo/world_pass.cpp
	src/weapon/accessory_touch.cpp
	src/weapon/grab_debug.cpp
	src/weapon/grenade_hands.cpp
	src/weapon/hand_signal.cpp
	src/weapon/hit_feedback.cpp
	src/weapon/holsters.cpp
	src/weapon/melee_gesture.cpp
	src/weapon/use_reach.cpp
	src/input/haptics.cpp
	src/weapon/physical_grenade.cpp
	src/weapon/physical_reload.cpp
	src/weapon/profiles.cpp
	src/weapon/reload.cpp
	src/weapon/reload_hands.cpp
	src/weapon/game_reload_sounds.cpp
	src/weapon/reload_sounds.cpp
	src/ui/menu_button.cpp
	src/ui/panel_canvas.cpp
	src/ui/reticle.cpp
	src/ui/settings_panel.cpp
	src/ui/vignette.cpp
	src/ui/vr_panel.cpp
	src/weapon/two_hand.cpp
	src/weapon/two_handed.cpp
	src/weapon/weapon_swap.cpp
	src/xr/controller_actions.cpp
	src/xr/runtime_selection.cpp
	src/xr/theater.cpp
)
target_link_libraries(bfvr_runtime PUBLIC bfvr_common PRIVATE minhook OpenXR::openxr_loader OpenXR::headers d3d11 dxgi dxguid windowscodecs ole32 winmm)

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

	# Left-handed role swap of pad and hand poses; exits non-zero on failure.
	add_executable(bfvr_handedness_test tests/handedness_test.cpp src/input/handedness.cpp)
	target_link_libraries(bfvr_handedness_test PRIVATE bfvr_common)

	# Physical reload state machine (AK and AR variants); exits non-zero on failure.
	add_executable(bfvr_reload_test tests/reload_test.cpp src/weapon/accessory_touch.cpp src/weapon/grenade_hands.cpp src/weapon/hand_signal.cpp src/weapon/holsters.cpp src/weapon/reload.cpp src/weapon/reload_hands.cpp src/weapon/game_reload_sounds.cpp src/weapon/profiles.cpp)
	target_link_libraries(bfvr_reload_test PRIVATE bfvr_common)

	# Two-handed grip and the converged shot direction; exits non-zero on failure.
	add_executable(bfvr_two_hand_test tests/two_hand_test.cpp src/weapon/two_hand.cpp src/body/arm_ik.cpp)
	target_link_libraries(bfvr_two_hand_test PRIVATE bfvr_common)

	# In-VR settings panel: pages, hit tests, clicks, GDI rasterising; with a path it writes a BMP snapshot.
	add_executable(bfvr_settings_panel_test tests/settings_panel_test.cpp src/ui/settings_panel.cpp src/ui/panel_canvas.cpp src/ui/menu_button.cpp)
	target_link_libraries(bfvr_settings_panel_test PRIVATE bfvr_common)

	# Comfort vignette strength and texture; exits non-zero on failure.
	add_executable(bfvr_vignette_test tests/vignette_test.cpp src/ui/vignette.cpp src/ui/reticle.cpp src/body/arm_ik.cpp)
	target_link_libraries(bfvr_vignette_test PRIVATE bfvr_common)

	# Melee gesture (thrust detection); exits non-zero on failure.
	add_executable(bfvr_melee_test tests/melee_test.cpp src/weapon/melee_gesture.cpp)
	target_link_libraries(bfvr_melee_test PRIVATE bfvr_common)

	# Haptics mixer (game rumble per hand, gesture pulses); exits non-zero on failure.
	add_executable(bfvr_haptics_test tests/haptics_test.cpp src/input/haptics.cpp)
	target_link_libraries(bfvr_haptics_test PRIVATE bfvr_common)

	# Use by reaching (the game's interact); exits non-zero on failure.
	add_executable(bfvr_use_reach_test tests/use_reach_test.cpp src/weapon/use_reach.cpp)
	target_link_libraries(bfvr_use_reach_test PRIVATE bfvr_common)

	# Finger poses (src/body/finger_pose); exits non-zero on failure.
	add_executable(bfvr_finger_pose_test tests/finger_pose_test.cpp src/body/finger_pose.cpp src/body/arm_ik.cpp)
	target_link_libraries(bfvr_finger_pose_test PRIVATE bfvr_common)

	# The drawn gun's kick (src/body/gun_recoil); exits non-zero on failure.
	add_executable(bfvr_gun_recoil_test tests/gun_recoil_test.cpp src/body/gun_recoil.cpp)
	target_link_libraries(bfvr_gun_recoil_test PRIVATE bfvr_common)
	add_executable(bfvr_torso_test tests/torso_test.cpp src/body/torso.cpp)
	target_link_libraries(bfvr_torso_test PRIVATE bfvr_common)
	add_executable(bfvr_grasp_contact_test tests/grasp_contact_test.cpp src/body/grasp_contact.cpp src/body/finger_pose.cpp src/body/arm_ik.cpp)
	target_link_libraries(bfvr_grasp_contact_test PRIVATE bfvr_common)

	# Sound research (re/notes/sound.md): records the default playback device; not a test.
	add_executable(bfvr_audio_loopback tools/audio_loopback.cpp)
	target_link_libraries(bfvr_audio_loopback PRIVATE ole32)
	target_compile_definitions(bfvr_audio_loopback PRIVATE NOMINMAX)
	target_compile_options(bfvr_audio_loopback PRIVATE /W4 /permissive-)

	# Offline tests (ctest --preset bf4-debug); the smoke theater needs an XR runtime and is not one.
	enable_testing()
	add_test(NAME arm_ik COMMAND bfvr_arm_ik_test)
	add_test(NAME eye_math COMMAND bfvr_eye_math_test)
	add_test(NAME settings COMMAND bfvr_settings_test)
	add_test(NAME right_stick COMMAND bfvr_right_stick_test)
	add_test(NAME gestures COMMAND bfvr_gestures_test)
	add_test(NAME handedness COMMAND bfvr_handedness_test)
	add_test(NAME reload COMMAND bfvr_reload_test)
	add_test(NAME two_hand COMMAND bfvr_two_hand_test)
	add_test(NAME settings_panel COMMAND bfvr_settings_panel_test)
	add_test(NAME vignette COMMAND bfvr_vignette_test)
	add_test(NAME melee COMMAND bfvr_melee_test)
	add_test(NAME use_reach COMMAND bfvr_use_reach_test)
	add_test(NAME haptics COMMAND bfvr_haptics_test)
	add_test(NAME finger_pose COMMAND bfvr_finger_pose_test)
	add_test(NAME gun_recoil COMMAND bfvr_gun_recoil_test)
	add_test(NAME torso COMMAND bfvr_torso_test)
	add_test(NAME grasp_contact COMMAND bfvr_grasp_contact_test)
endif()

# Desktop launcher, BF4 only: installs the proxy, edits bfvr.ini and starts the game.
if(BFVR_GAME STREQUAL "bf4")
	add_library(bfvr_launcher_core STATIC
		src/launcher/ini_file.cpp
		src/launcher/install.cpp
		src/launcher/vr_setup.cpp
	)
	target_link_libraries(bfvr_launcher_core PUBLIC bfvr_common PRIVATE bcrypt shell32 ole32)

	add_executable(bfvr_launcher WIN32
		src/launcher/main.cpp
		src/launcher/theme.cpp
		src/launcher/launcher.rc
		src/launcher/launcher.manifest
	)
	target_link_libraries(bfvr_launcher PRIVATE bfvr_launcher_core comctl32 shell32 ole32 dwmapi uxtheme windowscodecs)
	# The resource script finds the icon and the wallpaper here.
	target_include_directories(bfvr_launcher PRIVATE ${CMAKE_SOURCE_DIR}/assets/launcher)
	target_compile_definitions(bfvr_launcher PRIVATE BFVR_BUILD_CONFIG="$<CONFIG>")
	# The launcher seeds a new bfvr.ini from the template beside it, as in the release zip.
	add_custom_command(TARGET bfvr_launcher POST_BUILD
		COMMAND ${CMAKE_COMMAND} -E copy_if_different ${CMAKE_SOURCE_DIR}/tools/bfvr.ini.example $<TARGET_FILE_DIR:bfvr_launcher>
	)

	if(BFVR_BUILD_TESTS)
		add_executable(bfvr_launcher_test tests/launcher_test.cpp)
		target_link_libraries(bfvr_launcher_test PRIVATE bfvr_launcher_core)
		add_test(NAME launcher COMMAND bfvr_launcher_test)
	endif()
endif()
