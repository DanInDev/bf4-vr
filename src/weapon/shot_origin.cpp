#include "weapon/shot_origin.hpp"

#include <windows.h>

#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include <MinHook.h>

#include "common/log.hpp"
#include "input/vr_controls.hpp"
#include "safety/mp_guard.hpp"
#include "stereo/camera_hook.hpp"
#include "weapon/shot_frame.hpp"

namespace bfvr::weapon
{
	namespace
	{
		std::atomic<bool> enabled{ true };
		std::atomic<std::uint64_t> calls{};
		std::atomic<std::uint64_t> replaced{};
		std::atomic<float> last_offset[3]{};
		std::atomic<float> last_forward[3]{};
		constexpr float max_reach = 0.8f; // metres from the camera: never from beyond a wall the gun pokes into

#if defined(_M_X64)
		// The weapon's shot-frame setter: four 16-byte copies from rdx to rcx +0x40..+0x7F, ret.
		constexpr std::uintptr_t setter_address = 0x1401473B0;
		constexpr std::uint8_t setter_bytes[] = { 0x0F, 0x28, 0x02, 0x0F, 0x29, 0x41, 0x40, 0x0F, 0x28, 0x4A, 0x10, 0x0F, 0x29, 0x49, 0x50, 0x0F, 0x28, 0x42,
			0x20, 0x0F, 0x29, 0x41, 0x60, 0x0F, 0x28, 0x4A, 0x30, 0x0F, 0x29, 0x49, 0x70, 0xC3 };
		using setter_t = void(__fastcall*)(std::uintptr_t weapon, const float* frame);
		setter_t original_setter{};

		// The local current weapon, looked up at most every 200 ms (the setter runs for every weapon).
		std::atomic<std::uintptr_t> local_weapon{};
		std::atomic<ULONGLONG> local_weapon_at{};

		std::uintptr_t current_local_weapon()
		{
			const ULONGLONG now = GetTickCount64();
			if (now - local_weapon_at.load(std::memory_order_relaxed) > 200)
			{
				local_weapon_at = now;
				local_weapon = input::current_client_weapon();
			}
			return local_weapon.load(std::memory_order_relaxed);
		}

		void __fastcall setter_detour(std::uintptr_t weapon, const float* frame)
		{
			++calls;
			if (frame && weapon && enabled.load(std::memory_order_relaxed) && input::controller_aim_active() && !safety::mod_disabled()
				&& weapon == current_local_weapon())
			{
				shot_line_local line;
				float yaw{};
				if (input::shot_line(line.start, line.direction) && input::body_yaw_now(yaw))
				{
					float game[16];
					std::memcpy(game, frame, sizeof(game));
					alignas(16) float moved[16];
					shot_frame_at_gun(game, yaw, stereo::physical_stance_lift(), line, max_reach, moved);
					for (int axis = 0; axis < 3; ++axis)
					{
						last_offset[axis] = moved[12 + axis] - game[12 + axis];
						last_forward[axis] = moved[8 + axis];
					}
					++replaced;
					original_setter(weapon, moved);
					return;
				}
			}
			original_setter(weapon, frame);
		}
#endif
	}

#if defined(_M_X64)
	namespace
	{
		// The game's code is decrypted after start-up: wait until the setter's bytes are there.
		DWORD WINAPI wait_and_hook(LPVOID)
		{
			auto* const target = reinterpret_cast<void*>(setter_address);
			std::uint8_t bytes[sizeof(setter_bytes)]{};
			for (int attempt = 0; attempt < 600; ++attempt)
			{
				if (safety::mod_disabled())
				{
					return 0;
				}
				if (ReadProcessMemory(GetCurrentProcess(), target, bytes, sizeof(bytes), nullptr) && std::memcmp(bytes, setter_bytes, sizeof(bytes)) == 0)
				{
					MH_STATUS status = MH_Initialize();
					if (status == MH_OK || status == MH_ERROR_ALREADY_INITIALIZED)
					{
						status = MH_CreateHook(target, reinterpret_cast<void*>(&setter_detour), reinterpret_cast<void**>(&original_setter));
						if (status == MH_OK)
						{
							status = MH_EnableHook(target);
						}
					}
					log::write("shot origin: shot-frame setter hook at %p: %s", target, MH_StatusToString(status));
					return 0;
				}
				Sleep(200);
			}
			log::write("shot origin: the shot-frame setter never matched build 179547; not hooked");
			return 0;
		}
	}
#endif

	bool install_shot_origin_hook()
	{
#if defined(_M_X64)
		if (HANDLE thread = CreateThread(nullptr, 0, &wait_and_hook, nullptr, 0, nullptr))
		{
			CloseHandle(thread);
			return true;
		}
		return false;
#else
		log::write("shot origin: BF4 x64 only; not installed");
		return false;
#endif
	}

	void set_shots_from_gun(bool on)
	{
		enabled = on;
	}

	std::string shot_origin_report()
	{
		char text[256];
		std::snprintf(text, sizeof(text), "shots_from_gun=%d calls=%llu replaced=%llu start_from_camera=(%.3f %.3f %.3f) forward=(%.3f %.3f %.3f)", enabled.load() ? 1 : 0,
			static_cast<unsigned long long>(calls.load()), static_cast<unsigned long long>(replaced.load()), last_offset[0].load(), last_offset[1].load(),
			last_offset[2].load(), last_forward[0].load(), last_forward[1].load(), last_forward[2].load());
		return text;
	}
}
