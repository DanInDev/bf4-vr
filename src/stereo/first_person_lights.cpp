#include "stereo/first_person_lights.hpp"

#include <windows.h>

#include <atomic>
#include <cstdio>
#include <cstring>
#include <mutex>

#include <MinHook.h>

#include "common/log.hpp"
#include "safety/mp_guard.hpp"
#include "stereo/light_rebase.hpp"

namespace bfvr::stereo
{
	namespace
	{
		std::atomic<bool> enabled{ true };
		std::atomic<std::uint64_t> calls{};
		std::atomic<std::uint64_t> differed{}; // the two cameras were not the same view
		std::atomic<std::uint64_t> rebased{};  // calls the hook re-based from the drawn view
		std::mutex cameras_lock;
		float last_to[16]{};   // param_4: the view the lights are drawn in
		float last_from[16]{}; // param_5: the camera the game re-bases from
		bool cameras_known{};

#if defined(_M_X64)
		// FUN_140CC9C50 (re/symbols/bf4-179547.md "First-person light re-base"): the prologue up to its __chkstk call.
		constexpr std::uintptr_t rebase_address = 0x140CC9C50;
		constexpr std::uint8_t rebase_bytes[] = { 0x40, 0x55, 0x53, 0x56, 0x57, 0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57, 0x48, 0x8D,
			0xAC, 0x24, 0xA8, 0xB3, 0xFF, 0xFF, 0xB8, 0x58, 0x4D, 0x00, 0x00 };
		using rebase_t = void(__fastcall*)(void* lights, void* renderer, void* view, float* to, float* from);
		rebase_t original_rebase{};

		void __fastcall rebase_detour(void* lights, void* renderer, void* view, float* to, float* from)
		{
			++calls;
			if (to && from)
			{
				{
					const std::lock_guard guard(cameras_lock);
					std::memcpy(last_to, to, sizeof(last_to));
					std::memcpy(last_from, from, sizeof(last_from));
					cameras_known = true;
				}
				if (std::memcmp(to, from, sizeof(float) * 16) != 0)
				{
					++differed;
				}
				if (enabled.load(std::memory_order_relaxed) && !safety::mod_disabled())
				{
					++rebased;
					original_rebase(lights, renderer, view, to, to);
					return;
				}
			}
			original_rebase(lights, renderer, view, to, from);
		}

		// The game's code is decrypted after start-up: wait until the function's bytes are there.
		DWORD WINAPI wait_and_hook(LPVOID)
		{
			auto* const target = reinterpret_cast<void*>(rebase_address);
			std::uint8_t bytes[sizeof(rebase_bytes)]{};
			for (int attempt = 0; attempt < 600; ++attempt)
			{
				if (safety::mod_disabled())
				{
					return 0;
				}
				if (ReadProcessMemory(GetCurrentProcess(), target, bytes, sizeof(bytes), nullptr) && std::memcmp(bytes, rebase_bytes, sizeof(bytes)) == 0)
				{
					MH_STATUS status = MH_Initialize();
					if (status == MH_OK || status == MH_ERROR_ALREADY_INITIALIZED)
					{
						status = MH_CreateHook(target, reinterpret_cast<void*>(&rebase_detour), reinterpret_cast<void**>(&original_rebase));
						if (status == MH_OK)
						{
							status = MH_EnableHook(target);
						}
					}
					log::write("first-person lights: re-base hook at %p: %s", target, MH_StatusToString(status));
					return 0;
				}
				Sleep(200);
			}
			log::write("first-person lights: the re-base function never matched build 179547; not hooked");
			return 0;
		}
#endif

		void append_rows(std::string& text, const char* name, const float (&m)[16])
		{
			char line[256];
			std::snprintf(line, sizeof(line), "\n%s forward=(%.3f %.3f %.3f) position=(%.3f %.3f %.3f)", name, m[8], m[9], m[10], m[12], m[13], m[14]);
			text += line;
		}
	}

	bool install_first_person_light_hook()
	{
#if defined(_M_X64)
		if (HANDLE thread = CreateThread(nullptr, 0, &wait_and_hook, nullptr, 0, nullptr))
		{
			CloseHandle(thread);
			return true;
		}
		return false;
#else
		log::write("first-person lights: BF4 x64 only; not installed");
		return false;
#endif
	}

	void set_light_at_gun(bool on)
	{
		enabled = on;
	}

	std::string first_person_light_report(std::uintptr_t light)
	{
		float to[16]{}, from[16]{};
		bool known = false;
		{
			const std::lock_guard guard(cameras_lock);
			std::memcpy(to, last_to, sizeof(to));
			std::memcpy(from, last_from, sizeof(from));
			known = cameras_known;
		}
		char head[256];
		std::snprintf(head, sizeof(head), "light_at_gun=%d calls=%llu cameras_differed=%llu rebased_from_view=%llu", enabled.load() ? 1 : 0,
			static_cast<unsigned long long>(calls.load()), static_cast<unsigned long long>(differed.load()), static_cast<unsigned long long>(rebased.load()));
		std::string text = head;
		if (!known)
		{
			return text + "\nno cameras yet";
		}
		append_rows(text, "view (to)", to);
		append_rows(text, "game camera (from)", from);
		if (!light)
		{
			return text;
		}
		float matrix[16]{};
		std::uint8_t flag = 0;
		if (!ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<LPCVOID>(light + 0x50), matrix, sizeof(matrix), nullptr)
			|| !ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<LPCVOID>(light + 0xDA), &flag, 1, nullptr))
		{
			return text + "\nerror: cannot read the light";
		}
		matrix[3] = matrix[7] = matrix[11] = 0.0f;
		matrix[15] = 1.0f;
		float game[16], ours[16];
		light_rebase::rebase(matrix, from, to, game);
		light_rebase::rebase(matrix, to, to, ours);
		char line[256];
		std::snprintf(line, sizeof(line), "\nfirst_person_flag=%u", flag);
		text += line;
		append_rows(text, "light entity", matrix);
		append_rows(text, "drawn by the game", game);
		append_rows(text, "drawn with light_at_gun", ours);
		std::snprintf(line, sizeof(line), "\ngame: %.2f deg %.3f m off the entity; light_at_gun: %.2f deg %.3f m", light_rebase::forward_angle_degrees(matrix, game),
			light_rebase::position_difference(matrix, game), light_rebase::forward_angle_degrees(matrix, ours), light_rebase::position_difference(matrix, ours));
		text += line;
		return text;
	}
}
