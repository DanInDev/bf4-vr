#include "hooks/nvapi_hook.hpp"

#include <windows.h>

#include <iterator>
#include <mutex>
#include <unordered_set>

#include <MinHook.h>

#include "common/log.hpp"

namespace bfvr::hooks
{
	namespace
	{
		using query_interface_t = void*(__cdecl*)(unsigned int);

		// Function IDs from NVIDIA's public nvapi_interface.h (NVAPI SDK, MIT).
		struct known_function
		{
			unsigned int id;
			const char* name;
		};
		// Every NvAPI_Stereo_* entry of nvapi_interface.h (generated, not hand-typed).
		constexpr known_function stereo_functions[] = {
			{ 0xbe7692ec, "NvAPI_Stereo_CreateConfigurationProfileRegistryKey" },
			{ 0xf117b834, "NvAPI_Stereo_DeleteConfigurationProfileRegistryKey" },
			{ 0x24409f48, "NvAPI_Stereo_SetConfigurationProfileValue" },
			{ 0x49bceecf, "NvAPI_Stereo_DeleteConfigurationProfileValue" },
			{ 0x239c4545, "NvAPI_Stereo_Enable" },
			{ 0x2ec50c2b, "NvAPI_Stereo_Disable" },
			{ 0x348ff8e1, "NvAPI_Stereo_IsEnabled" },
			{ 0x296c434d, "NvAPI_Stereo_GetStereoSupport" },
			{ 0xac7e37f4, "NvAPI_Stereo_CreateHandleFromIUnknown" },
			{ 0x3a153134, "NvAPI_Stereo_DestroyHandle" },
			{ 0xf6a1ad68, "NvAPI_Stereo_Activate" },
			{ 0x2d68de96, "NvAPI_Stereo_Deactivate" },
			{ 0x1fb0bc30, "NvAPI_Stereo_IsActivated" },
			{ 0x451f2134, "NvAPI_Stereo_GetSeparation" },
			{ 0x5c069fa3, "NvAPI_Stereo_SetSeparation" },
			{ 0xda044458, "NvAPI_Stereo_DecreaseSeparation" },
			{ 0xc9a8ecec, "NvAPI_Stereo_IncreaseSeparation" },
			{ 0x4ab00934, "NvAPI_Stereo_GetConvergence" },
			{ 0x3dd6b54b, "NvAPI_Stereo_SetConvergence" },
			{ 0x4c87e317, "NvAPI_Stereo_DecreaseConvergence" },
			{ 0xa17daabe, "NvAPI_Stereo_IncreaseConvergence" },
			{ 0xe6839b43, "NvAPI_Stereo_GetFrustumAdjustMode" },
			{ 0x7be27fa2, "NvAPI_Stereo_SetFrustumAdjustMode" },
			{ 0x932cb140, "NvAPI_Stereo_CaptureJpegImage" },
			{ 0xc7177702, "NvAPI_Stereo_InitActivation" },
			{ 0x0d6c6cd2, "NvAPI_Stereo_Trigger_Activation" },
			{ 0x8b7e99b5, "NvAPI_Stereo_CapturePngImage" },
			{ 0x3cd58f89, "NvAPI_Stereo_ReverseStereoBlitControl" },
			{ 0x6b9b409e, "NvAPI_Stereo_SetNotificationMessage" },
			{ 0x96eea9f8, "NvAPI_Stereo_SetActiveEye" },
			{ 0x5e8f0bec, "NvAPI_Stereo_SetDriverMode" },
			{ 0xce653127, "NvAPI_Stereo_GetEyeSeparation" },
			{ 0x40c8ed5e, "NvAPI_Stereo_IsWindowedModeSupported" },
			{ 0xf5dcfcba, "NvAPI_Stereo_SetSurfaceCreationMode" },
			{ 0x36f1c736, "NvAPI_Stereo_GetSurfaceCreationMode" },
			{ 0xed4416c5, "NvAPI_Stereo_Debug_WasLastDrawStereoized" },
			{ 0x44f0ecd1, "NvAPI_Stereo_SetDefaultProfile" },
			{ 0x624e21c2, "NvAPI_Stereo_GetDefaultProfile" },
		};

		query_interface_t original_query{};
		std::mutex seen_lock;
		std::unordered_set<unsigned int> seen;

		const char* name_of(unsigned int id)
		{
			for (const known_function& function : stereo_functions)
			{
				if (function.id == id)
				{
					return function.name;
				}
			}
			return "";
		}

		void* __cdecl query_detour(unsigned int id)
		{
			void* const function = original_query(id);
			{
				const std::lock_guard guard(seen_lock);
				if (!seen.insert(id).second)
				{
					return function;
				}
			}
			log::write("nvapi: query 0x%08x %s -> %s", id, name_of(id), function ? "provided" : "NOT provided");
			return function;
		}
	}

	bool install_nvapi_probe()
	{
		const wchar_t* const library = sizeof(void*) == 8 ? L"nvapi64.dll" : L"nvapi.dll";
		// Loading it first gives us the same module instance the game will use.
		const HMODULE module = LoadLibraryW(library);
		if (!module)
		{
			log::write("nvapi: %ls not available (not an NVIDIA system?)", library);
			return false;
		}

		void* const target = reinterpret_cast<void*>(GetProcAddress(module, "nvapi_QueryInterface"));
		MH_STATUS status = MH_Initialize();
		if (!target || (status != MH_OK && status != MH_ERROR_ALREADY_INITIALIZED))
		{
			log::write("nvapi: probe setup failed");
			return false;
		}

		status = MH_CreateHook(target, reinterpret_cast<void*>(&query_detour), reinterpret_cast<void**>(&original_query));
		if (status == MH_OK)
		{
			status = MH_EnableHook(target);
		}
		if (status != MH_OK)
		{
			log::write("nvapi: hooking nvapi_QueryInterface failed: %s", MH_StatusToString(status));
			return false;
		}

		log::write("nvapi: probe installed on %ls!nvapi_QueryInterface", library);
		return true;
	}
}
