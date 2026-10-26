#include "input/pointer.hpp"

#include <cmath>

#include "body/arm_ik.hpp"

namespace bfvr::input
{
	pointer_hit point_at(const stereo::pose& aim, const quad_target& quad, float max_distance)
	{
		using body::ik::quat;
		using body::ik::vec3;
		pointer_hit result;
		if (quad.width <= 0 || quad.height <= 0)
		{
			return result;
		}

		const quat ray_rotation = body::ik::normalize(quat{ aim.orientation[0], aim.orientation[1], aim.orientation[2], aim.orientation[3] });
		const quat quad_rotation = body::ik::normalize(quat{ quad.pose.orientation[0], quad.pose.orientation[1], quad.pose.orientation[2], quad.pose.orientation[3] });
		const vec3 origin{ aim.position[0], aim.position[1], aim.position[2] };
		const vec3 centre{ quad.pose.position[0], quad.pose.position[1], quad.pose.position[2] };
		const vec3 direction = body::ik::rotate(ray_rotation, { 0, 0, -1 });
		const vec3 normal = body::ik::rotate(quad_rotation, { 0, 0, 1 });

		const float facing = body::ik::dot(direction, normal);
		if (facing >= -1e-6f)
		{
			return result; // parallel, or hitting the back
		}
		const float distance = body::ik::dot(centre - origin, normal) / facing;
		if (distance < 0 || distance > max_distance)
		{
			return result;
		}

		const vec3 point = origin + direction * distance;
		const vec3 local = body::ik::rotate(body::ik::conjugate(quad_rotation), point - centre);
		const float u = local.x / quad.width + 0.5f;
		const float v = 0.5f - local.y / quad.height;
		if (u < 0 || u > 1 || v < 0 || v > 1)
		{
			return result;
		}
		result = { true, distance, u, v, { point.x, point.y, point.z } };
		return result;
	}
}
