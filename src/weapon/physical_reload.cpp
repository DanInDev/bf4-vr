#include "weapon/physical_reload.hpp"

#include "common/controlled.hpp"
#include "common/settings_store.hpp"

#include <windows.h>
#include <Xinput.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <mutex>
#include <string>

#include "body/arm_pose.hpp"
#include "common/config.hpp"
#include "common/log.hpp"
#include "input/haptics.hpp"
#include "input/vr_controls.hpp"
#include "physics/ray_cast.hpp"
#include "stereo/eye_math.hpp"
#include "stereo/tracking.hpp"
#include "weapon/accessory_touch.hpp"
#include "weapon/break_open.hpp"
#include "weapon/gun_traits.hpp"
#include "weapon/physical_grenade.hpp"
#include "weapon/profiles.hpp"
#include "weapon/reload.hpp"
#include "weapon/reload_hands.hpp"
#include "weapon/server_soldier.hpp"
#include "weapon/game_reload_sounds.hpp"
#include "weapon/grab_debug.hpp"
#include "weapon/body_rig.hpp"
#include "weapon/free_hands.hpp"
#include "weapon/reload_sounds.hpp"
#include "weapon/weapon_swap.hpp"
#include "weapon/zone_book.hpp"

namespace bfvr::weapon
{
	namespace
	{
		// BF4 179547 (re/notes/weapon.md). Client: local soldier -> weapons component -> current slot.
		constexpr std::uintptr_t soldier_weapons = 0x570;
		constexpr std::uintptr_t weapons_flags = 0x28;
		constexpr std::uintptr_t weapons_index_a = 0x8d8;
		constexpr std::uintptr_t weapons_index_b = 0x8e0;
		constexpr std::uintptr_t weapons_begin = 0x890;
		constexpr std::uintptr_t weapons_end = 0x898;
		constexpr std::uintptr_t weapon_unlock = 0x4850;  // -> unlock asset, +0x20 name ("U_AK12")
		constexpr std::uintptr_t unlock_name = 0x20;
		constexpr std::uintptr_t weapon_data = 0x30;      // SoldierWeaponData
		constexpr std::uintptr_t data_firing = 0x90;      // WeaponFiringData
		constexpr std::uintptr_t data_anim_set = 0x1C8;   // SoldierWeaponData.AnimBaseSet: 1 = wabsPstl (typeinfo, #60)
		constexpr std::uintptr_t firing_primary = 0x10;   // FiringFunctionData
		constexpr std::uintptr_t firing_rumble = 0x38;    // WeaponFiringData.Rumble: low, high, duration
		constexpr std::uintptr_t firing_sway = 0x30;      // WeaponFiringData.WeaponSway (GunSwayData)
		// GunSwayData hip-fire GunSwayRecoilData (Stand/Crouch/Prone .NoZoom.Recoil) by soldier stance,
		// and FirstShotRecoilMultiplier (re/notes/weapon.md, "Recoil").
		constexpr std::uintptr_t sway_recoil[3] = { 0x10 + 0x90, 0x188 + 0x30, 0x240 + 0x30 };
		constexpr std::uintptr_t sway_first_shot = 0x444;
		constexpr std::uintptr_t soldier_stance = 0x4f0;  // u32: 0 stand, 1 crouch, 2 prone
		constexpr std::uintptr_t function_capacity = 0x220; // Ammo.MagazineCapacity (counts the chamber)
		constexpr std::uintptr_t function_sound = 0x160;    // FiringFunctionData.Sound (SoundAsset)
		// Server (single player runs it in-process): local ServerSoldierEntity -> weapons component ->
		// slot array (same slot index as the client) -> weapon firing object -> ammo.
		constexpr std::uintptr_t server_soldier_weapons = 0x7c0;
		constexpr std::uintptr_t server_weapon_slots = 0x828;
		constexpr std::uintptr_t server_firing = 0x200;
		constexpr std::uintptr_t firing_magazine = 0x1a0;  // int32 rounds in the gun, reserve at +4
		constexpr int max_slots = 16;
		// Rounds the game keeps beyond ours, so its magazine never hits 0 (its own reload). #89: for a magazine gun from the
		// frame time and the gun's rate of fire (spare_rounds_for: 2 at headset frame rates; a fast gun at 20 fps fired
		// both of 2 before the trigger block reached the game); the launcher keeps its 2.
		constexpr int spare_rounds = 2;
		int spares_now = 2;              // render thread: this frame's spares for the gun in hand
		float frame_seconds_smooth = 1.0f / 90.0f;
		float rounds_per_second = 20.0f; // the gun's rate while firing, at least 15 (1200 rpm until measured)
		ULONGLONG burst_at{};            // the last frame the gun fired
		constexpr ULONGLONG points_max_age_ms = 250;

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
			return address && WriteProcessMemory(GetCurrentProcess(), reinterpret_cast<LPVOID>(address), &value, sizeof(value), nullptr);
		}

		struct game_weapon
		{
			bool valid{};
			int slot{ -1 };
			char name[48]{};
			int capacity{};          // game MagazineCapacity
			bool launcher{};         // #64: a break-open launcher (weapon/profiles is_break_open), one round
			std::string sound_folder; // its firing sound's folder: the folder of its reload sound set (game_reload_sounds)
			std::uintptr_t ammo{};   // server weapon firing + 0x1A0
			int magazine{};
			int reserve{};
			float rumble[3]{};       // the game's firing rumble: low, high, seconds
			body::recoil_profile recoil; // the drawn gun's kick for the current stance
		};

		// The game's hip-fire recoil for the stance, relative to the AK-12's (the tuned kick; live read
		// 2026-11-03: 0.3 up, -0.12..0.17 sideways, decrease 18, first shot x2.1).
		body::recoil_profile read_recoil(std::uintptr_t firing, std::uintptr_t soldier)
		{
			static const body::game_recoil ak12{ 0.3f, -0.12f, 0.17f, 18.0f, 2.1f };
			std::uintptr_t sway{};
			std::uint32_t stance{};
			// GunSwayRecoilData: AmplitudeMax, IncPerShot, HorizontalIncPerShotMin, ...Max, HorizontalMax, DecreaseFactor
			float recoil[6]{};
			body::game_recoil gun;
			if (!read(firing + firing_sway, sway) || !sway || !read(soldier + soldier_stance, stance) || stance > 2
				|| !read(sway + sway_recoil[stance], recoil) || !read(sway + sway_first_shot, gun.first_shot))
			{
				return {};
			}
			gun.vertical = recoil[1];
			gun.horizontal_min = recoil[2];
			gun.horizontal_max = recoil[3];
			gun.decrease = recoil[5];
			return body::profile_from_game(gun, ak12);
		}

		std::atomic<bool> pistol_in_hand{}; // the last weapon read is a pistol (#63: its shorter barrel)

		game_weapon read_game_weapon()
		{
			game_weapon weapon;
			std::uintptr_t soldier{}, weapons{}, holder{}, begin{}, end{}, client{};
			std::uint32_t flags{}, index{};
			if (!(soldier = game::local_soldier()) != 0 || !read(soldier + soldier_weapons, weapons) || !read(weapons + weapons_flags, flags)
				|| !read(weapons + ((flags & 4) ? weapons_index_b : weapons_index_a), holder) || !read(holder + 0x8, index)
				|| !read(weapons + weapons_begin, begin) || !read(weapons + weapons_end, end) || !begin || end <= begin
				|| index >= (end - begin) / 8 || index >= max_slots || !read(begin + index * 8, client) || !client)
			{
				return weapon;
			}
			std::uintptr_t unlock{}, name{}, data{}, firing{}, function{};
			if (!read(client + weapon_unlock, unlock) || !read(unlock + unlock_name, name) || !read(name, weapon.name))
			{
				return weapon;
			}
			weapon.name[sizeof(weapon.name) - 1] = 0;
			if (!read(client + weapon_data, data) || !read(data + data_firing, firing) || !read(firing + firing_primary, function)
				|| !read(function + function_capacity, weapon.capacity))
			{
				return weapon;
			}
			read(firing + firing_rumble, weapon.rumble);
			{
				// FiringFunctionData.Sound (+0x160; tools/weapon_sound_probe.py), its asset name at +0x10.
				std::uintptr_t sound{}, sound_name{};
				char text[128]{};
				if (read(function + function_sound, sound) && read(sound + 0x10, sound_name) && read(sound_name, text))
				{
					text[sizeof(text) - 1] = 0;
					weapon.sound_folder = sound_folder_of(text);
				}
			}
			weapon.recoil = read_recoil(firing, soldier);
			std::int32_t anim_set = -1;
			weapon.recoil.pistol = read(data + data_anim_set, anim_set) && anim_set == 1;
			pistol_in_hand = weapon.recoil.pistol;
			std::uintptr_t server_soldier{}, server_weapons{}, server_weapon{}, server_ammo{};
			if ((server_soldier = player_server_soldier()) == 0 || !read(server_soldier + server_soldier_weapons, server_weapons)
				|| !read(server_weapons + server_weapon_slots + index * 8, server_weapon) || !server_weapon
				|| !read(server_weapon + server_firing, server_ammo) || !read(server_ammo + firing_magazine, weapon.magazine)
				|| !read(server_ammo + firing_magazine + 4, weapon.reserve))
			{
				return weapon;
			}
			// Grenades, the knife and gadgets carry -1: no magazine to reload.
			weapon.launcher = is_break_open(weapon.name);
			weapon.valid = weapon.magazine >= 0 && weapon.reserve >= 0 && (weapon.capacity > 1 || weapon.launcher) && weapon.capacity <= 500;
			weapon.slot = static_cast<int>(index);
			weapon.ammo = server_ammo + firing_magazine;
			return weapon;
		}

