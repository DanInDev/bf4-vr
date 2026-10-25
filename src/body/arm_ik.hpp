#pragma once

namespace bfvr::body::ik
{
	// Rig math for the first-person skeleton. Quaternions are Hamilton (x, y, z, w) and rotate
	// column vectors (v' = q v q*); a model transform is parent * local.
	struct vec3
	{
		float x{}, y{}, z{};
	};

	struct quat
	{
		float x{}, y{}, z{}, w{ 1 };
	};

	// Engine pose element (QuatTransform, 0x20 bytes): translation, uniform scale, rotation.
	struct bone_transform
	{
		vec3 position;
		float scale{ 1 };
		quat rotation;
	};
	static_assert(sizeof(bone_transform) == 0x20);

	vec3 operator+(vec3 a, vec3 b);
	vec3 operator-(vec3 a, vec3 b);
	vec3 operator*(vec3 a, float s);
	float dot(vec3 a, vec3 b);
	vec3 cross(vec3 a, vec3 b);
	float length(vec3 a);
	vec3 normalize(vec3 a);

	quat operator*(quat a, quat b);
	quat conjugate(quat q);
	quat normalize(quat q);
	vec3 rotate(quat q, vec3 v);
	// Shortest rotation taking direction from onto direction to.
	quat from_to(vec3 from, vec3 to);

	// Model-space transform of one bone: walks the parent chain (parents[i] < i, root -1).
	bone_transform model_of(const bone_transform* local, const int* parents, int bone);
	bone_transform compose(const bone_transform& parent, const bone_transform& local);
	// Inverse of a rigid transform with uniform scale: compose(t, inverse(t)) is the identity.
	bone_transform inverse(const bone_transform& t);
	// Rewrites local[bone] so the bone's model transform becomes model.
	void set_model_transform(bone_transform* local, const int* parents, int bone, const bone_transform& model);

	struct arm_bones
	{
		int upper{ -1 };   // shoulder joint (e.g. RightArm)
		int fore{ -1 };    // elbow joint (RightForeArm)
		int hand{ -1 };    // wrist joint (RightHand)
		int roll{ -1 };    // optional forearm twist bone between fore and hand (RightForeArmRoll)
	};

	struct arm_goal
	{
		vec3 wrist;     // model space
		quat hand;      // model-space rotation of the hand bone
		vec3 pole;      // model-space point the elbow bends towards
	};

	// Two-bone IK: rewrites the local rotations of upper, fore and hand so the wrist reaches
	// goal.wrist (or points at it when out of reach), the elbow bends towards goal.pole and the
	// hand takes goal.hand. With a roll bone, half of the change in wrist twist goes to it.
	// Bone lengths come from the current pose. Returns false (pose untouched) on a degenerate chain.
	bool solve_arm(bone_transform* local, const int* parents, const arm_bones& bones, const arm_goal& goal);
}
