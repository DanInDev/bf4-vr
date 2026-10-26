#pragma once

#include <cstddef>
#include <vector>

// Engine-free: turns a constant block written for the centre camera into the second eye's block.
// The per-view constant blocks hold copies of the RenderView's matrices (view-projection and its
// transpose, re/notes/stereo-rendering.md); per-draw blocks hold only world matrices. Every
// 16-byte aligned window of the RenderView that changes when the camera moves to the second eye is
// a replacement candidate. Tested by tests/eye_math_test.cpp.
namespace bfvr::stereo
{
	class constant_patch
	{
	public:
		void clear();
		// centre and eye: the same RenderView (floats, 16-byte aligned start) before and after the move.
		void add_view(const float* centre, const float* eye, std::size_t floats);
		// Replaces every 16-byte aligned 16-float window (matrices) and float3 (positions) of the
		// block that equals a centre value; returns the number of replacements.
		int apply(float* block, std::size_t floats) const;
		// Every 16-byte aligned view-projection built with the game's projection (x/y scales sx, sy,
		// infinite far plane) belongs to a camera that moves with the eye, whatever that camera is (the
		// world camera, or the first-person camera the gun and arms are drawn with). Moving the camera
		// by d along its own right axis changes only the clip-x translation, by -d * sx. Handles both
		// storage orders; pure projections (no translation) are left alone. Returns the count.
		static int shift_view_projections(float* block, std::size_t floats, float sx, float sy, float d);
		// Per-view constant block (512 bytes, re/notes/stereo-rendering.md): view matrix rows at +0x30,
		// projection at +0x70, view-projection at +0xB0, rotation-only view-projection at +0xF0,
		// (sx, sy, 1, near) at +0x130, camera position at +0x140. The first-person (gun and arms) camera
		// uses this layout with its own projection, so no RenderView value matches it. Recognised by
		// its own consistency (camera position = -R^T t, scales repeated); moves the camera by d along
		// its right axis. Returns true when the block was changed.
		static bool shift_per_view_block(float* block, std::size_t floats, float d);
		std::size_t matrices() const { return matrices_.size(); }
		std::size_t positions() const { return positions_.size(); }

	private:
		struct matrix
		{
			float centre[16];
			float eye[16];
		};
		struct position
		{
			float centre[3];
			float eye[3];
		};
		std::vector<matrix> matrices_;
		std::vector<position> positions_;
	};
}
