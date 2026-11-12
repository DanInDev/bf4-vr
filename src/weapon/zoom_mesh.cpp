#include "weapon/zoom_mesh.hpp"

#include <windows.h>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <mutex>
#include <vector>

#include "safety/mp_guard.hpp"

namespace bfvr::weapon
{
	namespace
	{
		std::atomic<bool> enabled{ true };
		std::atomic<bool> at_eye{};
		std::atomic<float> forced{ -1.0f }; // research: >= 0 holds the factor there

#if defined(_M_X64)
		// Chain as weapon/hip_accuracy; WeaponStates and the factor from the type registry, read and
		// written live on the SCAR-H (re/symbols/bf4-179547.md).
		constexpr std::uintptr_t local_soldier_global = static_cast<std::uintptr_t>(0x14230f260ull);
		constexpr std::uintptr_t soldier_weapons = 0x570;
		constexpr std::uintptr_t weapons_begin = 0x890;
		constexpr std::uintptr_t weapons_end = 0x898;
		constexpr std::uintptr_t weapon_data = 0x30;   // SoldierWeaponData
		constexpr std::uintptr_t data_states = 0x88;   // Array<WeaponStateData>: first element, count at -4
		constexpr std::uintptr_t state_size = 0x88;
		constexpr std::uintptr_t state_factor = 0x48;  // ZoomMeshTransitionFactor
		constexpr int max_slots = 16;
		constexpr std::uint32_t max_states = 8;

		struct patched_state
		{
			std::uintptr_t address{}; // of the factor
			float original{};
			float written{};
		};
		std::mutex lock_;
		std::vector<patched_state> patched;
		unsigned long long writes{};

		template <typename T>
		bool read(std::uintptr_t address, T& value)
		{
			SIZE_T done = 0;
			return address && ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<LPCVOID>(address), &value, sizeof(value), &done)
				&& done == sizeof(value);
		}

		template <typename T>
		bool write(std::uintptr_t address, const T& value)
		{
			SIZE_T done = 0;
			return address && WriteProcessMemory(GetCurrentProcess(), reinterpret_cast<LPVOID>(address), &value, sizeof(value), &done)
				&& done == sizeof(value);
		}

		void restore_all()
		{
			for (const patched_state& p : patched)
			{
				float current{};
				// Only where our value still stands: a freed asset's memory may hold something else now.
				if (read(p.address, current) && current == p.written)
				{
					write(p.address, p.original);
				}
			}
			patched.clear();
		}

		void patch(std::uintptr_t address, float value, bool keep_original)
		{
			float current{};
			if (!read(address, current) || !(current >= 0.0f && current <= 1.0f))
			{
				return; // not a transition factor: leave it alone
			}
			auto found = std::find_if(patched.begin(), patched.end(), [&](const patched_state& p) { return p.address == address; });
			if (found == patched.end() || current != found->written)
			{
				if (found == patched.end())
				{
					if (patched.size() >= 64)
					{
						return;
					}
					patched.push_back({ address });
					found = patched.end() - 1;
				}
				found->original = current;
			}
			const float wanted = keep_original ? zoom_mesh_factor(found->original, true) : value;
			if (current != wanted)
			{
				write(address, wanted);
				++writes;
			}
			found->written = wanted;
		}
#endif
	}

	void set_zoom_mesh(bool on)
	{
		enabled = on;
	}

	void set_sight_at_eye(bool eye)
	{
		at_eye = eye;
	}

	void update_zoom_mesh()
	{
#if defined(_M_X64)
		const std::lock_guard guard(lock_);
		const float hold = forced.load(std::memory_order_relaxed);
		const bool wanted = !safety::mod_disabled() && (hold >= 0.0f || (enabled.load(std::memory_order_relaxed) && at_eye.load(std::memory_order_relaxed)));
		if (!wanted)
		{
			if (!patched.empty())
			{
				restore_all();
			}
			return;
		}
		std::uintptr_t soldier{}, weapons{}, begin{}, end{};
		if (!read(local_soldier_global, soldier) || !read(soldier + soldier_weapons, weapons) || !read(weapons + weapons_begin, begin)
			|| !read(weapons + weapons_end, end) || !begin || end <= begin)
		{
			return;
		}
		const int slots = static_cast<int>(std::min<std::uintptr_t>((end - begin) / 8, max_slots));
		for (int slot = 0; slot < slots; ++slot)
		{
			std::uintptr_t client{}, data{}, states{};
			std::uint32_t count{};
			if (!read(begin + slot * 8, client) || !read(client + weapon_data, data) || !read(data + data_states, states) || !states
				|| !read(states - 4, count) || count == 0 || count > max_states)
			{
				continue;
			}
			for (std::uint32_t i = 0; i < count; ++i)
			{
				patch(states + state_size * i + state_factor, hold, hold < 0.0f);
			}
		}
#endif
	}

	std::string zoom_mesh_command(const std::string& argument)
	{
		if (argument == "off")
		{
			forced = -1.0f;
		}
		else if (!argument.empty())
		{
			forced = std::clamp(static_cast<float>(std::atof(argument.c_str())), 0.0f, 1.0f);
		}
#if defined(_M_X64)
		const std::lock_guard guard(lock_);
		char text[256];
		std::snprintf(text, sizeof(text), "zoom_mesh=%d at_eye=%d forced=%.2f patched=%zu writes=%llu", enabled.load() ? 1 : 0, at_eye.load() ? 1 : 0,
			forced.load(), patched.size(), writes);
		std::string out = text;
		for (const patched_state& p : patched)
		{
			std::snprintf(text, sizeof(text), "\n%016llX factor %.2f (game %.2f)", static_cast<unsigned long long>(p.address), p.written, p.original);
			out += text;
		}
		return out;
#else
		return "zoom_mesh: BF4 (x64) only";
#endif
	}
}