		std::atomic<bool> physical{};
		std::atomic<ULONGLONG> last_shot_at{}; // #65: the muzzle flash move's clock
		std::atomic<bool> discard{ true };
		std::atomic<bool> palming{ true };
		std::atomic<bool> free_hand_reload{ true }; // #38; always on since #48 (was free_hand_reload)
		std::atomic<bool> magazine_flight_on{ true }; // live setting magazine_flight (#47)
		std::atomic<bool> claims_free_hand{};       // the free gun hand's grip belongs to the reload this frame
		// For the pad filter on the XInput thread.
		std::atomic<bool> reload_all{};     // setting physical_reload_all: the AR/AK reload for every gun (the old way)
		std::atomic<bool> game_reload_gun{}; // the gun in hand is one the game reloads
		std::atomic<bool> gun_points{ true }; // setting gun_points
		std::atomic<bool> manual_cycle_on{ true }; // setting manual_cycle
		std::atomic<bool> pump_at_rest_on{ true }; // setting pump_at_rest (#94)
		std::atomic<bool> lid_lock_on{ true };     // setting lid_hand_lock (#95)
		std::atomic<bool> shells_by_hand{ true };  // setting shells_by_hand
		std::atomic<bool> belts_by_hand{ true };   // setting belts_by_hand
		std::atomic<bool> revolver_by_hand{ true }; // setting revolver_by_hand
		std::string game_reload_logged;
		std::atomic<bool> active{};
		std::atomic<bool> block_trigger{};
		std::atomic<bool> block_grip{};
		// The off hand is doing reload work (holding a magazine, racking), not only near a reload zone.
		std::atomic<bool> hand_engaged{};
		// Accessory toggle by touch (any gun, either reload mode): D-pad up pulse until this tick.
		std::atomic<bool> touch_enabled{ true };
		std::atomic<bool> accessory_busy{};
		std::atomic<ULONGLONG> pulse_until{};
		constexpr ULONGLONG pulse_ms = 120;
		accessory_touch toucher;
		std::uint64_t accessory_toggles{};

		struct slot_memory
		{
			std::string name;
			std::uintptr_t ammo{}; // server weapon ammo: a new object (respawn, level) starts fresh
			reload gun;
			break_open launcher;   // #64: used instead of gun when the weapon is a break-open launcher
			int written{ -1 };     // magazine value we last wrote into the game
			int written_spares{ 2 }; // #89: the spares in it
			int written_reserve{ -1 };
		};

		// #56: the held magazine for the HUD (read on the XR thread); stale after a quarter second.
		std::mutex magazine_view_lock;
		held_magazine_view magazine_view;
		ULONGLONG magazine_view_at{};

		std::mutex lock;
		std::map<int, slot_memory> slots;
		int current_slot{ -1 };
		reload_hands hands;
		bool handle_override_on{}; // research: pipe `reload handle x y z` (Wep_Root axes), `reload handle off`
		float handle_override[3]{};
		hand_result::zone last_zone{ hand_result::zone::none };
		hand_result last_result; // for the pipe report
		LARGE_INTEGER last_update{};
		const char* recent[8]{};
		int recent_count{};
		std::uint64_t shots{};
		bool magazine_detached{};
		bool bolt_at_rear{}; // this stroke played the rear-stop cue; the release plays only the bolt going home

		const char* event_name(reload_event event)
		{
			switch (event)
			{
			case reload_event::none: return "none";
			case reload_event::shot: return "shot";
			case reload_event::dry_fire: return "dry_fire";
			case reload_event::bolt_locked: return "bolt_locked";
			case reload_event::magazine_out: return "magazine_out";
			case reload_event::magazine_dropped: return "magazine_dropped";
			case reload_event::magazine_stowed: return "magazine_stowed";
			case reload_event::magazine_taken: return "magazine_taken";
			case reload_event::magazine_in: return "magazine_in";
			case reload_event::flicked_out: return "flicked_out";
			case reload_event::chambered: return "chambered";
			case reload_event::racked_empty: return "racked_empty";
			case reload_event::round_ejected: return "round_ejected";
			case reload_event::magazine_palmed: return "magazine_palmed";
			case reload_event::racked_locked: return "racked_locked";
			case reload_event::shell_loaded: return "shell_loaded";
			case reload_event::lid_opened: return "lid_opened";
			case reload_event::lid_closed: return "lid_closed";
			case reload_event::belt_taken: return "belt_taken";
			case reload_event::belt_laid: return "belt_laid";
			case reload_event::belt_dropped: return "belt_dropped";
			}
			return "?";
		}

		// The gun in hand's sound folder ("AK12", "M320"): its own cues cut from the game's files are in
		// bfvr_sounds\<folder> (tools/game_cues.py), e.g. its rear stop.
		char gun_folder[64]{};

		void play_gun(cue which)
		{
			play(which, gun_folder);
		}

		// The game's own reload sound for this gun where its reload entity is known
		// (weapon/game_reload_sounds), else the gun's own cue from the game's files or the synthesised one (weapon/reload_sounds).
		void sound_for(reload_event event)
		{
			if (play_game_reload_sound(event))
			{
				return;
			}
			switch (event)
			{
			case reload_event::magazine_taken:
			case reload_event::magazine_stowed: play_gun(cue::cloth); break;
			case reload_event::magazine_in:
			case reload_event::shell_loaded:
			case reload_event::belt_laid: play_gun(cue::magazine_in); break;
			case reload_event::lid_opened: play_gun(cue::magazine_out); break;
			case reload_event::lid_closed: play_gun(cue::chamber); break;
			case reload_event::belt_taken:
			case reload_event::belt_dropped: play_gun(cue::cloth); break;
			case reload_event::magazine_out:
			case reload_event::flicked_out:
			case reload_event::magazine_palmed: play_gun(cue::magazine_out); break;
			case reload_event::chambered:
			case reload_event::round_ejected:
			case reload_event::racked_empty:
			case reload_event::racked_locked: play_gun(bolt_at_rear ? cue::bolt_home : cue::chamber); break;
			default: break;
			}
		}

		// The reloading hand (the off hand, or the free gun hand while the off hand carries the gun, #38) and
		// the hand holding the gun.
		int reloading_hand{};
		void feel(reload_event event)
		{
			switch (event)
			{
			case reload_event::dry_fire: input::haptic(1 - reloading_hand, input::pulses::click); break;
			case reload_event::bolt_locked: input::haptic(1 - reloading_hand, input::pulses::grab); break;
			case reload_event::magazine_out:
			case reload_event::flicked_out:
			case reload_event::magazine_palmed:
			case reload_event::magazine_taken: input::haptic(reloading_hand, input::pulses::grab); break;
			case reload_event::magazine_stowed:
			case reload_event::shell_loaded:
			case reload_event::belt_taken:
			case reload_event::lid_opened: input::haptic(reloading_hand, input::pulses::tick); break;
			case reload_event::belt_laid:
			case reload_event::lid_closed:
				input::haptic(reloading_hand, input::pulses::seat);
				input::haptic(1 - reloading_hand, input::pulses::grab);
				break;
			case reload_event::magazine_in:
			case reload_event::chambered:
			case reload_event::racked_empty:
			case reload_event::racked_locked:
			case reload_event::round_ejected:
				input::haptic(reloading_hand, input::pulses::seat);
				input::haptic(1 - reloading_hand, input::pulses::grab);
				break;
			default: break;
			}
		}

		bool out_cue_played{}; // this frame: a magazine-out cue already sounded (#78)

		void remember(reload_event event)
		{
			if (event == reload_event::magazine_out || event == reload_event::flicked_out || event == reload_event::magazine_palmed)
			{
				out_cue_played = true;
			}
			sound_for(event);
			feel(event);
			if (event == reload_event::none || event == reload_event::shot)
			{
				return;
			}
			if (recent_count == static_cast<int>(std::size(recent)))
			{
				std::memmove(recent, recent + 1, sizeof(recent) - sizeof(recent[0]));
				--recent_count;
			}
			recent[recent_count++] = event_name(event);
		}

		// AR: the release drops the magazine out of the gun (magazine_dropped), which must sound like a
		// magazine coming out, also while the other hand holds the new one (#78); a magazine dropped from the hand must not.
		void sound_if_magazine_left(bool was_in, const reload& gun)
		{
			if (release_needs_out_cue(was_in, gun.state().magazine_in, out_cue_played))
			{
				sound_for(reload_event::magazine_out);
			}
			out_cue_played = false;
		}

		// The game's magazine for our state: rounds in the gun plus the spare (empty: only the
		// spare, and the trigger is blocked).
		int game_magazine(const reload& gun)
		{
			return gun.rounds_in_gun() + spares_now;
		}

