#include "weapon/own_crate.hpp"

#include <windows.h>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <thread>
#include <vector>

#include "common/log.hpp"
#include "common/settings_store.hpp"
#include "weapon/crate_guns.hpp"
#include "weapon/server_soldier.hpp"

namespace bfvr::weapon
{
#if defined(_M_X64)
	namespace
	{
		// BF4 179547 (re/notes/weapon.md "Own crate", re/symbols/bf4-179547.md).
		constexpr std::uintptr_t server_soldier_vtable = 0x141E67BA0;
		constexpr int customize_slot = 100; // ServerSoldierEntity: apply a CustomizeSoldierData ({soldier, player, data, 0})
		constexpr std::uintptr_t customize_function = 0x140FC0F30;
		constexpr std::uint8_t customize_prologue[] = { 0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0xDA, 0xE8 };
		constexpr std::uintptr_t unlock_asset_vtable = 0x141E76F40;       // SoldierWeaponUnlockAsset
		constexpr std::uintptr_t customize_data_vtable = 0x141E77120;     // CustomizeSoldierData (live instances)
		constexpr std::size_t customize_data_size = 0x58;
		constexpr std::uintptr_t soldier_player = 0x240;                  // ServerSoldierEntity -> its player

		template <typename T>
		bool read(std::uintptr_t address, T& value)
		{
			SIZE_T done = 0;
			return address && ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<LPCVOID>(address), &value, sizeof(value), &done) && done == sizeof(value);
		}

		bool read_text(std::uintptr_t pointer_at, char (&text)[96])
		{
			std::uintptr_t text_address{};
			if (!read(pointer_at, text_address) || !read(text_address, text))
			{
				return false;
			}
			text[sizeof(text) - 1] = 0;
			return true;
		}

