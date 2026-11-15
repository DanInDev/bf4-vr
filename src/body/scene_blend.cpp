#include "body/scene_blend.hpp"

#include <algorithm>
#include <cmath>

namespace bfvr::body
{
	float scene_blend::update(bool scripted, float seconds, float time_constant)
	{
		const float target = scripted ? 0.0f : 1.0f;
		const float step = time_constant > 0.0f ? 1.0f - std::exp(-std::max(seconds, 0.0f) / time_constant) : 1.0f;
		weight_ += (target - weight_) * step;
		if (std::fabs(target - weight_) < 0.01f)
		{
			weight_ = target;
		}
		return weight_;
	}

	void blend_pose(ik::bone_transform* edited, const ik::bone_transform* animated, int count, float weight)
	{
		if (!edited || !animated || weight >= 1.0f)
		{
			return;
		}
		const float keep = std::max(weight, 0.0f);
		for (int bone = 0; bone < count; ++bone)
		{
			ik::bone_transform& out = edited[bone];
			const ik::bone_transform& from = animated[bone];
			out.position = from.position + (out.position - from.position) * keep;
			out.scale = from.scale + (out.scale - from.scale) * keep;
			const ik::quat a = from.rotation;
			const ik::quat b = out.rotation;
			const float sign = a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w < 0.0f ? -1.0f : 1.0f;
			out.rotation = ik::normalize({ a.x + (sign * b.x - a.x) * keep, a.y + (sign * b.y - a.y) * keep, a.z + (sign * b.z - a.z) * keep,
				a.w + (sign * b.w - a.w) * keep });
		}
	}
}
