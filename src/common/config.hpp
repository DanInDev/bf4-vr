#pragma once

#include <string>

namespace bfvr::config
{
	// Settings come from bfvr.ini beside the executable ("key = value" lines, '#' comments).
	// An environment variable BFVR_<KEY> (upper case) overrides the file.
	//
	// Keys:
	//   theater      = 1 | 0                             the OpenXR session: flat screen and VR (default 1)
	//   xr_runtime   = auto | system | steamvr | <path>   OpenXR runtime manifest (default auto)
	//   gamepad      = 1 | 0                             VR controllers as an Xbox pad (default 1)
	//   virtual_pad  = 1 | 0                             offer a fake XInput pad to BF4's DirectInput enumeration, so the
	//                                                    VR controllers work without a physical pad (default 1)
	//   control_pipe = 1 | 0                             \\.\pipe\bfvr status/screenshot server (default 1)
	//   nvapi_probe  = 0 | 1                             log NVAPI function IDs the game requests (default 0)
	//   dxgi_probe   = 0 | 1                             log DXGI stereo queries and swap chain creation (default 0)
	//   camera_hook  = 1 | 0                             hook the per-frame view copy (default 1; idle unless used)
	//   stereo       = 0 | 1                             head-tracked 3D (default 0; the launcher's VR setup sets 1)
	//   stereo_render = pair | aer                       both eyes from one game frame, or alternate eyes (default pair)
	//   stereo_first_person = 1 | 0                      apply the eye pose to the first-person (weapon/arms) camera too (default 1)
	//   stereo_aspect = headset | game                   per-eye render aspect (default headset)
	//   gun_scale    = <float>                           apparent gun/arms size, 1 = the game's (default 1)
	//   view_crop_h / view_crop_v = <0.4..1>             rendered part of the headset FOV (default 1)
	//   arm_ik       = 1 | 0                             first-person arms follow the VR controllers (default 1)
	//   arm_ik_weapon = 1 | 0                            the gun (Wep_Root) follows the right hand (default 1)
	//   arm_ik_wrist = <x y z>                           wrist relative to the grip pose, metres, right hand (default 0 0 0.06)
	//   arm_ik_pole  = <x y z>                           elbow direction from the shoulder, camera axes, right arm (default 0.5 -1 0.3)
	//   render_size  = 0 | <w>x<h>                       virtual fullscreen mode: the game renders at that size (default 0, experimental)
	//   hud_layer    = 0 | 1 | 2                         HUD on its own head-locked layer, not in the eye images
	//                                                    (default 0; 2 = also without head tracking, monitor tests)
	//   keep_fullscreen, arm_pose_hook, research_views*  see tools/bfvr.ini.example and stereo/world_pass.hpp
	// Live settings (hud_distance, hud_width and the rest) are in settings_store.cpp, not here.
	//   keep_focus   = 1 | 0                             hide focus loss from the game (no 30 fps / 720p drop) (default 1)
	//   research     = 1 | 0                             memory research pipe commands and research hooks
	//                                                    (default 1 in Debug builds, 0 in Release builds)
	std::string get(const char* key, const char* fallback);
	// bfvr.ini beside the executable (empty when the path cannot be built).
	std::wstring ini_path();
	bool get_bool(const char* key, bool fallback);

	// Research tooling (dump, poke, watch, pins, the arm-pose research hook) stays out of release
	// builds unless explicitly enabled.
	bool research_enabled();
}
