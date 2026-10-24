#pragma once

#include <string>

namespace bfvr::config
{
	// Settings come from bfvr.ini beside the executable ("key = value" lines, '#' comments).
	// An environment variable BFVR_<KEY> (upper case) overrides the file.
	//
	// Keys:
	//   theater      = 1 | 0                             stage 1 virtual screen (default 1)
	//   xr_runtime   = auto | system | steamvr | <path>   OpenXR runtime manifest (default auto)
	//   gamepad      = 1 | 0                             VR controllers as an Xbox pad (default 1)
	//   control_pipe = 1 | 0                             \\.\pipe\bfvr status/screenshot server (default 1)
	//   nvapi_probe  = 0 | 1                             log NVAPI function IDs the game requests (default 0)
	//   dxgi_probe   = 0 | 1                             log DXGI stereo queries and swap chain creation (default 0)
	//   camera_hook  = 1 | 0                             hook the per-frame view copy (default 1; idle unless used)
	//   stereo       = 0 | 1                             alternate-eye stereo with head tracking (default 0)
	//   stereo_first_person = 1 | 0                      apply the eye pose to the first-person (weapon/arms) camera too (default 1)
	std::string get(const char* key, const char* fallback);
	bool get_bool(const char* key, bool fallback);
}
