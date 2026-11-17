#include "weapon/game_reload_sounds.hpp"

#include <windows.h>

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "common/log.hpp"

namespace bfvr::weapon
{
	int cues_for(reload_event event, sound_cue (&cues)[2])
	{
		switch (event)
		{
		case reload_event::magazine_out:
		case reload_event::flicked_out: // the AK's old magazine knocked out (#47); the new one's seat follows as magazine_in
		case reload_event::magazine_palmed:
			cues[0] = { reload_sound::magazine_out, 0 };
			return 1;
		case reload_event::magazine_in:
			cues[0] = { reload_sound::magazine_in, 0 };
			cues[1] = { reload_sound::magazine_seat, 350 };
			return 2;
		case reload_event::chambered:
		case reload_event::racked_empty:
		case reload_event::racked_locked:
		case reload_event::round_ejected:
			cues[0] = { reload_sound::bolt, 0 };
			return 1;
		case reload_event::magazine_stowed:
		case reload_event::magazine_taken: // the synthesised cloth at the pouch did not sound right (headset 2026-11-04)
			cues[0] = { reload_sound::settle, 0 };
			return 1;
		default:
			return 0;
		}
	}

#if defined(_M_X64)
	namespace
	{
		// BF4 179547 (re/notes/sound.md): SoundEntity vtable, its event handler (slot 5) with the
		// event id at +8, SoundEntityData at entity +0x28, its Sound at +0x18, asset name at +0x10.
		constexpr std::uintptr_t sound_entity_vtable = 0x141CE88C0;
		constexpr std::uintptr_t sound_entity_event = 0x1407628D0;
		constexpr int event_slot = 5;
		constexpr std::uint8_t event_prologue[] = { 0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x6C, 0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x18 };

		std::atomic<bool> enabled{ true };
		std::mutex lock;
		std::string weapon;          // the gun the entity belongs to
		std::uintptr_t entity{};     // its reload SoundEntity, 0 while unknown
		std::uintptr_t entity_data{};
		bool searching{};
		ULONGLONG last_search{};
		constexpr ULONGLONG retry_ms = 5000; // a gun's entity may appear after it is picked up
		// A gun given by the own crate (#80) that the level never spawned may have no reload entity at all (AK 5C,
		// 2026-11-16: a full heap search every 7.5 s for as long as it was held). After this many misses the gun
		// keeps the synthesised cues until the gun changes.
		constexpr int max_misses = 3;
		int misses{};
		std::uint64_t lookups{};
		std::uint64_t sent{};
		std::uint64_t refused{};
		struct pending
		{
			std::uint32_t event;
			ULONGLONG due;
		};
		std::vector<pending> queue;
		char report[200];

		template <typename T>
		bool read(std::uintptr_t address, T& value)
		{
			SIZE_T done = 0;
			return address && ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<LPCVOID>(address), &value, sizeof(value), &done) && done == sizeof(value);
		}

		bool asset_name(std::uintptr_t entity_address, std::uintptr_t& data, char (&name)[128])
		{
			std::uintptr_t sound{}, text{};
			if (!read(entity_address + 0x28, data) || !read(data + 0x18, sound) || !read(sound + 0x10, text) || !read(text, name))
			{
				return false;
			}
			name[sizeof(name) - 1] = 0;
			return true;
		}

		// "U_AK12" -> ".../AK12/Weapon_..._Reload_SPC", never the AI variants.
		bool belongs_to(const char* name, const std::string& key)
		{
			return std::strstr(name, "_Reload_SPC") && !std::strstr(name, "/AI/") && std::strstr(name, ("/" + key + "/").c_str());
		}

