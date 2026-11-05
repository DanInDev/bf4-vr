#pragma once

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
}
