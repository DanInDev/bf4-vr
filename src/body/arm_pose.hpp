#pragma once

namespace bfvr::body
{
	// Hooks the per-object pose conversion (local pose -> model pose and skinning data, BF4 build
	// 179547, re/notes/skeleton.md) so the local soldier's first-person arm bones can be edited after
	// the animation and before skinning. Waits for the DRM to decrypt the code; x64 only.
	bool install_arm_pose_hook();

	// Research: every evaluation, add (dx, dy, dz) metres to the local translation of one bone of the
	// first-person skeleton (index < 0 = off).
	void set_bone_offset(int bone, float dx, float dy, float dz);

	struct arm_pose_stats
	{
		unsigned long long conversions{}; // pose conversions seen (all animated objects)
		unsigned long long matched{};     // of those, the local soldier's first-person rig with an edit
	};
	arm_pose_stats stats();
}
