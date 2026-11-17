#include "weapon/game_reload_sounds.hpp"

#include <windows.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "common/log.hpp"

namespace bfvr::weapon
{
	namespace
	{
		std::string lower(const char* text)
		{
			std::string out = text ? text : "";
			std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			return out;
		}
	}

	std::string sound_folder_of(const char* asset_name)
	{
		const std::string name = asset_name ? asset_name : "";
		const std::string low = lower(asset_name);
		const std::size_t at = low.find("/handheld/");
		if (at == std::string::npos)
		{
			return {};
		}
		const std::size_t begin = at + 10;
		const std::size_t end = name.find('/', begin);
		return end == std::string::npos ? std::string{} : name.substr(begin, end - begin);
	}

	bool reload_set_belongs(const char* asset_name, const std::string& folder)
	{
		const std::string low = lower(asset_name);
		return !folder.empty() && low.find("_reload_spc") != std::string::npos && low.find("/ai/") == std::string::npos
			&& low.find("/" + lower(folder.c_str()) + "/") != std::string::npos;
	}

	bool patch_has_event(const std::uint32_t* events, int count, std::uint32_t event)
	{
		return std::find(events, events + count, event) != events + count;
	}

	reload_patch patch_kind_of(const std::string& patch_name)
	{
		const std::string low = lower(patch_name.c_str());
		if (low.find("shotgun_pumpaction") != std::string::npos)
		{
			return reload_patch::pump;
		}
		if (low.find("sniper_boltaction") != std::string::npos)
		{
			return reload_patch::sniper;
		}
		if (low.find("reload_lmg") != std::string::npos)
		{
			return reload_patch::lmg;
		}
		if (low.find("reload_pistol") != std::string::npos)
		{
			return reload_patch::pistol;
		}
		if (low.find("reload_5") != std::string::npos)
		{
			return reload_patch::tube5;
		}
		return reload_patch::ar;
	}

	int cues_for(reload_event event, reload_patch patch, sound_cue (&cues)[2])
	{
		// The pump and Reload_5 patches have the AR's settle event, but it makes no sound there (verified 2026-11-17): a
		// shell taken from the pouch keeps the synthesised cue.
		if ((patch == reload_patch::pump || patch == reload_patch::tube5)
			&& (event == reload_event::magazine_taken || event == reload_event::magazine_stowed))
		{
			return 0;
		}
		switch (patch)
		{
		case reload_patch::pump:
			if (event == reload_event::shell_loaded)
			{
				cues[0] = { reload_sound::pump_shell_in, 0 };
				return 1;
			}
			break;
		case reload_patch::tube5:
			if (event == reload_event::shell_loaded)
			{
				cues[0] = { reload_sound::tube5_shell_in, 0 };
				return 1;
			}
			if (event == reload_event::chambered || event == reload_event::racked_empty || event == reload_event::round_ejected)
			{
				cues[0] = { reload_sound::tube5_pump, 0 };
				return 1;
			}
			break;
		case reload_patch::lmg:
			if (event == reload_event::lid_opened)
			{
				cues[0] = { reload_sound::lmg_lid_open, 0 };
				return 1;
			}
			if (event == reload_event::lid_closed)
			{
				cues[0] = { reload_sound::lmg_lid_close, 0 };
				return 1;
			}
			if (event == reload_event::belt_laid)
			{
				cues[0] = { reload_sound::magazine_seat, 0 };
				return 1;
			}
			break;
		case reload_patch::pistol:
			if (event == reload_event::lid_opened)
			{
				cues[0] = { reload_sound::magazine_out, 0 }; // the break open and the cases thrown (0.98)
				return 1;
			}
			if (event == reload_event::lid_closed)
			{
				cues[0] = { reload_sound::pistol_close, 0 };
				return 1;
			}
			break;
		default:
			break;
		}
		return cues_for(event, cues);
	}

	std::uint32_t rear_stop_event(reload_patch patch)
	{
		// The sniper's bolt event (9FA31A86) sounds when fired alone but stayed silent as a rear stop on the Scout
		// (verified 2026-11-17): its rear stop keeps the synthesised cue.
		return patch == reload_patch::pump ? reload_sound::pump_back : 0u;
	}

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
		case reload_event::shell_loaded: // a tube gun's reload entity: its shell-in sound
			cues[0] = { reload_sound::magazine_in, 0 };
			return 1;
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
		std::string folder;          // its sound folder (the firing sound's), the reload set's folder
		std::uintptr_t entity{};     // its reload SoundEntity, 0 while unknown
		std::uintptr_t entity_data{};
		// The entity's patch's graph input events (SoundPatchConfiguration +0x28 patch, +0x28 SoundGraphData, +0x70 input
		// events, each AudioGraphParameter's NameHash at +0x14; tools/weapon_sound_probe.py): a cue it lacks is not sent.
		constexpr int max_events = 64;
		std::uint32_t patch_events[max_events]{};
		int patch_event_count{};
		std::string patch_name;
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
		std::uint64_t unplayable{}; // cues the gun's patch lacks: the synthesised one played
		struct pending
		{
			std::uint32_t event;
			ULONGLONG due;
		};
		std::vector<pending> queue;
		char report[400];

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

