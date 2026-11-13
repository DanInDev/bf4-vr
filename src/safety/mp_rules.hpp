#pragma once

#include <string>

// The campaign-only rules (docs/SAFETY.md) as plain decisions on text, so they can be tested without
// the game. mp_guard feeds them the process command line and the path of the loaded level.
namespace bfvr::safety
{
	enum class game
	{
		bf4,
		bf3,
	};

	// The game this build targets (BFVR_GAME_NAME).
	game built_game();

	// Why the mod must stay off for this command line, or null when it may run.
	// BF4: off when the line asks to join or claim a server. BF3: every mode is its own launch from
	// Battlelog, so the line must ask for single player (-webMode SP) and nothing else.
	const char* command_line_refusal(game target, const std::string& command_line);

	enum class level_kind
	{
		none, // no level loaded (loading screens)
		front_end, // menus: allowed, the mod waits
		campaign, // allowed
		forbidden, // multiplayer, co-op or anything unknown: the mod switches itself off for good
	};
	level_kind judge_level(game target, const std::string& path);
}
