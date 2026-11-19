#pragma once

#include <cstdint>
#include <string>

#include "stereo/tracking.hpp"

// Magnified sights (live setting scope): while the game aims down a sight that zooms its world view
// (the AK-12's flip magnifier: 45 -> 27 deg), the world is rendered a third time from the sight, along
// the sight line, with the lens's narrow FOV (stereo/eye_pass), and that picture is shown on the lens
// as a quad (xr/theater). The world view itself keeps the headset's FOV.
namespace bfvr::stereo
{
	// Game thread, each view copy, before the headset's FOV replaces the game's: game_fov_y is the
	// vertical world FOV the game asked for (radians), rendered_eye the eye this copy renders (LOCAL).
	void scope_on_view_copy(float game_fov_y, const pose& rendered_eye, const head_sample& head);

	struct scope_request
	{
		bool active{};
		pose relative;         // the scope camera in the rendered eye's frame
		float render_fov_y{};  // radians
		float crop{};          // fraction of the render's height the lens circle covers
		pose quad;             // the lens, OpenXR LOCAL
		float quad_size{};     // metres
		float magnification{};
		int eye{};             // the eye behind the sight (0 = left): only it sees the lens picture
		std::uint64_t at{};    // GetTickCount64
	};
	// The latest request; inactive when not aiming through a magnifying sight or the gun is not tracked.
	scope_request latest_scope_request();

	// #139: the held optic's selected level as the game's data has it (weapon::zoom_power, else the learned zoom or the
	// name table): 1 for a red dot or holo, the magnification for a scope or a flipped magnifier; 0 when unknown.
	// Any thread (the eye pass's lens glass reads it).
	float optic_power_now();

	// Pipe "scope": the game FOVs, magnification, request and the eye pass's scope renders.
	std::string scope_report();
}