		// The patch behind a reload set's configuration and its graph input event hashes.
		int read_patch_events(std::uintptr_t data, std::uint32_t (&events)[max_events], std::string& name)
		{
			std::uintptr_t sound{}, patch{}, patch_text{}, graph{}, array{};
			std::uint32_t count{};
			char text[128]{};
			if (!read(data + 0x18, sound) || !read(sound + 0x28, patch) || !patch || !read(patch + 0x10, patch_text) || !read(patch_text, text)
				|| !read(patch + 0x28, graph) || !read(graph + 0x70, array) || !array || !read(array - 4, count))
			{
				return -1;
			}
			text[sizeof(text) - 1] = 0;
			name = text;
			int n = 0;
			for (std::uint32_t i = 0; i < count && n < max_events; ++i)
			{
				std::uintptr_t entry{};
				std::uint32_t hash{};
				if (read(array + i * 8, entry) && read(entry + 0x14, hash))
				{
					events[n++] = hash;
				}
			}
			return n;
		}

		// Background thread: one pass over committed private read/write memory for SoundEntity
		// objects, keeping the first whose sound is this gun's reload patch.
		void search(std::string wanted, std::string key)
		{
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
						if (chunk[i] == sound_entity_vtable && asset_name(base + offset + i * sizeof(std::uintptr_t), data, name) && reload_set_belongs(name, key))
						{
							found = base + offset + i * sizeof(std::uintptr_t);
							found_data = data;
							log::write("reload sounds: %s -> SoundEntity %p (%s)", wanted.c_str(), reinterpret_cast<void*>(found), name);
							break;
						}
					}
				}
			}
			std::uint32_t events[max_events]{};
			std::string patch;
			const int event_count = found ? read_patch_events(found_data, events, patch) : -1;
			if (found)
			{
				log::write("reload sounds: %s patch %s, %d events", wanted.c_str(), patch.c_str(), event_count);
			}
			const std::lock_guard guard(lock);
			searching = false;
			if (weapon == wanted)
			{
				entity = found;
				entity_data = found_data;
				patch_event_count = event_count; // -1: not read, every cue is sent as before
				std::copy(std::begin(events), std::end(events), patch_events);
				patch_name = patch;
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

	void game_reload_sounds_weapon(const char* weapon_name, const char* sound_folder)
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
			const std::string unlock = weapon.rfind("U_", 0) == 0 ? weapon.substr(2) : weapon;
			folder = sound_folder && *sound_folder ? std::string(sound_folder) : unlock;
			patch_event_count = -1;
			patch_name.clear();
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
			std::thread(search, weapon, folder).detach();
		}
	}

	bool play_game_rear_stop()
	{
		const std::lock_guard guard(lock);
		const std::uint32_t event = rear_stop_event(patch_kind_of(patch_name));
		if (!enabled.load(std::memory_order_relaxed) || !entity || !event || (patch_event_count >= 0 && !patch_has_event(patch_events, patch_event_count, event)))
		{
			return false;
		}
		queue.push_back({ event, GetTickCount64() });
		return true;
	}

	bool play_game_reload_sound(reload_event event)
	{
		sound_cue cues[2]{};
		reload_patch patch = reload_patch::ar;
		{
			const std::lock_guard guard(lock);
			patch = patch_kind_of(patch_name);
		}
		const int count = cues_for(event, patch, cues);
		const ULONGLONG now = GetTickCount64();
		const std::lock_guard guard(lock);
		if (!enabled.load(std::memory_order_relaxed) || !entity || count == 0)
		{
			return false;
		}
		// A cue this gun's patch does not have would play nothing (the pump shotguns have no magazine-in, Reload_5 none of
		// the AR events): the synthesised cue plays instead. Only the cues it has are sent.
		const bool known = patch_event_count >= 0;
		if (known && !patch_has_event(patch_events, patch_event_count, cues[0].event))
		{
			++unplayable;
			return false;
		}
		for (int i = 0; i < count; ++i)
		{
			if (!known || patch_has_event(patch_events, patch_event_count, cues[i].event))
			{
				queue.push_back({ cues[i].event, now + cues[i].delay_ms });
			}
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
		std::snprintf(report, sizeof(report), "game_sounds=%d weapon=%s entity=%llx searching=%d lookups=%llu sent=%llu refused=%llu folder=%s patch=%s events=%d unplayable=%llu",
			enabled.load() ? 1 : 0, weapon.c_str(), static_cast<unsigned long long>(entity), searching ? 1 : 0, static_cast<unsigned long long>(lookups),
			static_cast<unsigned long long>(sent), static_cast<unsigned long long>(refused), folder.c_str(), patch_name.empty() ? "-" : patch_name.c_str() + patch_name.rfind('/') + 1,
			patch_event_count, static_cast<unsigned long long>(unplayable));
		return report;
	}
#else
	void set_game_reload_sounds(bool) {}
	void game_reload_sounds_weapon(const char*, const char*) {}
	bool play_game_reload_sound(reload_event) { return false; }
	bool play_game_rear_stop() { return false; }
	void game_reload_sounds_on_game_thread() {}
	const char* game_reload_sounds_report() { return "game_sounds=0 (x64 only)"; }
#endif
}