		// The drawn off hand: its finger shape and, while racking, its hold on the charging handle (eased
		// on and off; the pull itself springs back at once, like the real handle).
		float handle_weight{};
		bool handle_underhand{}; // the last stroke's grab: underhand (kept while the hand eases off the handle)
		float handle_side{};     // its handle's side of the gun (+1 left, -1 right, 0 on top)
		float handle_in_gun[3]{};
		// #40: the open off hand snapping onto the seated magazine, eased in quickly and out more slowly (the
		// hand returns to the controller after the grab with the magazine in it).
		std::atomic<bool> magazine_snapping{ true }; // always on since #48 (was magazine_snap)
		std::atomic<float> magazine_reach{ 0.13f };   // #57: the magazine zone's reach (low chest down to the off-side hip)
		float snap_weight{};
		bool snap_ak{};

		void ease_magazine_snap(const hand_result* result, bool ak, float seconds)
		{
			const float target = result && magazine_snapping.load(std::memory_order_relaxed) ? result->magazine_snap : 0.0f;
			const float tau = target > snap_weight ? 0.05f : 0.12f;
			snap_weight += (target - snap_weight) * (1.0f - std::exp(-seconds / tau));
			if (target == 0.0f && snap_weight < 0.01f)
			{
				snap_weight = 0.0f;
			}
			if (target > 0.0f)
			{
				snap_ak = ak;
			}
			body::set_magazine_snap(snap_weight, snap_ak);
		}

		// Grip lock: the drawn off hand onto the magazine on the rail, in within a frame or two, out a little
		// slower; while it fades out it stays on the last locked pose.
		std::atomic<bool> grip_lock_on{ true };
		float lock_weight{};
		body::magazine_lock_pose lock_pose;

		void ease_magazine_lock(const hand_result* result, bool ak, float seconds)
		{
			const float target = result ? result->hand_lock : 0.0f;
			const float tau = target > lock_weight ? 0.02f : 0.07f;
			lock_weight += (target - lock_weight) * (1.0f - std::exp(-seconds / tau));
			if (target == 0.0f && lock_weight < 0.01f)
			{
				lock_weight = 0.0f;
			}
			if (target > 0.0f)
			{
				lock_pose.ak = ak;
				lock_pose.position[0] = result->lock_position.x;
				lock_pose.position[1] = result->lock_position.y;
				lock_pose.position[2] = result->lock_position.z;
				lock_pose.rotation[0] = result->lock_rotation.x;
				lock_pose.rotation[1] = result->lock_rotation.y;
				lock_pose.rotation[2] = result->lock_rotation.z;
				lock_pose.rotation[3] = result->lock_rotation.w;
			}
			lock_pose.weight = lock_weight;
			body::set_magazine_lock(lock_pose);
		}

		void draw_off_hand(body::fingers::task task, const hand_result* result, bool has_root, float seconds, bool ak = false)
		{
			ease_magazine_snap(result, ak, seconds);
			ease_magazine_lock(result, ak, seconds);
			body::set_off_hand_task(grenade_in_hand() && grenade_hand() == 0 ? body::fingers::task::magazine
				: task == body::fingers::task::free && off_hand_at_belt() ? body::fingers::task::reach : task);
			const bool racking = result && (result->racking || result->pumping || (result->lid_held && lid_lock_on.load(std::memory_order_relaxed))) && has_root; // #95: a held lid holds the hand
			if (racking)
			{
				handle_in_gun[0] = result->handle_in_gun.x;
				handle_in_gun[1] = result->handle_in_gun.y;
				handle_in_gun[2] = result->handle_in_gun.z;
			}
			handle_weight += ((racking ? 1.0f : 0.0f) - handle_weight) * (1.0f - std::exp(-seconds / 0.04f));
			if (!racking && handle_weight < 0.01f)
			{
				handle_weight = 0.0f;
			}
			if (racking)
			{
				handle_underhand = result->rack_underhand;
				handle_side = result->rack_side;
			}
			body::set_handle_hold(handle_weight, handle_in_gun, racking ? result->rack_pull : 0.0f, handle_underhand, handle_side);
		}

		void attach_magazine()
		{
			body::set_extra_magazine({}); // a later step this update may ask for one again
			if (magazine_detached)
			{
				body::set_magazine_pose({});
				magazine_detached = false;
			}
		}

		// Called with lock held: hands the game back its own ammo (rounds in the gun, no spare).
		void release_slot(const game_weapon& weapon)
		{
			const auto found = slots.find(current_slot);
			if (found != slots.end() && weapon.valid && weapon.slot == current_slot && found->second.written >= 0)
			{
				const reload& gun = found->second.gun;
				write(weapon.ammo, gun.rounds_in_gun());
				write(weapon.ammo + 4, gun.state().reserve);
			}
			slots.clear();
			current_slot = -1;
			hands.reset();
			attach_magazine();
		}

		weapon::v3 v(const float* p)
		{
			return { p[0], p[1], p[2] };
		}

		weapon::q4 q(const float* p)
		{
			return { p[0], p[1], p[2], p[3] };
		}

		// The magazine pouch: 35 cm below the head, 10 cm ahead along the head's yaw.
		bool chest_point(weapon::v3& chest)
		{
			return weapon::body_rig_point(weapon::magazine_pouch, chest);
		}

		// ---- #64: the M320 reloaded by hand (weapon/break_open; design after The Great Destroyer's src/m320.h) ----
		// The trigger pulled while it is not ready works the release; a spent case slides out with the muzzle up or is
		// pulled out at the barrel's back end; a round from the chest pouch goes into the open, empty barrel; the open
		// hand pushed onto the barrel's left side swings it shut. The game's count is kept at ours + spare_rounds, so
		// its own reload (after every shot) never starts.
		constexpr float launcher_grab_reach = 0.08f;   // the off palm this near the barrel's back end takes from it
		constexpr float launcher_insert_reach = 0.05f; // a held round this near the open, empty barrel goes in
		constexpr float launcher_push_reach = 0.05f;   // the open palm this near the barrel's left side shuts it ...
		constexpr float launcher_push_arm = 0.12f;     // ... once it was this far from it since the barrel opened
		constexpr float launcher_swing_seconds = 0.12f;
		constexpr float launcher_case_seconds = 0.8f;   // a spent case falls this long, then it is gone
		bool launcher_shown{};
		bool launcher_trigger_was{};
		bool launcher_grip_was{};
		bool launcher_side_armed{};
		float launcher_open{};
		float case_fall{ -1.0f };
		weapon::v3 case_at{};

		float distance(const weapon::v3& a, const weapon::v3& b)
		{
			const float x = a.x - b.x, y = a.y - b.y, z = a.z - b.z;
			return std::sqrt(x * x + y * y + z * z);
		}

		const char* launcher_event_name(launcher_event event)
		{
			switch (event)
			{
			case launcher_event::none: return "none";
			case launcher_event::shot: return "shot";
			case launcher_event::opened: return "opened";
			case launcher_event::case_out: return "case_out";
			case launcher_event::round_taken: return "round_taken";
			case launcher_event::round_seated: return "round_seated";
			case launcher_event::round_out: return "round_out";
			case launcher_event::round_stowed: return "round_stowed";
			case launcher_event::round_dropped: return "round_dropped";
			case launcher_event::closed: return "closed";
			}
			return "?";
		}

		void note_launcher(launcher_event event, const weapon::v3& mouth)
		{
			switch (event)
			{
			case launcher_event::opened:
				play_gun(cue::magazine_out);
				input::haptic(1, input::pulses::click);
				break;
			case launcher_event::case_out:
				case_fall = 0.0f;
				case_at = mouth;
				input::haptic(1, input::pulses::tick);
				break;
			case launcher_event::round_taken:
			case launcher_event::round_out:
				play_gun(cue::cloth);
				input::haptic(0, input::pulses::grab);
				break;
			case launcher_event::round_stowed:
				play_gun(cue::cloth);
				input::haptic(0, input::pulses::tick);
				break;
			case launcher_event::round_seated:
				play_gun(cue::magazine_in);
				input::haptic(0, input::pulses::seat);
				break;
			case launcher_event::closed:
				play_gun(cue::chamber);
				input::haptic(0, input::pulses::seat);
				input::haptic(1, input::pulses::grab);
				break;
			default:
				break;
			}
			if (event == launcher_event::none || event == launcher_event::shot)
			{
				return;
			}
			if (recent_count == static_cast<int>(std::size(recent)))
			{
				std::memmove(recent, recent + 1, sizeof(recent) - sizeof(recent[0]));
				--recent_count;
			}
			recent[recent_count++] = launcher_event_name(event);
		}

		void hide_launcher()
		{
			if (launcher_shown)
			{
				body::set_launcher_pose({});
				launcher_shown = false;
			}
			launcher_open = 0.0f;
			case_fall = -1.0f;
		}

