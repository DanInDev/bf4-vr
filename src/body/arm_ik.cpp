#include "body/arm_ik.hpp"

#include <algorithm>
#include <cmath>

namespace bfvr::body::ik
{
	vec3 operator+(vec3 a, vec3 b) { return { a.x + b.x, a.y + b.y, a.z + b.z }; }
	vec3 operator-(vec3 a, vec3 b) { return { a.x - b.x, a.y - b.y, a.z - b.z }; }
	vec3 operator*(vec3 a, float s) { return { a.x * s, a.y * s, a.z * s }; }
	float dot(vec3 a, vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
	vec3 cross(vec3 a, vec3 b) { return { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x }; }
	float length(vec3 a) { return std::sqrt(dot(a, a)); }

	vec3 normalize(vec3 a)
	{
		const float l = length(a);
		return l > 1e-12f ? a * (1.0f / l) : vec3{};
	}

	quat operator*(quat a, quat b)
	{
		return { a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y, a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
			a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w, a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z };
	}

	quat conjugate(quat q) { return { -q.x, -q.y, -q.z, q.w }; }

	quat normalize(quat q)
	{
		const float l = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
		return l > 1e-12f ? quat{ q.x / l, q.y / l, q.z / l, q.w / l } : quat{};
	}

	vec3 rotate(quat q, vec3 v)
	{
		const vec3 u{ q.x, q.y, q.z };
		const vec3 t = cross(u, v) * 2.0f;
		return v + t * q.w + cross(u, t);
	}

	quat from_to(vec3 from, vec3 to)
	{
		from = normalize(from);
		to = normalize(to);
		const float c = dot(from, to);
		if (c < -0.99999f)
		{
			// Opposite: half turn about any axis perpendicular to from.
			vec3 axis = cross({ 1, 0, 0 }, from);
			if (length(axis) < 1e-3f)
			{
				axis = cross({ 0, 1, 0 }, from);
			}
			axis = normalize(axis);
			return { axis.x, axis.y, axis.z, 0 };
		}
		const vec3 axis = cross(from, to);
		return normalize(quat{ axis.x, axis.y, axis.z, 1.0f + c });
	}

	bone_transform compose(const bone_transform& parent, const bone_transform& local)
	{
		bone_transform model;
		model.position = parent.position + rotate(parent.rotation, local.position * parent.scale);
		model.scale = parent.scale * local.scale;
		model.rotation = parent.rotation * local.rotation;
		return model;
	}

	bone_transform inverse(const bone_transform& t)
	{
		bone_transform result;
		result.scale = t.scale != 0.0f ? 1.0f / t.scale : 1.0f;
		result.rotation = conjugate(t.rotation);
		result.position = rotate(result.rotation, t.position) * -result.scale;
		return result;
	}

	bone_transform model_of(const bone_transform* local, const int* parents, int bone)
	{
		constexpr int max_depth = 64;
		int chain[max_depth];
		int depth = 0;
		for (int b = bone; b >= 0 && depth < max_depth; b = parents[b])
		{
			chain[depth++] = b;
		}
		bone_transform model;
		while (depth > 0)
		{
			model = compose(model, local[chain[--depth]]);
		}
		return model;
	}

	namespace
	{
		void set_model_rotation(bone_transform* local, const int* parents, int bone, quat rotation)
		{
			const quat parent = parents[bone] >= 0 ? model_of(local, parents, parents[bone]).rotation : quat{};
			local[bone].rotation = normalize(conjugate(parent) * rotation);
		}

		// Twist part of q about the unit axis (swing-twist decomposition), raised to the power t.
		quat twist_fraction(quat q, vec3 axis, float t)
		{
			const float along = dot(vec3{ q.x, q.y, q.z }, axis);
			const float angle = 2.0f * std::atan2(along, q.w) * t;
			const vec3 v = axis * std::sin(0.5f * angle);
			return { v.x, v.y, v.z, std::cos(0.5f * angle) };
		}

		vec3 any_perpendicular(vec3 n)
		{
			const vec3 axis = std::fabs(n.y) < 0.9f ? vec3{ 0, 1, 0 } : vec3{ 1, 0, 0 };
			return normalize(cross(n, axis));
		}
	}

	void set_model_transform(bone_transform* local, const int* parents, int bone, const bone_transform& model)
	{
		const bone_transform parent = parents[bone] >= 0 ? model_of(local, parents, parents[bone]) : bone_transform{};
		local[bone] = compose(inverse(parent), model);
		local[bone].rotation = normalize(local[bone].rotation);
	}

	float reach_ratio(const bone_transform* local, const int* parents, const arm_bones& bones, vec3 wrist_goal)
	{
		const vec3 shoulder = model_of(local, parents, bones.upper).position;
		const vec3 elbow = model_of(local, parents, bones.fore).position;
		const float arm = length(elbow - shoulder) + length(model_of(local, parents, bones.hand).position - elbow);
		return arm > 1e-4f ? length(wrist_goal - shoulder) / arm : 0.0f;
	}

	bool solve_arm(bone_transform* local, const int* parents, const arm_bones& bones, const arm_goal& goal)
	{
		const bone_transform upper = model_of(local, parents, bones.upper);
		bone_transform fore = model_of(local, parents, bones.fore);
		bone_transform hand = model_of(local, parents, bones.hand);
		const vec3 shoulder = upper.position;
		float a = length(fore.position - shoulder);
		float b = length(hand.position - fore.position);
		const vec3 to_goal = goal.wrist - shoulder;
		const float reach = length(to_goal);
		if (a < 1e-4f || b < 1e-4f || reach < 1e-4f)
		{
			return false;
		}
		if (goal.max_stretch > 0 && reach > a + b)
		{
			// Every translation between the shoulder and the hand (roll bones included) scales, so
			// both segments lengthen by the same factor and their directions stay.
			const float stretch = std::min(reach / (a + b), 1.0f + goal.max_stretch);
			int bone = bones.hand;
			while (bone >= 0 && bone != bones.upper)
			{
				bone = parents[bone];
			}
			if (bone != bones.upper)
			{
				return false;
			}
			for (bone = bones.hand; bone != bones.upper; bone = parents[bone])
			{
				local[bone].position = local[bone].position * stretch;
			}
			fore = model_of(local, parents, bones.fore);
			hand = model_of(local, parents, bones.hand);
			a *= stretch;
			b *= stretch;
		}
		const vec3 n = to_goal * (1.0f / reach);
		const float d = std::clamp(reach, std::fabs(a - b) + 1e-4f, (a + b) * 0.9999f);

		// Bend plane: the pole's offset from the shoulder-goal line, else the animated elbow's.
		const auto off_line = [&](vec3 point)
		{
			const vec3 v = point - shoulder;
			return v - n * dot(v, n);
		};
		vec3 bend = off_line(goal.pole);
		if (length(bend) < 1e-4f)
		{
			bend = off_line(fore.position);
		}
		const vec3 m = length(bend) < 1e-4f ? any_perpendicular(n) : normalize(bend);

		const float cos_a = std::clamp((a * a + d * d - b * b) / (2.0f * a * d), -1.0f, 1.0f);
		const float sin_a = std::sqrt(std::max(0.0f, 1.0f - cos_a * cos_a));
		const vec3 elbow = shoulder + n * (a * cos_a) + m * (a * sin_a);

		set_model_rotation(local, parents, bones.upper, from_to(fore.position - shoulder, elbow - shoulder) * upper.rotation);

		const vec3 wrist = shoulder + n * d;
		const auto aim_forearm = [&]
		{
			const bone_transform fore_now = model_of(local, parents, bones.fore);
			const bone_transform hand_now = model_of(local, parents, bones.hand);
			set_model_rotation(local, parents, bones.fore,
				from_to(hand_now.position - fore_now.position, wrist - fore_now.position) * fore_now.rotation);
		};
		aim_forearm();

		if (bones.roll >= 0)
		{
			// Twist change of the hand relative to the roll bone, about the forearm axis, in the
			// roll bone's frame; the roll bone takes half so the forearm skin does not wrap.
			const bone_transform roll = model_of(local, parents, bones.roll);
			const bone_transform hand_now = model_of(local, parents, bones.hand);
			const quat before = conjugate(roll.rotation) * hand_now.rotation;
			const quat after = conjugate(roll.rotation) * normalize(goal.hand);
			const vec3 axis = normalize(rotate(conjugate(roll.rotation), hand_now.position - roll.position));
			if (length(axis) > 0.5f)
			{
				const quat change = normalize(after * conjugate(before));
				local[bones.roll].rotation = normalize(local[bones.roll].rotation * twist_fraction(change, axis, 0.5f));
				aim_forearm(); // the roll bone may sit off the forearm axis
			}
		}

		set_model_rotation(local, parents, bones.hand, normalize(goal.hand));
		return true;
	}
}
