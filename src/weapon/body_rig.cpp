#include "weapon/body_rig.hpp"

#include <cmath>

namespace bfvr::weapon
{
	v3 body_point(const v3& head, float body_yaw, const v3& offset, bool left_handed, float scale)
	{
		// Same axes as holsters::update: right = (cos, 0, -sin), back = (sin, 0, cos).
		const float s = std::sin(body_yaw), c = std::cos(body_yaw);
		const float x = (left_handed ? -offset.x : offset.x) * scale;
		const float z = offset.z * scale;
		return { head.x + x * c + z * s, head.y + offset.y * scale, head.z - x * s + z * c };
	}
}
