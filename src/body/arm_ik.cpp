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

		vec3 any_perpendicular(vec3 n)
		{
			const vec3 axis = std::fabs(n.y) < 0.9f ? vec3{ 0, 1, 0 } : vec3{ 1, 0, 0 };
			return normalize(cross(n, axis));
		}
	}

	bool solve_arm(bone_transform* local, const int* parents, const arm_bones& bones, const arm_goal& goal)
	{
		const bone_transform upper = model_of(local, parents, bones.upper);
		const bone_transform fore = model_of(local, parents, bones.fore);
		const bone_transform hand = model_of(local, parents, bones.hand);
		const vec3 shoulder = upper.position;
		const float a = length(fore.position - shoulder);
		const float b = length(hand.position - fore.position);
		const vec3 to_goal = goal.wrist - shoulder;
		const float reach = length(to_goal);
		if (a < 1e-4f || b < 1e-4f || reach < 1e-4f)
		{
			return false;
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

		const bone_transform fore_now = model_of(local, parents, bones.fore);
		const bone_transform hand_now = model_of(local, parents, bones.hand);
		const vec3 wrist = shoulder + n * d;
		set_model_rotation(local, parents, bones.fore,
			from_to(hand_now.position - fore_now.position, wrist - fore_now.position) * fore_now.rotation);

		set_model_rotation(local, parents, bones.hand, normalize(goal.hand));
		return true;
	}
}
