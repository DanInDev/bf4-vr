#pragma once

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
}