		// One pass over committed private read/write memory for objects with this vtable; the callback says stop.
		template <typename F>
		void scan_heap(std::uintptr_t vtable, F&& keep_going)
		{
			std::vector<std::uintptr_t> chunk(1 << 17);
			MEMORY_BASIC_INFORMATION region{};
			for (std::uintptr_t at = 0x10000; VirtualQuery(reinterpret_cast<LPCVOID>(at), &region, sizeof(region)) == sizeof(region);
				at = reinterpret_cast<std::uintptr_t>(region.BaseAddress) + region.RegionSize)
			{
				if (region.State != MEM_COMMIT || region.Type != MEM_PRIVATE || !(region.Protect & (PAGE_READWRITE | PAGE_EXECUTE_READWRITE)) || (region.Protect & PAGE_GUARD))
				{
					continue;
				}
				const auto base = reinterpret_cast<std::uintptr_t>(region.BaseAddress);
				for (std::size_t offset = 0; offset < region.RegionSize; offset += chunk.size() * sizeof(std::uintptr_t))
				{
					const std::size_t bytes = (std::min)(chunk.size() * sizeof(std::uintptr_t), region.RegionSize - offset);
					SIZE_T done = 0;
					if (!ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<LPCVOID>(base + offset), chunk.data(), bytes, &done))
					{
						break;
					}
					for (std::size_t i = 0; i < done / sizeof(std::uintptr_t); ++i)
					{
						if (chunk[i] == vtable && !keep_going(base + offset + i * sizeof(std::uintptr_t)))
						{
							return;
						}
					}
				}
			}
		}

		std::uintptr_t find_unlock_asset(const std::string& debug_id)
		{
			std::uintptr_t found{};
			scan_heap(unlock_asset_vtable, [&](std::uintptr_t asset) {
				char id[96]{};
				if (read_text(asset + 0x20, id) && debug_id == id)
				{
					found = asset;
					return false;
				}
				return true;
			});
			return found;
		}

		// A Frostbite array at `at` in our block: the pointer is to element 0, the count sits in the 4 bytes before it.
		std::uintptr_t make_array(std::uint8_t* at, const void* elements, std::uint32_t count, std::size_t element_size)
		{
			std::memcpy(at - 4, &count, sizeof(count));
			if (count)
			{
				std::memcpy(at, elements, count * element_size);
			}
			return reinterpret_cast<std::uintptr_t>(at);
		}

		struct give_call
		{
			std::uintptr_t asset{};
			std::uint32_t slot{};
			std::uintptr_t data_template{};
			bool done{};
			std::string result;
			bool owned{}; // queued by the panel: the game thread deletes it
		};
		std::mutex lock;
		std::vector<give_call*> queue;
		std::atomic<int> pending{};
		crate::change_watch primary_watch, secondary_watch;

		bool verified()
		{
			std::uintptr_t slot_function{};
			std::uint8_t bytes[sizeof(customize_prologue)]{};
			return read(server_soldier_vtable + customize_slot * sizeof(std::uintptr_t), slot_function) && slot_function == customize_function
				&& read(customize_function, bytes) && std::memcmp(bytes, customize_prologue, sizeof(bytes)) == 0;
		}

		DWORD call_customize(std::uintptr_t soldier, void* arguments)
		{
			__try
			{
				reinterpret_cast<void(__fastcall*)(void*, void*)>(customize_function)(reinterpret_cast<void*>(soldier), arguments);
				return 0;
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				return GetExceptionCode();
			}
		}

		std::string give(const give_call& call)
		{
			const std::uintptr_t soldier = player_server_soldier();
			std::uintptr_t vtable{}, player{};
			if (!soldier || !read(soldier, vtable) || vtable != server_soldier_vtable || !read(soldier + soldier_player, player) || !player)
			{
				return "error: no player soldier";
			}
			// The data: a copy of a live CustomizeSoldierData (its header as the engine made it), with our gun.
			std::uint8_t data_bytes[customize_data_size]{};
			if (!read(call.data_template, data_bytes))
			{
				return "error: template unreadable";
			}
			// One 4 KB page per pick, never freed on purpose: the engine may keep pointers to the data (the soldier's
			// kit for a respawn), and a pick is a rare, deliberate act. Layout: empty array +0x10, weapons +0x40,
			// data +0x100.
			auto* block = static_cast<std::uint8_t*>(VirtualAlloc(nullptr, 0x1000, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
			if (!block)
			{
				return "error: allocation";
			}
			const std::uintptr_t empty = make_array(block + 0x10, nullptr, 0, 8);
			struct unlock_weapon_and_slot
			{
				std::uintptr_t weapon;
				std::uint32_t slot;
				std::uint32_t pad;
				std::uintptr_t customization_unlocks;
				std::uintptr_t unlocks;
			} entry{ call.asset, call.slot, 0, empty, empty };
			static_assert(sizeof(unlock_weapon_and_slot) == 32);
			const std::uintptr_t weapons = make_array(block + 0x40, &entry, 1, sizeof(entry));
			std::uint8_t* const data = block + 0x100;
			std::memcpy(data, data_bytes, customize_data_size);
			const std::uint32_t header = 0xB1000000u | 0x1000u; // the asset flags with a count no release brings to 0
			std::memcpy(data + 8, &header, 4);
			std::memcpy(data + 0x18, &empty, 8);   // VisualGroups
			std::memcpy(data + 0x20, &empty, 8);   // Unlocks
			const float no_override = -1.0f;
			std::memcpy(data + 0x28, &no_override, 4); // OverrideMaxHealth
			std::memcpy(data + 0x2C, &no_override, 4); // OverrideCriticalHealthThreshold
			data[0x30] = 0;                        // RestoreToOriginalVisualState
			data[0x31] = 0;                        // ClearVisualState
			std::memcpy(data + 0x38, &weapons, 8); // Weapons
			std::memcpy(data + 0x40, &call.slot, 4); // ActiveSlot: the new gun in hand
			std::memset(data + 0x48, 0, 8);        // DeathPickup
			data[0x50] = 0;                        // KeepCurrentActiveSlot
			data[0x51] = 0;                        // RemoveAllExistingWeapons
			data[0x52] = 0;                        // DisableDeathPickup
			struct
			{
				std::uintptr_t soldier;
				std::uintptr_t player;
				std::uintptr_t data;
				std::uint64_t flag;
			} arguments{ soldier, player, reinterpret_cast<std::uintptr_t>(data), 0 };
			const DWORD code = call_customize(soldier, &arguments);
			char text[200];
			if (code)
			{
				std::snprintf(text, sizeof(text), "error: exception %08lx in the customization", code);
			}
			else
			{
				std::snprintf(text, sizeof(text), "ok: asset %llx to slot %u on soldier %llx (player %llx), data %p", static_cast<unsigned long long>(call.asset),
					call.slot, static_cast<unsigned long long>(soldier), static_cast<unsigned long long>(player), static_cast<void*>(data));
			}
			log::write("own crate: %s", text);
			return text;
		}
	}

	namespace
	{
		// The heap scans (any thread): the gun's asset and a CustomizeSoldierData to copy. Empty when ready.
		std::string prepare(give_call& call, const std::string& id)
		{
			if (!verified())
			{
				return "error: ServerSoldierEntity slot 100 does not match build 179547";
			}
			call.asset = find_unlock_asset(id);
			if (!call.asset)
			{
				return "error: no SoldierWeaponUnlockAsset with id " + id;
			}
			// A level asset (header 0xB1000003/4), not the class's prototype (0x01000001, 2026-11-16): the copy is
			// reference counted at +0x08 and the prototype's count let a release free our copy mid-call.
			scan_heap(customize_data_vtable, [&](std::uintptr_t data) {
				std::uint32_t header{};
				if (read(data + 8, header) && (header >> 24) == 0xB1)
				{
					call.data_template = data;
					return false;
				}
				return true;
			});
			return call.data_template ? std::string() : "error: no CustomizeSoldierData in memory to copy";
		}

		void give_later(std::string id, std::uint32_t slot)
		{
			if (!player_server_soldier())
			{
				log::write("own crate: %s for slot %u skipped (no player soldier)", id.c_str(), slot);
				return;
			}
			++pending;
			std::thread([id, slot] {
				auto* call = new give_call;
				call->slot = slot;
				call->owned = true;
				const std::string error = prepare(*call, id);
				if (!error.empty())
				{
					log::write("own crate: %s for slot %u: %s", id.c_str(), slot, error.c_str());
					delete call;
					--pending;
					return;
				}
				log::write("own crate: %s for slot %u queued", id.c_str(), slot);
				const std::lock_guard guard(lock);
				queue.push_back(call);
			}).detach();
		}
	}

	void own_crate_settings(std::string_view primary, std::string_view secondary)
	{
		const std::uint64_t now = GetTickCount64();
		if (const std::string gun = primary_watch.update(primary, now, pending.load() > 0); !gun.empty())
		{
			give_later(gun, 0);
		}
		if (const std::string gun = secondary_watch.update(secondary, now, pending.load() > 0); !gun.empty())
		{
			give_later(gun, 1);
		}
	}

	std::string give_gun_pipe_command(const std::string& argument)
	{
		char id[64]{};
		unsigned slot = 0;
		if (sscanf_s(argument.c_str(), "%63s %u", id, static_cast<unsigned>(sizeof(id)), &slot) < 1 || slot > 9)
		{
			return "error: usage: givegun <debug id, e.g. U_M416> [slot 0..9]";
		}
		give_call call;
		call.slot = slot;
		if (const std::string error = prepare(call, id); !error.empty())
		{
			return error;
		}
		{
			const std::lock_guard guard(lock);
			queue.push_back(&call);
		}
		for (int waited = 0; waited < 400; ++waited)
		{
			{
				const std::lock_guard guard(lock);
				if (call.done)
				{
					return call.result;
				}
			}
			Sleep(5);
		}
		const std::lock_guard guard(lock);
		std::erase(queue, &call);
		return call.done ? call.result : "error: the game thread did not run it within 2 s";
	}

	void own_crate_on_game_thread()
	{
		// Every frame (settings are applied only when they change, and a pick must settle first).
		own_crate_settings(settings::global().text("crate_primary", "keep"), settings::global().text("crate_secondary", "keep"));
		const std::lock_guard guard(lock);
		for (give_call* call : queue)
		{
			call->result = give(*call);
			call->done = true;
			if (call->owned)
			{
				delete call;
				--pending;
			}
		}
		queue.clear();
	}
#else
	void own_crate_settings(std::string_view, std::string_view) {}
	std::string give_gun_pipe_command(const std::string&) { return "error: x64 only"; }
	void own_crate_on_game_thread() {}
#endif
}
