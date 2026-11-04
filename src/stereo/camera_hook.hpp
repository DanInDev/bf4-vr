#pragma once

#include <cstdint>
#include <string>

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

	// Apparent size of the first-person gun and arms (1 = the game's; config key gun_scale).
	void set_gun_scale(float scale);
	// Eye distance multiplier (world scale), 0.5..2; 1 = the runtime's IPD.
	void set_ipd_scale(float scale);
	float current_gun_scale();
	// gun_life_size: in head-tracked frames the gun and arms are scaled by tan(22.5 deg)/tan(fovY/2) on top
	// of gun_scale, so the foreground's fixed 45 degree FOV draws them at life size, on the controllers.
	void set_gun_life_size(bool on);
	// The row scale applied in the last frame (gun_scale times the life-size factor).
	float applied_gun_size();
	// Half the vertical FOV of the gun pass the life-size scale was last computed with (radians).
	float applied_gun_half_fov();

	// Fraction (tangent space, 0.4..1) of the headset's reported FOV rendered per eye, horizontal and
	// vertical; config view_crop_h / view_crop_v. Smaller = sharper, but too small shows black edges.
	void set_view_crop(float horizontal, float vertical);
	// steady_walk: the walk animation's sideways camera sway removed (stereo/steady_camera.hpp).
	void set_steady_walk(bool enabled);
	void current_view_crop(float& horizontal, float& vertical);

	// Research: world-view aspect (width/height) handed to the engine instead of the viewport's,
	// when not in stereo (0 = the game's own).
	void set_research_aspect(float value);

	// Research: a synthetic head yaw (degrees, 0 = off) applied like a headset eye pose when not
	// in stereo; world and first-person camera turn together.
	// stereo_render at runtime: "pair" (both eyes from one frame, eye pass) or "aer" (alternate eyes).
	std::string set_stereo_render(const std::string& name);
	void set_fake_yaw(float degrees);

	// Live setting cutscene_view: while the game moves the camera itself (stereo/scripted_view:
	// cutscenes, the death fall), show it on the theater screen (true) or keep head-tracked stereo
	// so the player can look around in the scene (false).
	void set_cutscene_screen(bool screen);
	// Pipe "cutscene": the detector's state and inputs.
	std::string cutscene_report();
	// Live setting physical_stance (#33): crouching or lying down in the room changes the game's
	// stance (input/physical_stance), and the view follows the real head only (the game's own eye
	// drop for its stance is added back). Switching it on measures standing afresh.
	void set_physical_stance(bool enabled);
	// Pipe "stance".
	std::string stance_report();

	// The game camera's own motion before the head pose is applied (stick turning and aiming,
	// walking, vehicles): yaw rate (rad/s) and speed (m/s), smoothed; 0 outside head-tracked frames.
	// Drives the comfort vignette (#10).
	void body_motion(float& yaw_rate, float& speed);

	// How the hook re-aimed the first-person camera this frame (controller aim): turned by yaw radians
	// about soldier up (atan2(look.x, look.z) sense) and levelled, instead of the game's aimer yaw and
	// pitch. The arm IK must place the hands in this rendered frame. active = false: the game's own.
	struct first_person_turn
	{
		bool active{};
		float yaw{};
	};
	first_person_turn first_person_view_turn();
}
