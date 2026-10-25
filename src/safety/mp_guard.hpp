#pragma once

namespace bfvr::safety
{
	// The mod is campaign-only (docs/SAFETY.md). False when the game's command line asks to join or
	// claim a multiplayer server; the runtime is then never started.
	bool command_line_allows_mod();

	// Watches the loaded level (BF4 build 179547, re/symbols/bf4-179547.md). Unless it is a campaign
	// level (Levels/SP/...) or the front end, all hooks are disabled for the rest of the process and
	// mod_disabled() turns true. Removing dinput8.dll before multiplayer stays the primary rule.
	void start_multiplayer_guard();
	bool mod_disabled();
}
