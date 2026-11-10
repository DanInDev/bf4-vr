#pragma once

#include "stereo/tracking.hpp"

// Engine-free maths for magnified sights (stereo/scope), tested by tests/scope_test.cpp. No game, D3D
// or OpenXR dependency may be added here. Poses are OpenXR LOCAL (x right, y up, looking along -z).
namespace bfvr::stereo::scope_math
{
	// How much the game magnifies while aiming: tan(hip/2) / tan(aim/2) of its vertical world FOV
	// (radians); 1 when either value is out of range or the aim FOV is not narrower.
	float magnification(float hip_fov_y, float aim_fov_y);

	struct lens
	{
		float distance{ 0.07f }; // lens centre ahead of the sight's eye point, along the sight (metres)
		float radius{ 0.012f };  // metres
		float crop{ 0.3f };      // fraction of the scope render's height the lens circle covers
	};

	struct plan
	{
		bool valid{};
		pose camera;           // the scope camera: at the sight's eye point, -z along the sight, up = the gun's up
		float lens_fov_y{};    // the world angle the lens shows (radians, full)
		float render_fov_y{};  // the scope render's vertical FOV (the lens circle covers lens.crop of it)
		pose quad;             // the lens: centre and orientation (faces the eye)
		float quad_size{};     // lens diameter (metres)
	};

	// sight_eye: the sight's eye point; rotation: the gun's Wep_Root rotation (x y z w); sight: the
	// sight line in Wep_Root axes (+z = barrel); eye: the eye that looks through it. The lens subtends
	// atan(radius / distance from the eye); it shows that angle divided by magnification.
	plan plan_scope(const pose& eye, const float (&sight_eye)[3], const float (&rotation)[4], const float (&sight)[3], float magnification, const lens& optic);

	// #39: the game's magnification per weapon and zoom level, learned while it aims (its world FOV is the
	// only place it shows), so the lens picture can run at the hip with the same zoom. Unknown levels
	// answer the fallback.
	class magnification_memory
	{
	public:
		void remember(const char* weapon, int level, float magnification);
		float lookup(const char* weapon, int level, float fallback) const;
		int size() const { return count_; }

	private:
		static constexpr int capacity = 32;
		struct entry
		{
			char weapon[48]{};
			int level{};
			float magnification{};
		};
		entry entries_[capacity]{};
		int count_{};
	};

	// to expressed in from's frame (from^-1 * to).
	pose relative_pose(const pose& from, const pose& to);

	// A game camera's rows (right, up, back, position as float4 rows, world = local * M; the camera
	// axes match OpenXR's) moved by a pose given in that camera's frame.
	void compose_rows(const float (&camera)[16], const pose& relative, float (&out)[16]);
}
