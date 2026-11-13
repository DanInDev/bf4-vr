#pragma once

#include <cstdint>
#include <string>

// The player's ServerSoldierEntity. Single player runs the server in-process, and its weapons hold the
// authoritative ammo (physical reload) and grenade counts (physical grenade). BF4 179547 keeps no stable
// pointer to it: the static soldier tables hold the spawned soldiers in an order that changes between
// sessions and respawns (the player was at 0x142311DB8 in one session and 0x142311E08 in the next, and
// 0x142311DB8 was null). Read from a fixed slot, the mod lost the soldier after a respawn and the game's own
// reload came back (re/notes/weapon.md "The player's server soldier"). So every candidate is checked by its
// vtable and by standing where the local client soldier stands.
namespace bfvr::weapon
{
	struct soldier_candidate
	{
		std::uint64_t address{}; // 64-bit on every preset (the bf3 build compiles this too)
		bool typed{};          // its vtable is ServerSoldierEntity's
		bool placed{};         // its position could be read
		float position[3]{};   // ServerSoldierEntity +0x720
	};

	// Engine-free: the typed candidate nearest the client soldier's feet, if within tolerance (metres); 0 none.
	std::uint64_t choose_server_soldier(const soldier_candidate* candidates, int count, const float (&client_feet)[3], float tolerance);

	// Engine-free: whether the soldier found before still counts. Positions differ by more than a metre for
	// moments while the player moves (live: found and lost about once a second in a fight), so it is kept
	// while it is still a ServerSoldierEntity and the client soldier is the one it was found for, unless it
	// stays more than drift_metres away for longer than drift_seconds (its object reused for someone else).
	// off_seconds: how long it has been that far off (the caller keeps it between calls).
	constexpr float drift_metres = 3.0f;
	constexpr float drift_seconds = 1.5f;
	bool keep_server_soldier(bool typed, bool same_client, float distance, float frame_seconds, float& off_seconds);

	// Any thread: the player's server soldier, or 0 (no level, a cutscene without a soldier, not found yet).
	// The last answer is kept while keep_server_soldier says so; otherwise the static tables are tried,
	// and if none of them holds the player either, a scan of the heap for ServerSoldierEntity objects runs in
	// the background (at most every 3 s) and its finds are tried on the next calls.
	std::uintptr_t player_server_soldier();

	// Pipe "serversoldier": the current answer, how it was found, and the counters.
	std::string server_soldier_report();
}
