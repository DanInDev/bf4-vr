#include "stereo/first_person_lights.hpp"

#include <windows.h>

#include <atomic>
#include <cstdio>
#include <cstring>
#include <map>
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
		float last_to[16]{};     // param_4: the view the lights are drawn in
		float last_from[16]{};   // param_5 as the game gave it: its camera
		float last_passed[16]{}; // the "from" the game was called with (the view itself while light_at_gun is on)
		bool cameras_known{};

		// A census of the renderer's light list (the lights the re-base walks), taken in the detour on request.
		struct census_entry
		{
			std::uintptr_t light;
			std::uintptr_t vtable;
			std::uint8_t first_person; // +0xDA: re-based between the cameras
			float position[3];
		};
		constexpr int census_capacity = 64;
		std::atomic<bool> census_requested{};
		std::atomic<bool> census_ready{};
		census_entry census[census_capacity]{};
		int census_count{}; // entries stored
		int census_total{}; // lights walked

#if defined(_M_X64)
		// FUN_140CC9C50 (re/symbols/bf4-179547.md "First-person light re-base"): the prologue up to its __chkstk call.
		constexpr std::uintptr_t rebase_address = 0x140CC9C50;
		constexpr std::uint8_t rebase_bytes[] = { 0x40, 0x55, 0x53, 0x56, 0x57, 0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57, 0x48, 0x8D,
			0xAC, 0x24, 0xA8, 0xB3, 0xFF, 0xFF, 0xB8, 0x58, 0x4D, 0x00, 0x00 };
		using rebase_t = void(__fastcall*)(void* lights, void* renderer, void* view, float* to, float* from);
		rebase_t original_rebase{};

		// The successor in the light set's tree, as the game's own walk does it (0x140623390): node +0 right child,
		// +8 left child, +0x10 parent with the colour in bit 0.
		std::uintptr_t next_node(std::uintptr_t node)
		{
			const auto at = [](std::uintptr_t address) { return *reinterpret_cast<const std::uintptr_t*>(address); };
			if (std::uintptr_t right = at(node))
			{
				while (const std::uintptr_t left = at(right + 8))
				{
					right = left;
				}
				return right;
			}
			std::uintptr_t current = node;
			std::uintptr_t parent = at(node + 0x10) & ~std::uintptr_t{ 1 };
			while (current == at(parent))
			{
				current = parent;
				parent = at(parent + 0x10) & ~std::uintptr_t{ 1 };
			}
			return at(current) != parent ? parent : current;
		}

		// On the render thread, right before the game walks the same set: lights + 0x30 is the end, lights + 0x38 the
		// first node, node + 0x18 the light (FUN_140CC9C50's first-person loop).
		void take_census(std::uintptr_t lights)
		{
			const std::uintptr_t end = lights + 0x30;
			std::uintptr_t node = *reinterpret_cast<const std::uintptr_t*>(lights + 0x38);
			int count = 0, total = 0;
			for (; node && node != end && total < 4096; node = next_node(node), ++total)
			{
				const std::uintptr_t light = *reinterpret_cast<const std::uintptr_t*>(node + 0x18);
				if (!light || count >= census_capacity)
				{
					continue;
				}
				census_entry& entry = census[count++];
				entry.light = light;
				entry.vtable = *reinterpret_cast<const std::uintptr_t*>(light);
				entry.first_person = *reinterpret_cast<const std::uint8_t*>(light + 0xDA);
				std::memcpy(entry.position, reinterpret_cast<const void*>(light + 0x80), sizeof(entry.position));
			}
			census_count = count;
			census_total = total;
			census_ready = true;
		}

		void __fastcall rebase_detour(void* lights, void* renderer, void* view, float* to, float* from)
		{
			++calls;
			if (lights && census_requested.exchange(false))
			{
				take_census(reinterpret_cast<std::uintptr_t>(lights));
			}
			float* passed = from;
			if (to && from)
			{
				if (std::memcmp(to, from, sizeof(float) * 16) != 0)
				{
					++differed;
				}
				if (enabled.load(std::memory_order_relaxed) && !safety::mod_disabled())
				{
					++rebased;
					passed = to;
				}
				const std::lock_guard guard(cameras_lock);
				std::memcpy(last_to, to, sizeof(last_to));
				std::memcpy(last_from, from, sizeof(last_from));
				std::memcpy(last_passed, passed, sizeof(last_passed));
				cameras_known = true;
			}
			original_rebase(lights, renderer, view, to, passed);
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

		std::string census_report()
		{
			census_ready = false;
			census_requested = true;
			for (int wait = 0; wait < 100 && !census_ready.load(); ++wait)
			{
				Sleep(10);
			}
			if (!census_ready.load())
			{
				census_requested = false;
				return "census: the re-base did not run within 1 s";
			}
			std::map<std::uintptr_t, int> by_vtable, flagged_by_vtable;
			char line[256];
			std::string list;
			for (int i = 0; i < census_count; ++i)
			{
				const census_entry& entry = census[i];
				++by_vtable[entry.vtable];
				if (entry.first_person)
				{
					++flagged_by_vtable[entry.vtable];
					std::snprintf(line, sizeof(line), "\n  first-person light %llx vtable %llx at (%.3f %.3f %.3f)", static_cast<unsigned long long>(entry.light),
						static_cast<unsigned long long>(entry.vtable), entry.position[0], entry.position[1], entry.position[2]);
					list += line;
				}
			}
			std::snprintf(line, sizeof(line), "census: %d lights in the set (%d read)", census_total, census_count);
			std::string text = line;
			for (const auto& [vtable, count] : by_vtable)
			{
				std::snprintf(line, sizeof(line), "\n  vtable %llx: %d, first-person %d", static_cast<unsigned long long>(vtable), count, flagged_by_vtable[vtable]);
				text += line;
			}
			return text + list;
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

	std::string first_person_light_report(std::uintptr_t light, bool census_wanted)
	{
		if (census_wanted)
		{
			return census_report();
		}
		float to[16]{}, from[16]{}, passed[16]{};
		bool known = false;
		{
			const std::lock_guard guard(cameras_lock);
			std::memcpy(to, last_to, sizeof(to));
			std::memcpy(from, last_from, sizeof(from));
			std::memcpy(passed, last_passed, sizeof(passed));
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
		append_rows(text, "passed as from", passed);
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
		// What the renderer drew last frame (with the "from" the game was actually called with), and what the game's own
		// camera would have given.
		float drawn[16], unhooked[16];
		light_rebase::rebase(matrix, passed, to, drawn);
		light_rebase::rebase(matrix, from, to, unhooked);
		char line[256];
		std::snprintf(line, sizeof(line), "\nfirst_person_flag=%u", flag);
		text += line;
		append_rows(text, "light entity", matrix);
		append_rows(text, "drawn", drawn);
		append_rows(text, "from the game's camera", unhooked);
		std::snprintf(line, sizeof(line), "\ndrawn: %.2f deg %.3f m off the entity; from the game's camera: %.2f deg %.3f m",
			light_rebase::forward_angle_degrees(matrix, drawn), light_rebase::position_difference(matrix, drawn),
			light_rebase::forward_angle_degrees(matrix, unhooked), light_rebase::position_difference(matrix, unhooked));
		text += line;
		return text;
	}
}
