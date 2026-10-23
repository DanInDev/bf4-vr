#include "hooks/xinput_hook.hpp"

#include <windows.h>
#include <Xinput.h>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <iterator>

#include <MinHook.h>

#include "common/log.hpp"
#include "input/gamepad.hpp"

namespace bfvr::hooks
{
	namespace
	{
		using get_state_t = DWORD(WINAPI*)(DWORD, XINPUT_STATE*);
		using get_capabilities_t = DWORD(WINAPI*)(DWORD, DWORD, XINPUT_CAPABILITIES*);

		constexpr DWORD virtual_user_index = 0;
		constexpr const wchar_t* modules[] = { L"xinput1_4.dll", L"xinput1_3.dll", L"xinput9_1_0.dll" };
		constexpr int module_count = static_cast<int>(std::size(modules));

		// MinHook returns one trampoline per target, so each DLL gets its own detour instance.
		get_state_t original_get_state[module_count]{};
		get_capabilities_t original_get_capabilities[module_count]{};
		DWORD packet_number{};

		SHORT add_axis(SHORT real, std::int16_t extra)
		{
			return static_cast<SHORT>(std::clamp(static_cast<int>(real) + extra, -32768, 32767));
		}

		// Diagnostics: does the game poll at all (e.g. without a physical pad plugged in)?
		std::atomic<std::uint64_t> state_calls{};
		std::atomic<std::uint64_t> capability_calls{};

		DWORD merge_state(DWORD result, DWORD user_index, XINPUT_STATE* state)
		{
			++state_calls;
			if (user_index != virtual_user_index || !state)
			{
				return result;
			}

			const input::gamepad_state pad = input::latest();
			if (!pad.connected)
			{
				return result;
			}

			if (result != ERROR_SUCCESS)
			{
				*state = {};
			}

			XINPUT_GAMEPAD& gamepad = state->Gamepad;
			gamepad.wButtons |= pad.buttons;
			gamepad.bLeftTrigger = std::max(gamepad.bLeftTrigger, pad.left_trigger);
			gamepad.bRightTrigger = std::max(gamepad.bRightTrigger, pad.right_trigger);
			gamepad.sThumbLX = add_axis(gamepad.sThumbLX, pad.left_x);
			gamepad.sThumbLY = add_axis(gamepad.sThumbLY, pad.left_y);
			gamepad.sThumbRX = add_axis(gamepad.sThumbRX, pad.right_x);
			gamepad.sThumbRY = add_axis(gamepad.sThumbRY, pad.right_y);
			// A changing packet number tells the game the state may have changed.
			state->dwPacketNumber = ++packet_number;
			return ERROR_SUCCESS;
		}

		DWORD merge_capabilities(DWORD result, DWORD user_index, XINPUT_CAPABILITIES* capabilities)
		{
			++capability_calls;
			if (user_index != virtual_user_index || !capabilities || result == ERROR_SUCCESS || !input::latest().connected)
			{
				return result;
			}

			*capabilities = {};
			capabilities->Type = XINPUT_DEVTYPE_GAMEPAD;
			capabilities->SubType = XINPUT_DEVSUBTYPE_GAMEPAD;
			capabilities->Gamepad.wButtons = 0xF3FF;
			capabilities->Gamepad.bLeftTrigger = 0xFF;
			capabilities->Gamepad.bRightTrigger = 0xFF;
			capabilities->Gamepad.sThumbLX = capabilities->Gamepad.sThumbLY = static_cast<SHORT>(0xFFC0);
			capabilities->Gamepad.sThumbRX = capabilities->Gamepad.sThumbRY = static_cast<SHORT>(0xFFC0);
			return ERROR_SUCCESS;
		}

		template <int N>
		DWORD WINAPI get_state_detour(DWORD user_index, XINPUT_STATE* state)
		{
			return merge_state(original_get_state[N](user_index, state), user_index, state);
		}

		template <int N>
		DWORD WINAPI get_capabilities_detour(DWORD user_index, DWORD flags, XINPUT_CAPABILITIES* capabilities)
		{
			return merge_capabilities(original_get_capabilities[N](user_index, flags, capabilities), user_index, capabilities);
		}

		constexpr get_state_t get_state_detours[] = { &get_state_detour<0>, &get_state_detour<1>, &get_state_detour<2> };
		constexpr get_capabilities_t get_capabilities_detours[] = { &get_capabilities_detour<0>, &get_capabilities_detour<1>, &get_capabilities_detour<2> };

		// Returns true when the target is hooked, including by an earlier module that forwards to it.
		bool hook(void* target, void* detour, void** original, const char* name)
		{
			MH_STATUS status = MH_CreateHook(target, detour, original);
			if (status == MH_ERROR_ALREADY_CREATED)
			{
				return true;
			}
			if (status == MH_OK)
			{
				status = MH_EnableHook(target);
			}
			if (status != MH_OK)
			{
				log::write("xinput hook: %s at %p failed: %s", name, target, MH_StatusToString(status));
				return false;
			}
			return true;
		}
	}

	xinput_stats xinput_call_counts()
	{
		return { state_calls.load(), capability_calls.load() };
	}

	bool install_xinput_hooks()
	{
		const MH_STATUS status = MH_Initialize();
		if (status != MH_OK && status != MH_ERROR_ALREADY_INITIALIZED)
		{
			log::write("xinput hook: MH_Initialize failed: %s", MH_StatusToString(status));
			return false;
		}

		bool any_loaded = std::any_of(std::begin(modules), std::end(modules), [](const wchar_t* name) { return GetModuleHandleW(name) != nullptr; });
		if (!any_loaded)
		{
			LoadLibraryW(L"xinput9_1_0.dll");
		}

		int hooked = 0;
		for (int i = 0; i < module_count; ++i)
		{
			const HMODULE module = GetModuleHandleW(modules[i]);
			if (!module)
			{
				continue;
			}

			void* const get_state = reinterpret_cast<void*>(GetProcAddress(module, "XInputGetState"));
			void* const get_capabilities = reinterpret_cast<void*>(GetProcAddress(module, "XInputGetCapabilities"));
			if (get_state && hook(get_state, reinterpret_cast<void*>(get_state_detours[i]), reinterpret_cast<void**>(&original_get_state[i]), "XInputGetState"))
			{
				++hooked;
				log::write("xinput hook: %ls XInputGetState at %p", modules[i], get_state);
			}
			if (get_capabilities)
			{
				hook(get_capabilities, reinterpret_cast<void*>(get_capabilities_detours[i]),
					reinterpret_cast<void**>(&original_get_capabilities[i]), "XInputGetCapabilities");
			}
		}

		if (!hooked)
		{
			log::write("xinput hook: no XInput DLL could be hooked");
		}
		return hooked > 0;
	}
}
