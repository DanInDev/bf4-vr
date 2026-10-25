#pragma once

#include <string>

namespace bfvr::xr
{
	// Points the OpenXR loader at the runtime chosen by the xr_runtime setting, through
	// XR_RUNTIME_JSON for this process only; the system-wide registration is never changed.
	// Must run before xrCreateInstance. Returns a description of the choice for the log.
	//
	//   auto     SteamVR when vrserver.exe is running (Steam Link, Lighthouse), else the system runtime
	//   system   the active runtime registered in Windows (Meta Horizon Link on this PC)
	//   steamvr  SteamVR's OpenXR runtime, located via openvrpaths.vrpath
	//   <path>   any runtime manifest, e.g. a simulator's .json
	//
	// An XR_RUNTIME_JSON already set by the user always wins.
	std::string select_runtime();

	// True when SteamVR's server is running. Creating an instance on SteamVR's runtime launches
	// SteamVR, so retries wait for this instead (otherwise SteamVR keeps restarting while the user
	// tries to connect the headset, headset test 2026-10-25).
	bool steamvr_running();
}
