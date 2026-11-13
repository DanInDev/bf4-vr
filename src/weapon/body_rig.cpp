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

	v3 body_point(const v3& head, float body_yaw, const v3& offset, bool left_handed, const body::body_size& size)
	{
		// Same axes as holsters::update: right = (cos, 0, -sin), back = (sin, 0, cos).
		const float s = std::sin(body_yaw), c = std::cos(body_yaw);
		const v3 sized = sized_offset(offset, size);
		const float x = left_handed ? -sized.x : sized.x;
		return { head.x + x * c + sized.z * s, head.y + sized.y, head.z - x * s + sized.z * c };
	}
}
