#include "hooks/rumble_hook.hpp"

#include <windows.h>

#include <atomic>
#include <cstdint>
#include <cstring>

#include <MinHook.h>

#include "common/log.hpp"
#include "input/haptics.hpp"
#include "safety/mp_guard.hpp"

namespace bfvr::hooks
{
	namespace
	{
		std::atomic<bool> active{};
	}

#if defined(_M_X64)
	namespace
	{
		// push rbx; sub rsp,50h; mov rax,[rip+1A1775Bh]; movaps [rsp+30h],xmm7
		constexpr std::uintptr_t rumble_setter = 0x14094D490;
		constexpr std::uint8_t expected_prologue[] = { 0x40, 0x53, 0x48, 0x83, 0xEC, 0x50, 0x48, 0x8B, 0x05, 0x5B, 0x77, 0xA1, 0x01, 0x0F, 0x29, 0x7C, 0x24, 0x30 };

		using rumble_setter_t = void(__fastcall*)(void* pads, float low, float high);
		rumble_setter_t original{};

		void __fastcall rumble_detour(void* pads, float low, float high)
		{
			input::set_game_rumble(low, high);
			original(pads, low, high);
		}

		DWORD WINAPI wait_and_hook(LPVOID)
		{
			auto* const target = reinterpret_cast<void*>(rumble_setter);
			std::uint8_t bytes[sizeof(expected_prologue)]{};
			for (int attempt = 0; attempt < 600; ++attempt)
			{
				if (safety::mod_disabled())
				{
					return 0;
				}
				if (ReadProcessMemory(GetCurrentProcess(), target, bytes, sizeof(bytes), nullptr) && std::memcmp(bytes, expected_prologue, sizeof(bytes)) == 0)
				{
					MH_STATUS status = MH_Initialize();
					if (status == MH_OK || status == MH_ERROR_ALREADY_INITIALIZED)
					{
						status = MH_CreateHook(target, reinterpret_cast<void*>(&rumble_detour), reinterpret_cast<void**>(&original));
						if (status == MH_OK)
						{
							status = MH_EnableHook(target);
						}
					}
					active = status == MH_OK;
					log::write("rumble: setter hook at %p: %s", target, MH_StatusToString(status));
					return 0;
				}
				Sleep(200);
			}
			log::write("rumble: setter never matched build 179547; using XInputSetState (focus-gated)");
			return 0;
		}
	}

	bool install_rumble_hook()
	{
		const HANDLE thread = CreateThread(nullptr, 0, &wait_and_hook, nullptr, 0, nullptr);
		if (!thread)
		{
			return false;
		}
		CloseHandle(thread);
		return true;
	}
#else
	bool install_rumble_hook()
	{
		return false;
	}
#endif

	bool rumble_hook_active()
	{
		return active.load(std::memory_order_relaxed);
	}
}
