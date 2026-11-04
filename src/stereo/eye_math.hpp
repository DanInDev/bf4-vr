#pragma once

#include <cstdint>

#include "stereo/tracking.hpp"

// Engine-free per-eye maths, shared by the camera hook and the per-eye pass, tested by
// tests/eye_math_test.cpp. No game, D3D or OpenXR dependency may be added here.
namespace bfvr::stereo::eye_math
{
	// Composes an OpenXR LOCAL-space eye pose onto a game camera: 4 rows (right, up, forward,
	// position) of float4, row vectors (world = local * M). The camera's local axes match
	// OpenXR's (x right, y up, looking along -z).
	void compose_pose(float* camera, const pose& eye);

	// The symmetric frustum the game renders: covers both eyes' FOVs, cropped in tangent space
	// (crop 1 = all of the runtime's FOV). With headset_aspect the aspect is the cropped
	// tan(h)/tan(v) the engine is told to use; otherwise the game's aspect is kept (aspect 0) and
	// the horizontal half angle follows from it.
	struct render_fov
	{
		float half_horizontal{};
		float half_vertical{};
		float aspect{}; // 0 = keep the game's
	};
	render_fov symmetric_fov(const fov (&fovs)[2], float crop_h, float crop_v, bool headset_aspect, float game_aspect);

	// Crops the bottom of a symmetric frustum by `bottom` radians and keeps its top: the half vertical
	// angle shrinks by bottom/2 and the camera pitches up by `tilt` = bottom/2. The same lines then
	// cover only what the lens shows (Quest 3 via Steam Link: about +44 / -30 degrees). With the
	// headset aspect the engine aspect follows; the horizontal half angle is unchanged.
	struct tilted_fov
	{
		render_fov fov;
		float tilt{}; // radians, camera pitched up about its own x axis
	};
	tilted_fov crop_bottom(const render_fov& symmetric, float bottom, bool headset_aspect);

	// The pose turned about its own x axis (right) by radians; positive pitches -z (forward) up.
	pose pitched(const pose& eye, float radians);

	// Eye midpoint with the left eye's orientation: what once-per-frame consumers (arm IK)
	// use when both eyes are rendered from one head sample.
	pose centre_pose(const head_sample& head);

	// The pose moved by metres along its own right axis (+x), orientation unchanged: the right eye as
	// the eye pass renders it from the left eye.
	pose moved_along_right(const pose& eye, float metres);

	// Distance between the two eye positions (the interpupillary distance the runtime reports).
	float eye_distance(const head_sample& head);

	// Angle (radians) between the two eye orientations. The per-eye path assumes parallel eyes
	// (one head rotation, per-eye positions): Overlord rejects canted eye cameras the same way.
	float eye_angle(const head_sample& head);

	// One frame's eyes, both from the same head sample.
	struct view_family
	{
		std::uint64_t frame{};
		head_sample head;
	};

	// Complete pairs only: both eyes must come from the same family before the pair may be
	// submitted. A stale or repeated eye is rejected and counted, never used to fill a gap.
	class eye_pair
	{
	public:
		void begin(std::uint64_t frame);
		// false when the eye is out of range, belongs to another frame or was already rendered.
		bool rendered(int eye, std::uint64_t frame);
		bool complete() const { return active_ && done_[0] && done_[1]; }
		std::uint64_t frame() const { return frame_; }
		std::uint64_t rejected() const { return rejected_; }
		// Frames begun again before both eyes were rendered.
		std::uint64_t abandoned() const { return abandoned_; }

	private:
		std::uint64_t frame_{};
		bool active_{};
		bool done_[2]{};
		std::uint64_t rejected_{};
		std::uint64_t abandoned_{};
	};
}
