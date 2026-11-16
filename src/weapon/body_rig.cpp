#include "weapon/body_rig.hpp"

#include <cmath>

namespace bfvr::weapon
{
	v3 sized_offset(const v3& offset, const body::body_size& size)
	{
		const float y = offset.y <= body::hip_zone_line ? offset.y * size.hip_drop / body::reference_hip_drop : offset.y * size.scale;
		const float z = offset.z < 0.0f ? offset.z * size.chest_depth / body::reference_chest_depth : offset.z * size.scale;
		return { offset.x * size.scale, y, z };
	}

	v3 level_head(const v3& eyes, const q4& q)
	{
		const auto rotate = [](const q4& r, const v3& v)
		{
			const float tx = 2.0f * (r.y * v.z - r.z * v.y), ty = 2.0f * (r.z * v.x - r.x * v.z), tz = 2.0f * (r.x * v.y - r.y * v.x);
			return v3{ v.x + r.w * tx + (r.y * tz - r.z * ty), v.y + r.w * ty + (r.z * tx - r.x * tz), v.z + r.w * tz + (r.x * ty - r.y * tx) };
		};
		const v3 from_neck{ 0.0f, eye_above_neck, -eye_ahead_of_neck };
		const v3 now = rotate(q, from_neck);
		const v3 neck{ eyes.x - now.x, eyes.y - now.y, eyes.z - now.z };
		// The head's yaw: its forward (-z) flattened.
		const v3 forward = rotate(q, { 0.0f, 0.0f, -1.0f });
		const float flat = std::sqrt(forward.x * forward.x + forward.z * forward.z);
		if (!(flat > 1e-3f))
		{
			return eyes; // looking straight up or down: no yaw to keep
		}
		const float fx = forward.x / flat, fz = forward.z / flat;
		// from_neck turned by the yaw alone: its forward part along the flat forward.
		return { neck.x + fx * eye_ahead_of_neck, neck.y + eye_above_neck, neck.z + fz * eye_ahead_of_neck };
	}

	v3 slot_model_point(const v3& point, float body_yaw, float ahead)
	{
		// Facing -z at yaw 0, turning left with +yaw.
		return { point.x - std::sin(body_yaw) * ahead, point.y, point.z - std::cos(body_yaw) * ahead };
	}

	v3 body_point(const v3& head, float body_yaw, const v3& offset, bool left_handed, const body::body_size& size)
	{
		// Same axes as holsters::update: right = (cos, 0, -sin), back = (sin, 0, cos).
		const float s = std::sin(body_yaw), c = std::cos(body_yaw);
		const v3 sized = sized_offset(offset, size);
		const float x = left_handed ? -sized.x : sized.x;
		return { head.x + x * c + sized.z * s, head.y + sized.y, head.z - x * s + sized.z * c };
	}
}
