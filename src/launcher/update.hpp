#pragma once

#include <string>
#include <string_view>

// The launcher's update check (#17): asks GitHub for the latest release of the mod, only when the user
// clicks "Check for updates" (nothing is sent otherwise).
namespace bfvr::launcher
{
	// This build's version (CMake project version).
	std::string_view mod_version();

	struct release
	{
		bool ok{};
		std::string tag;   // "v0.2.0"
		std::string url;   // the release page
		std::string error; // when !ok
	};

	// GET api.github.com/repos/DanInDev/bf4-vr/releases/latest (blocking, a few seconds at most).
	release latest_release();

	// The tag_name and html_url of a GitHub release JSON object.
	release parse_release(std::string_view json);

	// Dotted versions, a leading "v" ignored, missing parts 0: negative when a < b, 0 equal, positive.
	int compare_versions(std::string_view a, std::string_view b);
}
