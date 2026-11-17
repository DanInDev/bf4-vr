#pragma once

#include <cstdint>
#include <string>

#include "weapon/reload.hpp"

// The game's own reload sounds at the physical reload's steps (#30, re/notes/sound.md). Each gun
// has a reload SoundEntity whose patch (assault rifles: Weapon_SourcePatch_Reload_AR) plays one
// stage per graph input event; the game's reload animation sends those events, a physical reload
// sends them through the entity's event handler instead. Where a gun's entity is not found, the
// synthesised cues of weapon/reload_sounds play instead.
namespace bfvr::weapon
{
	// Graph input events of Weapon_SourcePatch_Reload_AR (engine string hash; matched against the
	// game's own reloads by recording, re/notes/sound.md).
	namespace reload_sound
	{
		constexpr std::uint32_t magazine_out = 0x9D874880;
		constexpr std::uint32_t magazine_in = 0x0C87FAA9;
		constexpr std::uint32_t magazine_seat = 0x95E4238C; // follows magazine_in by ~0.4 s
		constexpr std::uint32_t bolt = 0x7C80C030;           // "Bolt": charging handle / bolt release
		constexpr std::uint32_t settle = 0xC46CF22F;         // closing foley at the end of a reload
	}

	// The other reload patches' events, matched by recording each event against the game's own reload of a gun using
	// that patch and its bone timeline (tools/sound_probe.py game_empty / stages / match; re/notes/sound.md "Census").
	namespace reload_sound
	{
		constexpr std::uint32_t pump_shell_in = 0x2519DEEC;   // Shotgun_PumpAction: a shell into the tube (0.98-0.99)
		constexpr std::uint32_t pump_back = 0x71B0C476;       // ... the pump's first stroke back (0.98)
		constexpr std::uint32_t sniper_bolt_back = 0x9FA31A86; // Sniper_BoltAction: the bolt worked (0.99)
		constexpr std::uint32_t lmg_lid_open = 0x3A3332B0;    // LMG: as the lid opens (0.94)
		constexpr std::uint32_t lmg_lid_close = 0x27EE4912;   // LMG: as the lid shuts (1.00)
		constexpr std::uint32_t pistol_close = 0x6F002904;    // Pistol (MP-412 REX): the frame shut (0.97)
		constexpr std::uint32_t tube5_shell_in = 0x0002B591;  // Reload_5 (UTAS): a shell in (0.98)
		constexpr std::uint32_t tube5_pump = 0x0002B597;      // Reload_5: the pump (0.99)
	}

	// Which reload patch a gun's sound set uses (its patch asset name); each has its own events.
	enum class reload_patch
	{
		ar,     // Weapon_SourcePatch_Reload_AR, and anything unrecognised
		pump,   // Weapon_SourcePatch_Reload_Shotgun_PumpAction
		sniper, // Weapon_SourcePatch_Reload_Sniper_BoltAction
		lmg,    // Weapon_SourcePatch_Reload_LMG
		pistol, // Weapon_SourcePatch_Reload_Pistol
		tube5,  // Weapon_SourcePatch_Reload_5 (UTAS; the launchers)
	};
	reload_patch patch_kind_of(const std::string& patch_name);

	struct sound_cue
	{
		std::uint32_t event{};
		unsigned delay_ms{};
	};

	// Up to two cues for a reload event (the AR patch's); returns how many were written.
	int cues_for(reload_event event, sound_cue (&cues)[2]);
	// The same for a given patch: its own events where it has them (a shell, a lid, the revolver's frame).
	int cues_for(reload_event event, reload_patch patch, sound_cue (&cues)[2]);
	// The rear stop of a rack or pump stroke in this patch's own sound, 0 if it has none (the synthesised one plays).
	std::uint32_t rear_stop_event(reload_patch patch);
	// Present thread: the rear stop in the game's sound for the gun in hand; false: play the synthesised one.
	bool play_game_rear_stop();

	// The gun's sound folder from one of its sound asset names (its firing sound,
	// "sound/Weapons/Handheld/Remington870/Weapon_Shotgun_Remington870_SPC" -> "Remington870"); empty if none. The
	// folder, not the unlock name, names the reload set (2026-11-17: U_870, U_AK5C, U_M16A4 ... were looked up under the
	// wrong name and played synthesised cues; re/notes/sound.md "Census").
	std::string sound_folder_of(const char* asset_name);
	// A reload sound set of this folder: "/<folder>/" and "_Reload_SPC" anywhere in the name, ignoring case, not an AI one.
	bool reload_set_belongs(const char* asset_name, const std::string& folder);
	// Whether a patch with these graph input events can play this cue (a cue the patch does not have plays nothing:
	// the synthesised one plays instead).
	bool patch_has_event(const std::uint32_t* events, int count, std::uint32_t event);

	// Game side (x64; nothing on BF3).
	void set_game_reload_sounds(bool enabled);
	// Present thread, every frame with the gun in hand ("U_AK12") and its sound folder (sound_folder_of its firing
	// sound; empty: the unlock name without "U_"): finds its reload SoundEntity in the background when the gun changes.
	void game_reload_sounds_weapon(const char* weapon_name, const char* sound_folder = "");
	// Present thread: queue the game's cues of a physical reload event; false when this gun's reload
	// entity is not known (or game sounds are off) or the event has no game cue: play a synthesised one.
	bool play_game_reload_sound(reload_event event);
	// Game thread (view-params copy): sends due cues to the entity.
	void game_reload_sounds_on_game_thread();
	// Pipe "reload" appends this: entity, lookups, cues sent.
	const char* game_reload_sounds_report();
}
