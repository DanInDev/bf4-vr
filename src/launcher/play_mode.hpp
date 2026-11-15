#pragma once

#include <filesystem>

#include "launcher/install.hpp"

// Normal and VR play, the Play page's switch: each mode keeps its own copy of BF4's settings profile
// beside it (PROFSAVE_profile.bfvr-normal and .bfvr-vr, plain text, editable), and the live profile is
// the current mode's. Campaign progress (PROFSAVE) is never touched. The caller installs or removes the mod.
namespace bfvr::launcher
{
	enum class play_mode { normal, vr };

	std::filesystem::path mode_copy(const std::filesystem::path& profile, play_mode mode);
	// "normal" or "vr": which mode's settings the live profile holds.
	std::filesystem::path mode_marker(const std::filesystem::path& profile);

	// From the marker; before the first switch there is none: VR while the mod is installed.
	play_mode current_mode(const std::filesystem::path& profile, bool mod_installed);

	// Keeps the live profile as the current mode's copy, then makes the target's copy live.
	// Entering VR without a VR copy yet sets the VR values (vr_setup) on the normal settings; with
	// set_vr_values (Set up for VR) they are set in any case. Entering normal without a normal copy
	// takes the backup an older launcher kept, else leaves the profile as it is.
	// Refused while the game runs: BF4 writes the profile back on exit.
	result switch_profile(const std::filesystem::path& profile, play_mode current, play_mode target, bool game_running,
		bool set_vr_values = false);
}
