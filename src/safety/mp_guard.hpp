#pragma once

namespace bfvr::safety
{
	// The mod is campaign-only (docs/SAFETY.md). False when the game's command line asks for anything
	// but the campaign (safety/mp_rules); the runtime is then never started.
	bool command_line_allows_mod();

	// Watches the loaded level (BF4 build 179547, re/symbols/bf4-179547.md; BF3 build 1147186,
	// bf3-vr re/symbols). Unless it is a campaign level or the front end (safety/mp_rules), all hooks
	// are disabled for the rest of the process and mod_disabled() turns true. Removing dinput8.dll
	// before multiplayer stays the primary rule.
	void start_multiplayer_guard();
	bool mod_disabled();

	// True while a campaign level is loaded and running (not the front end, not a loading screen,
	// whose level path is briefly empty). Head-tracked stereo is used only then; menus and loading
	// screens stay on the theater screen.
	bool campaign_level_loaded();
}
