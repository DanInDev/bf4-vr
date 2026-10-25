#pragma once

#include <cstdint>

namespace bfvr::stereo
{
	struct camera_stats
	{
		unsigned long copy_thread{};
		std::uint64_t copies{};
	};
	camera_stats stats();

	// Hooks the per-frame copy of the game's view parameters into the renderer (BF4 build 179547,
	// see re/notes/camera.md) so the main camera can be shifted sideways before rendering.
	// Waits (in a background thread) until the game's code is decrypted and the function's bytes
	// match build 179547, then hooks; a different build is never hooked.
	bool install_camera_hook();

	// Sideways camera shift in metres along the camera's right vector (0 = untouched).
	void set_eye_offset(float metres);
	float eye_offset();

	// Research: odd copies get +amplitude, even copies -amplitude (0 = off).
	void set_alternating_offset(float amplitude);

	// Research: writes the candidate vertical-FOV field (radians, 0 = off) when not in stereo.
	void set_fov_override(float radians);

	// Research: per RenderView (0 world, 1 = +0x4E0, 2 = +0x9C0) FOV (radians, 0 = off) and
	// sideways shift (metres, 0 = off), applied when not in stereo.
	void set_view_override(int view, float fov_radians, float shift_metres);

	// Research: shifts the transform (rows right, up, forward, position) at a 16-byte-aligned
	// offset inside the view-params block along its right row, every copy (slot 0..3, 0 m = off).
	bool set_transform_shift(int slot, std::uint32_t offset, float metres);

	// Research: writes a float at a 4-byte-aligned offset in the view-params block every copy and
	// marks view 0 dirty (slot 0..3; offset 0 = off).
	bool set_pin(int slot, std::uint32_t offset, float value);

	// Research: world-view aspect (width/height) handed to the engine instead of the viewport's,
	// when not in stereo (0 = the game's own).
	void set_research_aspect(float value);

	// Research: a synthetic head yaw (degrees, 0 = off) applied like a headset eye pose when not
	// in stereo; world and first-person camera turn together.
	void set_fake_yaw(float degrees);
}
