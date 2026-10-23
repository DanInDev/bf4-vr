#pragma once

#include <cstdint>

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

	// What the game was told to render for the most recent view copy (alternate-eye rendering).
	struct rendered_view
	{
		bool stereo{};      // false: copy left untouched (no tracking yet)
		int eye{};          // 0 = left, 1 = right
		pose eye_pose;      // eye pose used, OpenXR LOCAL space
		fov rendered_fov;   // symmetric FOV the game rendered with
		std::uint64_t copy_index{};
	};

	void publish_rendered_view(const rendered_view& view);
	rendered_view latest_rendered_view();
}