		// Belt-fed: the lid and the belt as the hands have them (body::belt_feed_pose); off for every other gun.
		bool belt_feed_shown{};
		void draw_belt_feed(const hand_result* result, float lid_degrees, bool revolver = false, bool rounds = true)
		{
			if (!result)
			{
				if (belt_feed_shown)
				{
					body::set_belt_feed_pose({});
					belt_feed_shown = false;
				}
				return;
			}
			body::belt_feed_pose pose;
			pose.active = true;
			pose.lid = result->lid;
			pose.lid_degrees = lid_degrees;
			pose.revolver = revolver;
			pose.rounds = rounds;
			pose.belt = result->belt_draw == 0 ? body::belt_feed_pose::belt_place::laid
				: result->belt_draw == 2 ? body::belt_feed_pose::belt_place::at_pose : body::belt_feed_pose::belt_place::hanging;
			pose.belt_position[0] = result->belt_position.x;
			pose.belt_position[1] = result->belt_position.y;
			pose.belt_position[2] = result->belt_position.z;
			pose.belt_rotation[0] = result->belt_rotation.x;
			pose.belt_rotation[1] = result->belt_rotation.y;
			pose.belt_rotation[2] = result->belt_rotation.z;
			pose.belt_rotation[3] = result->belt_rotation.w;
			body::set_belt_feed_pose(pose);
			belt_feed_shown = true;
		}

