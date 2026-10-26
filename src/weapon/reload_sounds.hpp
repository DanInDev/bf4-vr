#pragma once

#include <cstdint>

#include "weapon/reload.hpp"

// The game's own reload sounds at the physical reload's steps (#30, re/notes/sound.md). Each gun
// has a reload SoundEntity whose patch (assault rifles: Weapon_SourcePatch_Reload_AR) plays one
// stage per graph input event; the game's reload animation sends those events, a physical reload
// sends them through the entity's event handler instead.
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

	struct sound_cue
	{
		std::uint32_t event{};
		unsigned delay_ms{};
	};

	// Up to two cues for a reload event; returns how many were written.
	int cues_for(reload_event event, sound_cue (&cues)[2]);

	// Game side (x64; nothing on BF3).
	void set_reload_sounds(bool enabled);
	// Present thread, every frame with the gun in hand ("U_AK12"): finds its reload SoundEntity in
	// the background when the gun changes.
	void reload_sounds_weapon(const char* weapon_name);
	// Present thread: queue the cues of a physical reload event.
	void play_reload_sound(reload_event event);
	// Game thread (view-params copy): sends due cues to the entity.
	void reload_sounds_on_game_thread();
	// Pipe "reload" appends this: entity, lookups, cues sent.
	const char* reload_sounds_report();
}
