#pragma once

#include <vector>

// Bone palettes of the game's skinned draws (extra meshes, #36): 64 entries of a 3x4 matrix, rows
// (rotation * scale | translation). Engine-free; tested by tests/palette_copy_test.cpp.
namespace bfvr::stereo::palette
{
	constexpr int entries = 64;
	constexpr int floats_per_entry = 12;

	// The entry's bone has this scale: its first row is that long (a tagged bone; rows are rotation * scale).
	bool tagged(const float* entry, float tag, float tolerance = 4e-4f);

	// For a copy that draws only the tagged bones moved: tagged entries become d * entry, every other entry
	// all zeros (its vertices collapse to a point: nothing drawn). Returns how many entries were tagged.
	int move_tagged(float* palette, const float (&d)[12], float tag, float tolerance = 4e-4f);

	// out = d * entry (3x4 rows, the implied fourth row 0 0 0 1).
	void multiply(const float (&d)[12], const float* entry, float* out);

	// An entry whose root bone was at root (no rotation) with scale tag, made relative to that root:
	// [R | t] becomes [R / tag | (t - root) / tag], so d * entry later puts the root at d.
	void relative_to_root(float* entry, float tag, const float (&root)[3]);

	// #41, a knife capture: a draw also seen with the gun is left out, unless a kept draw not seen with the gun shares
	// its index buffer (then it is the knife's own pre-pass: the level can start with the knife out, so its draws were
	// listed among the gun's). All three lists run over the capture's draws; buffers are compared by value.
	std::vector<bool> keep_knife_draws(const std::vector<bool>& keep, const std::vector<bool>& seen_with_gun, const std::vector<const void*>& buffers);
}