		// Background thread: one pass over committed private read/write memory for SoundEntity
		// objects, keeping the first whose sound is this gun's reload patch.
		void search(std::string wanted)
		{
			const std::string key = wanted.rfind("U_", 0) == 0 ? wanted.substr(2) : wanted;
			std::uintptr_t found{}, found_data{};
			std::vector<std::uintptr_t> chunk(1 << 17);
			MEMORY_BASIC_INFORMATION region{};
			for (std::uintptr_t at = 0x10000; !found && VirtualQuery(reinterpret_cast<LPCVOID>(at), &region, sizeof(region)) == sizeof(region);
				at = reinterpret_cast<std::uintptr_t>(region.BaseAddress) + region.RegionSize)
			{
				if (region.State != MEM_COMMIT || region.Type != MEM_PRIVATE || !(region.Protect & (PAGE_READWRITE | PAGE_EXECUTE_READWRITE)) || (region.Protect & PAGE_GUARD))
				{
					continue;
				}
				const auto base = reinterpret_cast<std::uintptr_t>(region.BaseAddress);
				for (std::size_t offset = 0; !found && offset < region.RegionSize; offset += chunk.size() * sizeof(std::uintptr_t))
				{
					const std::size_t bytes = std::min(chunk.size() * sizeof(std::uintptr_t), region.RegionSize - offset);
					SIZE_T done = 0;
					if (!ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<LPCVOID>(base + offset), chunk.data(), bytes, &done))
					{
						break;
					}
					for (std::size_t i = 0; i < done / sizeof(std::uintptr_t); ++i)
					{
						char name[128]{};
						std::uintptr_t data{};
						if (chunk[i] == sound_entity_vtable && asset_name(base + offset + i * sizeof(std::uintptr_t), data, name) && belongs_to(name, key))
						{
							found = base + offset + i * sizeof(std::uintptr_t);
							found_data = data;
							log::write("reload sounds: %s -> SoundEntity %p (%s)", wanted.c_str(), reinterpret_cast<void*>(found), name);
							break;
						}
					}
				}
			}
			const std::lock_guard guard(lock);
			searching = false;
			if (weapon == wanted)
			{
				entity = found;
				entity_data = found_data;
			}
			if (!found && weapon == wanted && ++misses == max_misses)
			{
				log::write("reload sounds: no reload SoundEntity for %s after %d searches; its synthesised cues play", wanted.c_str(), max_misses);
			}
			else if (!found)
			{
				log::write("reload sounds: no reload SoundEntity for %s", wanted.c_str());
			}
		}

		bool handler_verified()
		{
			static const bool verified = []
			{
				std::uintptr_t slot{};
				std::uint8_t bytes[sizeof(event_prologue)]{};
				const bool ok = read(sound_entity_vtable + event_slot * sizeof(std::uintptr_t), slot) && slot == sound_entity_event
					&& read(sound_entity_event, bytes) && std::memcmp(bytes, event_prologue, sizeof(bytes)) == 0;
				log::write("reload sounds: SoundEntity event handler %s", ok ? "verified" : "does not match build 179547; sounds off");
				return ok;
			}();
			return verified;
		}

		DWORD send(std::uintptr_t target, std::uint32_t event)
		{
			alignas(16) std::uint8_t entity_event[0x40]{};
			std::memcpy(entity_event + 8, &event, sizeof(event));
			__try
			{
				reinterpret_cast<void(__fastcall*)(void*, void*)>(sound_entity_event)(reinterpret_cast<void*>(target), entity_event);
				return 0;
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				return GetExceptionCode();
			}
		}
	}

	void set_game_reload_sounds(bool on)
	{
		enabled = on;
	}

	void game_reload_sounds_weapon(const char* weapon_name)
	{
		const std::lock_guard guard(lock);
		if (!enabled.load(std::memory_order_relaxed))
		{
			return;
		}
		const ULONGLONG now = GetTickCount64();
		if (weapon != weapon_name)
		{
			weapon = weapon_name;
			entity = entity_data = 0;
			queue.clear();
			last_search = 0;
			misses = 0;
		}
		if (!entity && !searching && misses < max_misses && now - last_search >= retry_ms && handler_verified())
		{
			searching = true;
			last_search = now;
			++lookups;
			std::thread(search, weapon).detach();
		}
	}

	bool play_game_reload_sound(reload_event event)
	{
		sound_cue cues[2]{};
		const int count = cues_for(event, cues);
		const ULONGLONG now = GetTickCount64();
		const std::lock_guard guard(lock);
		if (!enabled.load(std::memory_order_relaxed) || !entity || count == 0)
		{
			return false;
		}
		for (int i = 0; i < count; ++i)
		{
			queue.push_back({ cues[i].event, now + cues[i].delay_ms });
		}
		return true;
	}

	void game_reload_sounds_on_game_thread()
	{
		const std::lock_guard guard(lock);
		if (queue.empty())
		{
			return;
		}
		const ULONGLONG now = GetTickCount64();
		for (auto it = queue.begin(); it != queue.end();)
		{
			if (it->due > now)
			{
				++it;
				continue;
			}
			std::uintptr_t vtable{}, data{};
			if (entity && read(entity, vtable) && vtable == sound_entity_vtable && read(entity + 0x28, data) && data == entity_data)
			{
				if (send(entity, it->event) == 0)
				{
					++sent;
				}
				else
				{
					++refused;
					entity = 0; // gone or changed: look again on the next gun change
				}
			}
			else
			{
				++refused;
			}
			it = queue.erase(it);
		}
	}

	const char* game_reload_sounds_report()
	{
		const std::lock_guard guard(lock);
		std::snprintf(report, sizeof(report), "game_sounds=%d weapon=%s entity=%llx searching=%d lookups=%llu sent=%llu refused=%llu",
			enabled.load() ? 1 : 0, weapon.c_str(), static_cast<unsigned long long>(entity), searching ? 1 : 0, static_cast<unsigned long long>(lookups),
			static_cast<unsigned long long>(sent), static_cast<unsigned long long>(refused));
		return report;
	}
#else
	void set_game_reload_sounds(bool) {}
	void game_reload_sounds_weapon(const char*) {}
	bool play_game_reload_sound(reload_event) { return false; }
	void game_reload_sounds_on_game_thread() {}
	const char* game_reload_sounds_report() { return "game_sounds=0 (x64 only)"; }
#endif
}
