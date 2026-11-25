#pragma once

#include <cstdint>

#include "stereo/stereo_hold.hpp"

namespace bfvr::stereo
{
	// Plain copies of OpenXR poses/FOVs so game-side code needs no OpenXR headers.
	// Positions in metres, OpenXR LOCAL space (x right, y up, -z forward).
	struct pose
	{
		float position[3]{};
		float orientation[4]{ 0, 0, 0, 1 }; // x, y, z, w
	};

	struct fov
	{
		float left{}, right{}, up{}, down{}; // radians, left/down negative
	};

	// Latest headset view sample, published by the XR frame thread.
	struct head_sample
	{
		bool valid{};
		pose eyes[2];  // 0 = left, 1 = right
		fov fovs[2];
	};

	void publish_head_sample(const head_sample& sample);
	head_sample latest_head_sample();

	// The floor's height in OpenXR LOCAL (y of the STAGE space's origin), when the runtime has a STAGE space
	// (the Quest's guardian floor through SteamVR); for the body calibration (#46).
	void publish_floor(bool valid, float y);
	bool latest_floor(float& y);

	// Controller poses for the same display time as the head sample, OpenXR LOCAL space.
	// grip: palm centre, -z along the handle; aim: pointing ray, -z forward.
	struct hand_sample
	{
		bool valid[2]{}; // 0 = left, 1 = right; position and orientation both tracked
		pose grip[2];
		pose aim[2];
	};

	void publish_hand_sample(const hand_sample& sample);
	hand_sample latest_hand_sample();

	// What the game was told to render for the most recent view copy (alternate-eye rendering).
	struct rendered_view
	{
		bool stereo{};      // false: copy left untouched (no tracking yet)
		int eye{};          // 0 = left, 1 = right
		pose eye_pose;      // eye pose used, OpenXR LOCAL space
		fov rendered_fov;   // symmetric FOV the game rendered with
		std::uint64_t copy_index{};
		// stereo_render = pair: this copy is the left eye of a frame whose right eye the eye pass
		// renders (stereo/eye_pass.hpp); second_pose is that right eye as rendered.
		bool pair{};
		pose second_pose;
		mono_reason mono{ mono_reason::off }; // why stereo is false (#163)
	};

	void publish_rendered_view(const rendered_view& view);
	rendered_view latest_rendered_view();
}
