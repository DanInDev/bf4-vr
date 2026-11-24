#pragma once

#include <span>
#include <string>
#include <string_view>

#include "common/settings.hpp"

// The live settings of this process (issue #25, docs/tasks/vr-settings.md): one registry, filled from
// bfvr.ini at start-up, changed through the pipe (`setting ...`) or a panel, saved back on request.
namespace bfvr::settings
{
	registry& global();

	// Registers every live setting and loads them from bfvr.ini; warnings go to bfvr.log. Call once.
	void load_global();

	// Pipe: list | get <name> | set <name> <value> | step <name> <+n|-n> | reset <name> | save
	std::string pipe_command(std::string_view line);

	// Writes the changed values into bfvr.ini (comments and other keys kept); "" or an error text.
	std::string save_global();

	// Every live setting's definition, for a registry of its own (the launcher edits bfvr.ini with one).
	std::span<const definition> live_definitions();
	// Keys the game reads once when it starts (common/config), as definitions: only the launcher
	// offers them; changing them in a running game does nothing.
	std::span<const definition> startup_definitions();
	// #150: VR features that are always on. Registered in the game (the pipe can set them for tests) but never read
	// from bfvr.ini, not on the panel and not in the launcher (not in live_definitions).
	std::span<const definition> fixed_definitions();
	bool is_fixed(std::string_view name);
}