		void update_launcher(const game_weapon& weapon, slot_memory& memory, const body::weapon_points& points, const input::gamepad_state& pad,
			bool grip, bool hands_valid, float seconds)
		{
			break_open& m = memory.launcher;
			const weapon::v3 mouth = v(points.barrel_mouth);
			// Shots: the game counted down from what we wrote; the game raising it (a pickup) is taken as given.
			if (memory.written >= 0)
			{
				const int fired = memory.written - weapon.magazine;
				for (int shot = 0; shot < fired && shot < 4; ++shot)
				{
					note_launcher(m.trigger(), mouth);
					++shots;
				}
				if (fired > 0)
				{
					last_shot_at = GetTickCount64();
					input::recoil(input::firing_pulse(weapon.rumble[0], weapon.rumble[1], weapon.rumble[2]));
					body::kick_gun(1, weapon.recoil);
				}
				if (weapon.magazine > memory.written)
				{
					m.load(weapon.magazine - spare_rounds, weapon.reserve);
				}
				else if (memory.written_reserve >= 0 && weapon.reserve != memory.written_reserve)
				{
					m.set_reserve(m.state().reserve + weapon.reserve - memory.written_reserve);
				}
			}
			// The release: a fresh pull while it is not ready (when ready, the pull goes to the game and fires).
			const bool trigger = pad.right_trigger > 128;
			if (trigger && !launcher_trigger_was && !m.ready())
			{
				note_launcher(m.trigger(), mouth);
			}
			launcher_trigger_was = trigger;

			const bool usable = hands_valid && points.has_launcher;
			const bool grip_pressed = grip && !launcher_grip_was;
			launcher_grip_was = grip;
			const weapon::v3 palm = v(points.palm[0]);
			weapon::v3 chest{};
			const bool at_chest = chest_point(chest) && distance(palm, chest) <= magazine_reach.load(std::memory_order_relaxed);
			if (usable)
			{
				const float to_mouth = distance(palm, mouth);
				const float to_side = distance(palm, v(points.barrel_side));
				if (m.state().open && points.has_root)
				{
					// The barrel's forward (Wep_Root +z) height: how far the muzzle points up.
					const float* r = points.root_rotation;
					const float up = 2.0f * (r[1] * r[2] + r[3] * r[0]);
					note_launcher(m.tip(std::asin(std::clamp(up, -1.0f, 1.0f)) * 57.29578f), mouth);
				}
				if (grip_pressed)
				{
					if (at_chest)
					{
						note_launcher(m.take_from_chest(), mouth);
					}
					else if (m.state().open && to_mouth <= launcher_grab_reach)
					{
						note_launcher(m.grab_barrel(), mouth);
					}
				}
				if (m.state().holding && to_mouth <= launcher_insert_reach)
				{
					note_launcher(m.insert(), mouth);
				}
				if (!m.state().open)
				{
					launcher_side_armed = false;
				}
				else if (to_side > launcher_push_arm)
				{
					launcher_side_armed = true;
				}
				else if (launcher_side_armed && !grip && !m.state().holding && to_side <= launcher_push_reach)
				{
					note_launcher(m.close(), mouth);
				}
			}
			if (!grip && m.state().holding)
			{
				note_launcher(m.let_go(at_chest), mouth);
			}

			// Drawing: the barrel swings over launcher_swing_seconds; the case falls for a moment once out.
			const float target = m.state().open ? 1.0f : 0.0f;
			const float step = seconds / launcher_swing_seconds;
			launcher_open += std::clamp(target - launcher_open, -step, step);
			body::launcher_pose pose;
			pose.active = true;
			pose.open = launcher_open;
			using place = body::launcher_pose::place;
			const barrel_load load = m.state().load;
			pose.round = load == barrel_load::live ? place::in_barrel : place::hidden;
			if (m.state().holding)
			{
				pose.round = place::at_pose;
				std::copy(std::begin(points.palm[0]), std::end(points.palm[0]), pose.round_position);
				std::copy(std::begin(points.root_rotation), std::end(points.root_rotation), pose.round_rotation);
			}
			pose.spent_case = load == barrel_load::empty ? place::hidden : place::in_barrel;
			if (m.state().holding)
			{
				pose.spent_case = place::hidden;
			}
			if (case_fall >= 0.0f)
			{
				case_fall += seconds;
				if (case_fall > launcher_case_seconds)
				{
					case_fall = -1.0f;
				}
				else
				{
					pose.spent_case = place::at_pose;
					pose.case_position[0] = case_at.x;
					pose.case_position[1] = case_at.y - 4.9f * case_fall * case_fall;
					pose.case_position[2] = case_at.z;
					std::copy(std::begin(points.root_rotation), std::end(points.root_rotation), pose.case_rotation);
				}
			}
			body::set_launcher_pose(pose);
			launcher_shown = true;

			const int wanted = m.rounds_in_gun() + spare_rounds;
			if (wanted != weapon.magazine)
			{
				write(weapon.ammo, wanted);
			}
			if (m.game_reserve() != weapon.reserve)
			{
				write(weapon.ammo + 4, m.game_reserve());
			}
			memory.written = wanted;
			memory.written_reserve = m.game_reserve();

			active = true;
			block_trigger = !m.ready();
			block_grip = usable && (m.state().holding || at_chest || (m.state().open && distance(palm, mouth) <= launcher_grab_reach));
			hand_engaged = m.state().holding;
			claims_free_hand = false;
			draw_off_hand(m.state().holding ? body::fingers::task::magazine : body::fingers::task::free, nullptr, points.has_root, seconds);
			attach_magazine();
		}
	}

	void set_gun_points(bool on)
	{
		gun_points = on;
	}

	void set_manual_cycle(bool on)
	{
		manual_cycle_on = on;
	}

	void set_pump_at_rest(bool on)
	{
		pump_at_rest_on = on;
	}

	void set_lid_hand_lock(bool on)
	{
		lid_lock_on = on;
	}

	void set_shells_by_hand(bool on)
	{
		shells_by_hand = on;
	}

	void set_belts_by_hand(bool on)
	{
		belts_by_hand = on;
	}

	void set_revolver_by_hand(bool on)
	{
		revolver_by_hand = on;
	}

	void set_physical_reload_all(bool on)
	{
		reload_all = on;
	}

	void set_physical_reload(bool enabled)
	{
		physical = enabled;
	}

	void set_drop_discards_ammo(bool value)
	{
		discard = value;
	}

	void set_palm_reload(bool enabled)
	{
		palming = enabled;
	}

	void set_magazine_snapping(bool enabled)
	{
		magazine_snapping = enabled;
	}

	void set_magazine_reach(float metres)
	{
		magazine_reach = metres;
	}

	void set_free_hand_reload(bool enabled)
	{
		free_hand_reload = enabled;
	}

	void set_magazine_flight(bool enabled)
	{
		magazine_flight_on = enabled;
	}

	bool held_is_pistol()
	{
		return pistol_in_hand.load(std::memory_order_relaxed);
	}

	void set_grip_lock(bool enabled)
	{
		grip_lock_on = enabled;
		body::set_rack_lock(enabled);
	}

	bool reload_claims_free_hand()
	{
		return active.load(std::memory_order_relaxed) && claims_free_hand.load(std::memory_order_relaxed);
	}

	void set_accessory_touch(bool enabled)
	{
		touch_enabled = enabled;
	}

	void update_physical_reload()
	{
#if defined(_M_X64)
		const std::lock_guard guard(lock);
		LARGE_INTEGER now{}, frequency{};
		QueryPerformanceCounter(&now);
		QueryPerformanceFrequency(&frequency);
		const float seconds = last_update.QuadPart ? std::fmin(static_cast<float>(now.QuadPart - last_update.QuadPart) / static_cast<float>(frequency.QuadPart), 0.1f) : 0.0f;
		last_update = now;

		const body::weapon_points points = body::latest_weapon_points();
		const input::gamepad_state pad = input::latest();
		const bool hands_valid = points.valid && points.hand_valid[0] && points.hand_valid[1] && GetTickCount64() - points.at < points_max_age_ms;
		// A grenade in the off hand owns its grip (weapon/physical_grenade).
		const bool grip = (pad.buttons & XINPUT_GAMEPAD_LEFT_SHOULDER) != 0 && !grenade_in_hand();
		{
			const auto held = slots.find(current_slot);
			accessory_frame touch;
			touch.hands_valid = hands_valid;
			touch.off_hand = v(points.hand[0]);
			touch.has_optic = points.has_optic;
			touch.optic = v(points.optic);
			touch.has_rail = points.has_rail;
			touch.rail = v(points.rail);
			touch.magwell = v(points.magazine);
			touch.holding = (held != slots.end() && (held->second.gun.state().holding || held->second.gun.state().belt_held)) || last_result.belt_zone; // #95: the lid and belt keep their grip
			touch.grip = grip;
			const accessory_result touched = toucher.update(touch);
			const bool enabled = touch_enabled.load(std::memory_order_relaxed);
			accessory_busy = enabled && touched.busy;
			if (enabled && touched.toggle)
			{
				pulse_until = GetTickCount64() + pulse_ms;
				++accessory_toggles;
			}
		}

		const game_weapon weapon = read_game_weapon();
		// #83: the game's quick throw makes the grenade the weapon for ~200 ms of a physical throw: keep the gun's
		// state as it is (a reset here dropped the reload hands and flickered them).
		if (!weapon.valid && current_slot >= 0 && grenade_throw_pending())
		{
			return;
		}
		// all-weapons step 1: tube, belt and revolver guns keep the game's own reload until their families are built
		// (the AR reload treated a shotgun's tube or an LMG's belt as a box magazine).
		const gun_traits weapon_traits = traits_for(weapon.name);
		const bool game_reloads = weapon.valid && !weapon.launcher && !reload_all.load(std::memory_order_relaxed)
			&& (!physical_reload_covers(weapon_traits) || (weapon_traits.feed == feed::tube && !shells_by_hand.load(std::memory_order_relaxed))
				|| (weapon_traits.feed == feed::belt && !belts_by_hand.load(std::memory_order_relaxed))
				|| (weapon_traits.feed == feed::cylinder && !revolver_by_hand.load(std::memory_order_relaxed)));
		game_reload_gun = game_reloads;
		if (!physical.load(std::memory_order_relaxed) || game_reloads)
		{
			// Button reload: nothing writes the magazine, so any drop is the game firing.
			static std::uintptr_t seen_ammo{};
			static int seen_magazine{ -1 };
			if (weapon.valid && weapon.ammo == seen_ammo && weapon.magazine < seen_magazine)
			{
				input::recoil(input::firing_pulse(weapon.rumble[0], weapon.rumble[1], weapon.rumble[2]));
				body::kick_gun(std::min(seen_magazine - weapon.magazine, 3), weapon.recoil);
				last_shot_at = GetTickCount64();
			}
			seen_ammo = weapon.valid ? weapon.ammo : 0;
			seen_magazine = weapon.valid ? weapon.magazine : -1;
		}
		if (!physical.load(std::memory_order_relaxed) || !weapon.valid || game_reloads)
		{
			if (!physical.load(std::memory_order_relaxed))
			{
				release_slot(weapon);
			}
			else
			{
				// A gun the game reloads keeps the other guns' state (their magazines may be out); nothing is written.
				hands.reset();
				attach_magazine();
			}
			if (game_reloads && game_reload_logged != weapon.name)
			{
				game_reload_logged = weapon.name;
				log::write("reload: %s slot %d: the game's own reload (fed by a tube, a belt or a cylinder)", weapon.name, weapon.slot);
			}
			hide_launcher();
			draw_belt_feed(nullptr, 0.0f);
			active = false;
			block_trigger = false;
			block_grip = false;
			hand_engaged = false;
			claims_free_hand = false;
			body::set_pump_rest(false, 0);
			last_result.belt_zone = false; // #95: no lid or belt holds the accessory touch off now
			draw_off_hand(body::fingers::task::free, nullptr, false, seconds);
			return;
		}

		// A weapon switch keeps the old gun's state (its magazine may be out) for when it comes back.
		auto found = slots.find(weapon.slot);
		if (found == slots.end() || found->second.name != weapon.name || found->second.ammo != weapon.ammo)
		{
			slot_memory fresh{ weapon.name, weapon.ammo, reload(profile_for(weapon.name, weapon.capacity)), break_open{}, -1 };
			fresh.gun.load(weapon.magazine, weapon.reserve);
			fresh.launcher.load(weapon.magazine, weapon.reserve);
			found = slots.insert_or_assign(weapon.slot, std::move(fresh)).first;
			log::write("reload: %s slot %d %s, capacity %d, gun %d, reserve %d", weapon.name, weapon.slot,
				weapon.launcher ? "launcher" : found->second.gun.profile().kind == variant::ak ? "AK" : "AR", found->second.gun.profile().capacity,
				weapon.magazine, weapon.reserve);
		}
		if (weapon.slot != current_slot)
		{
			hands.reset();
			current_slot = weapon.slot;
		}
		game_reload_sounds_weapon(weapon.name, weapon.sound_folder.c_str());
		strncpy_s(gun_folder, weapon.sound_folder.c_str(), _TRUNCATE);
		zone_book::set_current_gun(weapon.name); // the panel's zone sliders edit this gun
		slot_memory& memory = found->second;
		if (weapon.launcher)
		{
			draw_belt_feed(nullptr, 0.0f);
			body::set_pump_rest(false, 0);
			last_result.belt_zone = false;
			update_launcher(weapon, memory, points, pad, grip, hands_valid && !gun_dropped(), seconds);
			return;
		}
		hide_launcher();
		reload& gun = memory.gun;
		gun.configure({ discard.load(std::memory_order_relaxed) });
		gun.set_manual_cycle(manual_cycle_on.load(std::memory_order_relaxed) && profile_for(weapon.name, weapon.capacity).manual_cycle);

		// Shots: the game counted down from what we wrote. Anything else (pickups) moves the reserve.
		if (memory.written >= 0)
		{
			const int fired = std::clamp(memory.written - weapon.magazine, 0, 100);
			for (int shot = 0; shot < fired; ++shot)
			{
				remember(gun.fire());
				++shots;
			}
			// #89: the gun's rate of fire, averaged over the frames of a burst (zero-shot frames too, so one shot in a
			// short frame is not read as a hundred a second); the spares follow it and the frame time.
			const ULONGLONG now_tick = GetTickCount64();
			if (fired > 0)
			{
				burst_at = now_tick;
			}
			if (seconds > 0.0f && now_tick - burst_at < 150)
			{
				const float weight = 1.0f - std::exp(-seconds / 0.3f);
				rounds_per_second += (std::fmin(fired / seconds, 200.0f) - rounds_per_second) * weight;
				rounds_per_second = std::clamp(rounds_per_second, 15.0f, 30.0f); // at least 900 rpm: the frames after the block count too
			}
			if (memory.written > weapon.magazine)
			{
				last_shot_at = GetTickCount64();
				input::recoil(input::firing_pulse(weapon.rumble[0], weapon.rumble[1], weapon.rumble[2]));
				body::kick_gun(std::min(memory.written - weapon.magazine, 3), weapon.recoil);
			}
			if (weapon.magazine > memory.written)
			{
				log::write("reload: the game raised %s's magazine %d -> %d; taking its rounds", weapon.name, memory.written, weapon.magazine);
				gun.load(weapon.magazine - memory.written_spares, weapon.reserve);
			}
			else if (memory.written_reserve >= 0 && weapon.reserve != memory.written_reserve)
			{
				// The game itself changed the reserve since our write (pickup, resupply).
				gun.set_reserve(gun.state().reserve + weapon.reserve - memory.written_reserve);
			}
		}

		hand_frame frame;
		frame.seconds = seconds;
		frame.hands_valid = hands_valid && !gun_dropped(); // free hands: nothing to reload on the floor
		// #38: while the off hand carries the gun, the free gun hand reloads (free_hand_reload; else it may
		// only rack). Palming turns the magazine to the left hand's outside, so it stays the left hand's.
		const bool swapped = gun_in_off_hand();
		const int reloading = swapped ? 1 : 0;
		reloading_hand = reloading;
		frame.handle_only = swapped && !free_hand_reload.load(std::memory_order_relaxed);
		frame.off_hand = v(points.hand[reloading]);
		frame.has_palm = true;
		frame.off_palm = v(points.palm[reloading]);
		frame.off_rotation = q(points.hand_rotation[reloading]);
		frame.has_root = points.has_root;
		frame.root = v(points.root);
		frame.root_rotation = q(points.root_rotation);
		frame.gun_hand = v(points.hand[1 - reloading]);
		frame.gun_rotation = q(points.hand_rotation[1 - reloading]);
		frame.magwell = v(points.magazine);
		frame.magazine_rotation = q(points.magazine_rotation);
		frame.has_slide = points.has_slide;
		frame.slide = v(points.slide);
		if (!chest_point(frame.chest))
		{
			frame.chest = { 0.0f, -0.35f, -0.1f };
		}
		frame.has_chest_hip = weapon::body_rig_point(weapon::magazine_hip, frame.chest_hip);
		frame.chest_reach = magazine_reach.load(std::memory_order_relaxed);
		{
			// The charging handle: the research override (pipe `reload handle x y z`), the gun's measured point, else the variant's.
			float point[3]{};
			const bool per_gun = gun_points.load(std::memory_order_relaxed);
			frame.has_handle_point = per_gun && handle_point_for(weapon.name, point);
			if (handle_override_on)
			{
				std::copy(std::begin(handle_override), std::end(handle_override), point);
				frame.has_handle_point = true;
			}
			frame.handle_point = { point[0], point[1], point[2] };
			frame.has_rack_shape = rack_zones_for(weapon.name, hand_config{}, frame.rack_shape);
			float grab[3]{};
			frame.has_magazine_grab = per_gun && magazine_grab_for(weapon.name, grab);
			frame.magazine_grab = { grab[0], grab[1], grab[2] };
			frame.has_way_out = per_gun && magazine_way_out_for(weapon.name, frame.way_out_degrees);
			frame.pump = gun.profile().manual_cycle && traits_for(weapon.name).cycle == cycle::pump;
			// #94: the hand pumps it, so the game's stroke after every shot is not drawn (keyed by the gun's ammo object).
			body::set_pump_rest(frame.pump && pump_at_rest_on.load(std::memory_order_relaxed),
				static_cast<unsigned>(weapon.ammo ^ (weapon.ammo >> 32)) | 1u);
			frame.tube = gun.profile().tube;
			frame.belt = gun.profile().belt;
			frame.revolver = gun.profile().revolver;
			frame.has_belt_points = points.has_belt_feed;
			frame.lid_hinge = v(points.lid_hinge);
			frame.lid_grab = v(points.lid_grab);
			frame.feed_tray = v(points.feed_tray);
			frame.belt_end = v(points.belt_end);
			frame.lid_degrees = lid_degrees_for(weapon.name);
			if (zone_book::enabled())
			{
				// The owner's per-gun tuning (panel Weapon > Tune grab zones, bfvr_zones.ini).
				const zone_tuning::gun_tunes tuned = zone_book::get(weapon.name);
				const auto shift_of = [&](zone_tuning::zone which)
				{
					const zone_tuning::shift s = zone_tuning::in_gun_frame(tuned.zones[static_cast<int>(which)]);
					return v3{ s.x, s.y, s.z };
				};
				frame.handle_shift = shift_of(zone_tuning::zone::handle);
				frame.handle_size = tuned.zones[static_cast<int>(zone_tuning::zone::handle)].size;
				frame.magazine_shift = shift_of(zone_tuning::zone::magazine);
				frame.magazine_size = tuned.zones[static_cast<int>(zone_tuning::zone::magazine)].size;
				frame.lid_shift = shift_of(zone_tuning::zone::lid);
				frame.lid_size = tuned.zones[static_cast<int>(zone_tuning::zone::lid)].size;
			}
		}
		frame.has_held = points.has_held;
		frame.held_top = v(points.held_magazine);
		frame.held_rotation = q(points.held_rotation);
		frame.held_tilt = points.held_tilt;
		frame.palming = palming.load(std::memory_order_relaxed) && !swapped;
		frame.grip_lock = grip_lock_on.load(std::memory_order_relaxed) && !swapped;
		frame.throw_flight = magazine_flight_on.load(std::memory_order_relaxed);
		frame.floor_valid = stereo::latest_floor(frame.floor_y);
		if (!frame.floor_valid && points.has_floor)
		{
			frame.floor_valid = true; // no STAGE floor: the soldier's own ground
			frame.floor_y = points.floor_y;
		}
		// #61: discarded magazines on the level's real ground (the game's ray cast), in the game's world.
		frame.has_world = points.valid;
		frame.world_origin = v(points.world_origin);
		frame.world_to_local = q(points.world_to_local);
		frame.ground = &physics::level_ground();
		{
			const stereo::head_sample head = stereo::latest_head_sample();
			if (head.valid)
			{
				const stereo::pose centre = stereo::eye_math::centre_pose(head);
				const float* o = centre.orientation;
				frame.has_view = true;
				frame.view_position = { centre.position[0], centre.position[1], centre.position[2] };
				frame.view_forward = { -2.0f * (o[0] * o[2] + o[3] * o[1]), -2.0f * (o[1] * o[2] - o[3] * o[0]), -(1.0f - 2.0f * (o[0] * o[0] + o[1] * o[1])) };
			}
		}
		// A grip on an accessory toggles it; the reload does not see it.
		frame.grip = swapped ? (pad.buttons & XINPUT_GAMEPAD_RIGHT_SHOULDER) != 0 : grip && !accessory_busy.load(std::memory_order_relaxed);
		// The free gun hand's grip near the pistol grip takes the gun back (free hands): when the hand is
		// nearer the pistol grip than any reload zone (as last placed) and holds nothing, the reload leaves it.
		const bool grip_first = swapped && points.has_support && !gun.state().holding && !last_result.racking
			&& grip_before_reload(frame.off_palm, v(points.grip), free_hands_config{}.regrip_reach, last_result);
		if (grip_first)
		{
			frame.grip = false;
		}
		frame.button = (pad.buttons & XINPUT_GAMEPAD_B) != 0; // right B: release / bolt catch
		const bool magazine_was_in = gun.state().magazine_in;
		const hand_result result = hands.update(gun, frame);
		if (result.in_zone != hand_result::zone::none && result.in_zone != last_zone)
		{
			input::haptic(reloading, input::pulses::tick);
		}
		last_zone = result.in_zone;
		last_result = result;
		if (result.palm_ready_now)
		{
			input::haptic(reloading, input::pulses::tick);
		}
		if (result.rack_at_rear)
		{
			// The stroke is long enough: the rear stop is felt and heard before the release (Overlord
			// plays its chamber recordings in two parts the same way).
			if (!play_game_rear_stop()) // the pump's own event where the gun's patch has one, else its cut from the game's files
			{
				play_gun(cue::bolt_back);
			}
			input::haptic(reloading, input::pulses::click);
			bolt_at_rear = true;
		}
		if (grab_debug_enabled() && hands_valid)
		{
			const grab_slot slots_shown[3]{ grab_slot::magazine, grab_slot::handle, grab_slot::pouch };
			const hand_result::zone zones_shown[3]{ hand_result::zone::magazine, hand_result::zone::handle, hand_result::zone::chest };
			for (int i = 0; i < 3; ++i)
			{
				const float centre[3]{ result.zone_centre[i].x, result.zone_centre[i].y, result.zone_centre[i].z };
				if (i == 1 && !result.has_handle)
				{
					clear_grab_zone(slots_shown[i]);
					continue;
				}
				publish_grab_zone(slots_shown[i], centre, result.zone_reach[i], result.in_zone == zones_shown[i]);
			}
			if (result.has_under_zone)
			{
				const float under[3]{ result.under_centre.x, result.under_centre.y, result.under_centre.z };
				publish_grab_zone(grab_slot::handle_under, under, result.under_radii.y, result.in_zone == hand_result::zone::handle);
			}
			else
			{
				clear_grab_zone(grab_slot::handle_under);
			}
			if (result.has_lid_zone)
			{
				const float lid[3]{ result.lid_centre.x, result.lid_centre.y, result.lid_centre.z };
				publish_grab_zone(grab_slot::lid, lid, result.lid_reach, false);
			}
			else
			{
				clear_grab_zone(grab_slot::lid);
			}
			if (result.has_chest_hip)
			{
				const float hip[3]{ result.chest_hip.x, result.chest_hip.y, result.chest_hip.z };
				publish_grab_zone(grab_slot::pouch_hip, hip, result.zone_reach[2], result.in_zone == hand_result::zone::chest);
			}
			else
			{
				clear_grab_zone(grab_slot::pouch_hip);
			}
		}
		for (int i = 0; i < result.count; ++i)
		{
			remember(result.events[i]);
		}
		if (!result.racking && !result.pumping)
		{
			bolt_at_rear = false;
		}
		sound_if_magazine_left(magazine_was_in, gun);

		if (seconds > 0.0f && seconds < 1.0f)
		{
			frame_seconds_smooth += (seconds - frame_seconds_smooth) * 0.02f;
		}
		spares_now = spare_rounds_for(rounds_per_second, frame_seconds_smooth);
		const int wanted = game_magazine(gun);
		if (wanted != weapon.magazine)
		{
			write(weapon.ammo, wanted);
		}
		if (gun.state().reserve != weapon.reserve)
		{
			write(weapon.ammo + 4, gun.state().reserve);
		}
		memory.written = wanted;
		memory.written_spares = spares_now;
		memory.written_reserve = gun.state().reserve;

		if (result.draw == magazine_draw::with_gun || !points.valid)
		{
			attach_magazine();
		}
		else
		{
			body::magazine_pose pose;
			pose.detached = true;
			// Held (not sliding home or falling): the drawn fist holds it, turned aside while palming.
			pose.in_hand = result.in_fist;
			pose.tilt = result.magazine_tilt;
			pose.tilt_pivot = hand_config{}.palm_pivot_drop;
			pose.ak = gun.profile().kind == variant::ak;
			pose.shell = gun.profile().tube;
			pose.hold_drop = settings::global().boolean("magazine_by_gun", true) ? magazine_hold_drop(weapon.name) : 0.0f;
			pose.position[0] = result.magazine_position.x;
			pose.position[1] = result.magazine_position.y;
			pose.position[2] = result.magazine_position.z;
			pose.rotation[0] = result.magazine_rotation.x;
			pose.rotation[1] = result.magazine_rotation.y;
			pose.rotation[2] = result.magazine_rotation.z;
			pose.rotation[3] = result.magazine_rotation.w;
			body::set_magazine_pose(pose);
			magazine_detached = true;
		}
		{
			held_magazine_view view;
			view.held = points.valid && result.draw == magazine_draw::in_hand && gun.state().holding;
			view.top[0] = result.magazine_position.x;
			view.top[1] = result.magazine_position.y;
			view.top[2] = result.magazine_position.z;
			view.magwell[0] = frame.magwell.x;
			view.magwell[1] = frame.magwell.y;
			view.magwell[2] = frame.magwell.z;
			view.rounds = gun.state().held_rounds;
			view.capacity = gun.profile().capacity;
			view.stow_shown = view.held && !result.guided && result.has_chest_hip;
			view.stow_chest[0] = result.zone_centre[2].x;
			view.stow_chest[1] = result.zone_centre[2].y;
			view.stow_chest[2] = result.zone_centre[2].z;
			view.stow_hip[0] = result.chest_hip.x;
			view.stow_hip[1] = result.chest_hip.y;
			view.stow_hip[2] = result.chest_hip.z;
			view.stow_reach = result.stow_reach;
			view.stow_hot = result.stow_zone;
			const std::lock_guard view_guard(magazine_view_lock);
			magazine_view = view;
			magazine_view_at = GetTickCount64();
		}

		// A second magazine (extra meshes, #36): drawn as a copy of the game's own magazine draw.
		body::extra_magazine_pose extra;
		switch (result.extra)
		{
		case hand_result::extra_magazine::seated:
			extra.where = body::extra_magazine_pose::kind::seated;
			break;
		case hand_result::extra_magazine::in_fist:
			extra.where = body::extra_magazine_pose::kind::in_fist;
			break;
		case hand_result::extra_magazine::falling:
			extra.where = body::extra_magazine_pose::kind::at_pose;
			extra.position[0] = result.extra_position.x;
			extra.position[1] = result.extra_position.y;
			extra.position[2] = result.extra_position.z;
			extra.rotation[0] = result.extra_rotation.x;
			extra.rotation[1] = result.extra_rotation.y;
			extra.rotation[2] = result.extra_rotation.z;
			extra.rotation[3] = result.extra_rotation.w;
			break;
		default:
			break;
		}
		body::set_extra_magazine(extra);

		const body::fingers::task off_task = gun.state().holding || result.draw == magazine_draw::in_hand ? body::fingers::task::magazine
			: result.racking || (result.lid_held && lid_lock_on.load(std::memory_order_relaxed)) ? body::fingers::task::handle
			: result.in_zone != hand_result::zone::none ? body::fingers::task::reach : body::fingers::task::free;
		draw_off_hand(off_task, &result, frame.has_root, seconds, gun.profile().kind == variant::ak);
		draw_belt_feed(gun.profile().belt || gun.profile().revolver ? &result : nullptr, frame.lid_degrees, gun.profile().revolver,
			!(gun.state().lid_open && gun.state().magazine_rounds == 0));

		active = true;
		block_trigger = !gun.state().chamber;
		block_grip = result.off_hand_busy;
		hand_engaged = gun.state().holding || result.racking;
		claims_free_hand = swapped && (gun.state().holding || result.racking || (result.off_hand_busy && !grip_first));
#endif
	}

	unsigned long long last_shot_tick()
	{
		return last_shot_at.load(std::memory_order_relaxed);
	}

	bool reload_engages_off_hand()
	{
		return active.load(std::memory_order_relaxed) && hand_engaged.load(std::memory_order_relaxed);
	}

	held_magazine_view held_magazine()
	{
		const std::lock_guard guard(magazine_view_lock);
		held_magazine_view view = magazine_view;
		view.held = view.held && GetTickCount64() - magazine_view_at < 250;
		return view;
	}

	bool reload_owns_off_hand()
	{
		return accessory_busy.load(std::memory_order_relaxed) || (active.load(std::memory_order_relaxed) && block_grip.load(std::memory_order_relaxed));
	}

	input::gamepad_state filter_pad(const input::gamepad_state& pad)
	{
		input::gamepad_state out = pad;
		if (accessory_busy.load(std::memory_order_relaxed))
		{
			out.buttons &= static_cast<std::uint16_t>(~XINPUT_GAMEPAD_LEFT_SHOULDER);
		}
		if (GetTickCount64() < pulse_until.load(std::memory_order_relaxed))
		{
			out.buttons |= XINPUT_GAMEPAD_DPAD_UP;
			out.connected = true;
		}
		// A stance press (stick flick or physical stance, input/vr_controls) is the game's crouch B:
		// it passes untouched, else it would reload or be dropped.
		const bool stance_press = input::stance_press_active();
		if (!active.load(std::memory_order_relaxed))
		{
			// reload = button, or a gun the game reloads: B is the game's reload (X opens the menu, the stick crouches).
			if ((!physical.load(std::memory_order_relaxed) || game_reload_gun.load(std::memory_order_relaxed)) && !stance_press
				&& (out.buttons & XINPUT_GAMEPAD_B))
			{
				out.buttons = static_cast<std::uint16_t>((out.buttons & ~XINPUT_GAMEPAD_B) | XINPUT_GAMEPAD_X);
			}
			return out;
		}
		if (!stance_press)
		{
			out.buttons &= static_cast<std::uint16_t>(~XINPUT_GAMEPAD_B);
		}
		if (block_grip.load(std::memory_order_relaxed))
		{
			out.buttons &= static_cast<std::uint16_t>(~XINPUT_GAMEPAD_LEFT_SHOULDER);
		}
		if (block_trigger.load(std::memory_order_relaxed))
		{
			out.right_trigger = 0;
		}
		return out;
	}

	std::string reload_pipe_command(const std::string& argument)
	{
		const std::lock_guard guard(lock);
		const auto found = slots.find(current_slot);
		if (!argument.empty())
		{
			if (!config::research_enabled())
			{
				return "error: 'reload <event>' needs research = 1 in bfvr.ini";
			}
			if (argument.rfind("setammo", 0) == 0)
			{
				// Research (per-gun handle points): the gun in hand's server magazine; 0 with reload = button starts the
				// game's own empty reload, whose animation shows where the hand takes the charging handle.
				int rounds = -1;
				const game_weapon weapon = read_game_weapon();
				if (sscanf_s(argument.c_str(), "setammo %d", &rounds) != 1 || rounds < 0 || rounds > 500 || !weapon.valid)
				{
					return "error: usage: reload setammo <rounds 0..500> (a magazine gun in hand)";
				}
				write(weapon.ammo, rounds);
				return std::string("server magazine of ") + weapon.name + " set to " + std::to_string(rounds);
			}
			if (argument.rfind("handle", 0) == 0)
			{
				float x{}, y{}, z{};
				handle_override_on = sscanf_s(argument.c_str(), "handle %f %f %f", &x, &y, &z) == 3;
				handle_override[0] = x;
				handle_override[1] = y;
				handle_override[2] = z;
				char text[96];
				std::snprintf(text, sizeof(text), "handle override %s (%.3f, %.3f, %.3f)", handle_override_on ? "on" : "off", x, y, z);
				return text;
			}
			if (found == slots.end() || !active)
			{
				return "error: no physical reload active (reload = physical, a magazine weapon in hand)";
			}
			reload& gun = found->second.gun;
			const bool magazine_was_in = gun.state().magazine_in;
			reload_event event = reload_event::none;
			if (argument == "fire") event = gun.fire();
			else if (argument == "button") event = gun.press_button(false);
			else if (argument == "catch") event = gun.press_button(true);
			else if (argument == "palm") event = gun.press_button(true, true);
			else if (argument == "grab") event = gun.grab_magazine();
			else if (argument == "take") event = gun.take_from_chest();
			else if (argument == "letgo") event = gun.let_go(false);
			else if (argument == "stow") event = gun.let_go(true);
			else if (argument == "insert") event = gun.insert();
			else if (argument == "rack") event = gun.rack();
			else if (argument == "lidopen") event = gun.open_lid(); // belt-fed lid, revolver frame
			else if (argument == "lidclose") event = gun.close_lid();
			else if (argument == "belttake") event = gun.take_belt();
			else if (argument == "beltlay") event = gun.lay_belt();
			else if (argument == "rearstop")
			{
				const bool game = play_game_rear_stop(); // the rack's rear stop, as at the top of a stroke
				if (!game)
				{
					play_gun(cue::bolt_back);
				}
				return game ? "ok: rear stop, the game's" : has_file(cue::bolt_back, gun_folder) ? "ok: rear stop, from the game's files" : "ok: rear stop, none (no WAV)";
			}
			else return "error: usage: reload [fire|button|catch|palm|grab|take|letgo|stow|insert|rack|lidopen|lidclose|belttake|beltlay|rearstop|handle x y z|handle off]";
			remember(event);
			sound_if_magazine_left(magazine_was_in, gun);
		}
		const body::weapon_points points = body::latest_weapon_points();
		if (found != slots.end() && launcher_shown)
		{
			// #64: the M320 by hand.
			const launcher_state& s = found->second.launcher.state();
			const char* const loads[]{ "empty", "live", "spent" };
			const weapon::v3 palm = v(points.palm[0]);
			std::string events;
			for (int i = 0; i < recent_count; ++i)
			{
				events += std::string(i ? " " : "") + recent[i];
			}
			weapon::v3 chest{};
			const bool has_chest = chest_point(chest);
			char text[768];
			std::snprintf(text, sizeof(text),
				"reload=physical launcher weapon=%s slot=%d open=%d load=%s holding=%d reserve=%d game_reserve=%d ready=%d written=%d shots=%llu\n"
				"drawn_open=%.2f case_fall=%.2f has_launcher=%d mouth=(%.3f,%.3f,%.3f) side=(%.3f,%.3f,%.3f) palm=(%.3f,%.3f,%.3f) to_mouth=%.3f to_side=%.3f side_armed=%d"
				" chest=(%.3f,%.3f,%.3f) to_chest=%.3f reach=%.2f%s\n"
				"recent: %s",
				found->second.name.c_str(), current_slot, s.open ? 1 : 0, loads[static_cast<int>(s.load)], s.holding ? 1 : 0, s.reserve,
				found->second.launcher.game_reserve(), found->second.launcher.ready() ? 1 : 0, found->second.written, static_cast<unsigned long long>(shots),
				launcher_open, case_fall, points.has_launcher ? 1 : 0, points.barrel_mouth[0], points.barrel_mouth[1], points.barrel_mouth[2],
				points.barrel_side[0], points.barrel_side[1], points.barrel_side[2], palm.x, palm.y, palm.z, distance(palm, v(points.barrel_mouth)),
				distance(palm, v(points.barrel_side)), launcher_side_armed ? 1 : 0, chest.x, chest.y, chest.z, distance(palm, chest),
				magazine_reach.load(), has_chest ? "" : " (no chest)", events.c_str());
			return text;
		}
		const hand_result& r = last_result;
		const int hand = reloading_hand; // the reloading hand
		const char* const zone_names[]{ "none", "magazine", "handle", "chest" };
		const char* const palm_names[]{ "none", "ready", "caught" };
		char zones[640];
		std::snprintf(zones, sizeof(zones),
			"\ngrip_lock=%d guided=%d rail=%.3f lock=%.2f/%.2f probe depth=%.3f off=%.3f deg=%.0f flags=%d"
			"\nzone=%s snap=%.2f/%.2f busy=%d racking=%d pull=%.3f magazine_zone=(%.3f,%.3f,%.3f) r=%.2f handle_zone=(%.3f,%.3f,%.3f) r=%.2f chest=(%.3f,%.3f,%.3f) palm=(%.3f,%.3f,%.3f) hand=%d"
			"\npalm_reload=%d palm_lock=%d palm_stage=%s tilt_deg=%.0f lean_deg=%.0f off_deg=%.0f gap=(%.3f,%.3f,%.3f) held_top=(%.3f,%.3f,%.3f)%s",
			grip_lock_on.load() ? 1 : 0, r.guided ? 1 : 0, r.rail_depth, r.hand_lock, lock_weight, r.rail_probe_depth, r.rail_probe_off, r.rail_probe_degrees, r.rail_probe_flags,
			zone_names[static_cast<int>(r.in_zone)], r.magazine_snap, snap_weight, r.off_hand_busy ? 1 : 0, r.racking ? 1 : 0, r.rack_pull, r.zone_centre[0].x, r.zone_centre[0].y,
			r.zone_centre[0].z, r.zone_reach[0], r.zone_centre[1].x, r.zone_centre[1].y, r.zone_centre[1].z, r.zone_reach[1], r.zone_centre[2].x,
			r.zone_centre[2].y, r.zone_centre[2].z, points.palm[hand][0], points.palm[hand][1], points.palm[hand][2], hand, palming ? 1 : 0,
			r.palm_locked ? 1 : 0, palm_names[static_cast<int>(r.palm)], r.magazine_tilt * 57.29578f, r.palm_misalign_degrees, r.palm_off_degrees, r.palm_gap.x, r.palm_gap.y, r.palm_gap.z,
			points.held_magazine[0], points.held_magazine[1], points.held_magazine[2], points.has_held ? " fist" : " none");
		char rack[400];
		float gun_handle[3]{};
		float gun_grab[3]{};
		const bool per_gun = gun_points.load(std::memory_order_relaxed) && found != slots.end();
		const bool handle_measured = per_gun && handle_point_for(found->second.name, gun_handle);
		const bool grab_measured = per_gun && magazine_grab_for(found->second.name, gun_grab);
		float way_out_degrees = 0.0f;
		const bool way_out_measured = per_gun && magazine_way_out_for(found->second.name, way_out_degrees);
		std::snprintf(rack, sizeof(rack), "\nrack underhand=%d under_zone=(%.3f,%.3f,%.3f) radii=(%.2f,%.2f,%.2f) handle_in_gun=%s(%.3f,%.3f,%.3f) magazine_grab=%s(%.3f,%.3f,%.3f) way_out=%s(%.0f deg)",
			r.rack_underhand ? 1 : 0, r.under_centre.x, r.under_centre.y, r.under_centre.z, r.under_radii.x, r.under_radii.y, r.under_radii.z,
			!handle_measured ? "variant-default" : handle_point_from_model(found->second.name) ? "model" : "measured", gun_handle[0], gun_handle[1], gun_handle[2],
			grab_measured ? "measured" : "default", gun_grab[0], gun_grab[1], gun_grab[2], way_out_measured ? "measured" : "straight", way_out_degrees);
		char touch[256];
		std::snprintf(touch, sizeof(touch), "\naccessory_touch=%d busy=%d toggles=%llu optic=(%.3f,%.3f,%.3f)%s rail=(%.3f,%.3f,%.3f)%s",
			touch_enabled ? 1 : 0, accessory_busy ? 1 : 0, static_cast<unsigned long long>(accessory_toggles), points.optic[0], points.optic[1],
			points.optic[2], points.has_optic ? "" : " none", points.rail[0], points.rail[1], points.rail[2], points.has_rail ? "" : " none");
		char text[768];
		if (found == slots.end())
		{
			std::snprintf(text, sizeof(text), "reload=%s active=0 (no magazine weapon in hand, or not in a level)", physical ? "physical" : "button");
			return text + std::string(touch);
		}
		const reload& gun = found->second.gun;
		const gun_state& s = gun.state();
		int length = std::snprintf(text, sizeof(text),
			"reload=%s active=%d weapon=%s slot=%d variant=%s capacity=%d\n"
			"magazine_in=%d magazine=%d chamber=%d bolt_locked=%d holding=%d held=%d palmed=%d reserve=%d game_written=%d shots=%llu spares=%d rate=%.1f frame_ms=%.1f\n"
			"magwell=(%.3f,%.3f,%.3f) slide=(%.3f,%.3f,%.3f) off_hand=(%.3f,%.3f,%.3f) points_valid=%d\nrecent:",
			physical ? "physical" : "button", active ? 1 : 0, found->second.name.c_str(), current_slot, gun.profile().kind == variant::ak ? "AK" : "AR",
			gun.profile().capacity, s.magazine_in ? 1 : 0, s.magazine_rounds, s.chamber ? 1 : 0, s.bolt_locked ? 1 : 0, s.holding ? 1 : 0,
			s.held_rounds, s.palmed ? s.palmed_rounds : -1, s.reserve, found->second.written, static_cast<unsigned long long>(shots), spares_now,
			rounds_per_second, frame_seconds_smooth * 1000.0f, points.magazine[0], points.magazine[1],
			points.magazine[2], points.slide[0], points.slide[1], points.slide[2], points.hand[hand][0], points.hand[hand][1], points.hand[hand][2],
			points.valid ? 1 : 0);
		for (int i = 0; i < recent_count && length > 0 && length < static_cast<int>(sizeof(text)); ++i)
		{
			length += std::snprintf(text + length, sizeof(text) - length, " %s", recent[i]);
		}
		char belt[320] = "";
		if (gun.profile().belt || gun.profile().revolver)
		{
			std::snprintf(belt, sizeof(belt), "\nbelt lid_open=%d lid=%.2f held=%d belt_laid=%d belt_held=%d points=%d lid_grab=(%.3f,%.3f,%.3f) tray=(%.3f,%.3f,%.3f) belt_end=(%.3f,%.3f,%.3f)",
				s.lid_open ? 1 : 0, last_result.lid, last_result.lid_held ? 1 : 0, s.belt_laid ? 1 : 0, s.belt_held ? 1 : 0, points.has_belt_feed ? 1 : 0,
				points.lid_grab[0], points.lid_grab[1], points.lid_grab[2], points.feed_tray[0], points.feed_tray[1], points.feed_tray[2],
				points.belt_end[0], points.belt_end[1], points.belt_end[2]);
		}
		return text + std::string(zones) + rack + touch + belt;
	}
}
