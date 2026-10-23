#pragma once

namespace bfvr::hooks
{
	// Research probe: detours nvapi_QueryInterface (the single entry point every NVAPI call goes
	// through) and logs each function ID the game asks for, once, with whether the driver has it.
	// Names are logged for the stereo IDs; map the rest with NVIDIA's nvapi_interface.h.
	bool install_nvapi_probe();
}
