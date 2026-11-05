#pragma once

// The weapon's shot frame (ClientWeapon +0x40, re/notes/weapon.md "Where shots start"): 4x4 rows right,
// up, forward, position, in the world, set every frame at the camera along the aim. With controller aim
// the gun is elsewhere: this builds the frame at the gun's sight line instead. Engine-free; tested by
// tests/shot_frame_test.cpp.
namespace bfvr::weapon
{
	struct shot_line_local
	{
		float start[3]{};     // OpenXR LOCAL (x right, y up, z back), metres from the tracking origin
		float direction[3]{}; // unit
	};

	// game: the frame the game set (at the camera joint). body_yaw: the rendered body's camera yaw
	// (atan2(look.x, look.z), the LOCAL frame's turn in the world). lift: what physical stance adds to the
	// tracking origin's height. max_reach: a start further than this from the camera is pulled in (never
	// through a wall the gun pokes into). The rows keep the game's handedness: they turn by the shortest
	// rotation from its forward to ours.
	void shot_frame_at_gun(const float (&game)[16], float body_yaw, float lift, const shot_line_local& line, float max_reach, float (&out)[16]);
}
