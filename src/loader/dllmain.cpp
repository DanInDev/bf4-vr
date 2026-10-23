// The game loads this file as dinput8.dll. It forwards DirectInput8Create to the
// real system DLL, logs that we are in-process and starts the runtime (core).

#include <windows.h>
#include <unknwn.h>

#include <cwchar>

#include "common/log.hpp"
#include "core/core.hpp"

namespace
{
	using direct_input8_create_t = HRESULT(WINAPI*)(HINSTANCE, DWORD, REFIID, LPVOID*, LPUNKNOWN);

	direct_input8_create_t system_direct_input8_create()
	{
		// Resolved on first call, never in DllMain: LoadLibrary under the loader lock can deadlock.
		static const direct_input8_create_t target = []() -> direct_input8_create_t
		{
			wchar_t path[MAX_PATH]{};
			const UINT length = GetSystemDirectoryW(path, MAX_PATH);
			if (length == 0 || length >= MAX_PATH || wcscat_s(path, L"\\dinput8.dll") != 0)
			{
				bfvr::log::write("could not build the system dinput8.dll path");
				return nullptr;
			}

			const HMODULE system_dll = LoadLibraryW(path);
			if (!system_dll)
			{
				bfvr::log::write("LoadLibrary(system dinput8.dll) failed: %lu", GetLastError());
				return nullptr;
			}

			return reinterpret_cast<direct_input8_create_t>(GetProcAddress(system_dll, "DirectInput8Create"));
		}();

		return target;
	}
}

extern "C" HRESULT WINAPI proxy_direct_input8_create(HINSTANCE instance, DWORD version, REFIID interface_id,
	LPVOID* output, LPUNKNOWN outer)
{
	const auto target = system_direct_input8_create();
	if (!target)
	{
		return E_FAIL;
	}

	const HRESULT result = target(instance, version, interface_id, output, outer);
	bfvr::log::write("DirectInput8Create(version=0x%04lX) forwarded, hr=0x%08lX", version, result);
	return result;
}

BOOL WINAPI DllMain(HINSTANCE module, DWORD reason, LPVOID)
{
	if (reason == DLL_PROCESS_ATTACH)
	{
		DisableThreadLibraryCalls(module);

		char executable[MAX_PATH]{};
		GetModuleFileNameA(nullptr, executable, MAX_PATH);
		bfvr::log::write("bfvr loader attached: game=%s pid=%lu process=%s pointer_bits=%u",
			BFVR_GAME_NAME, GetCurrentProcessId(), executable, static_cast<unsigned>(sizeof(void*) * 8));
		bfvr::core::start();
	}
	else if (reason == DLL_PROCESS_DETACH)
	{
		bfvr::log::write("bfvr loader detached");
	}

	return TRUE;
}
