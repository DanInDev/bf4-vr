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

	void point_on_quad(const quad_target& quad, float u, float v, float (&point)[3])
	{
		using body::ik::quat;
		using body::ik::vec3;
		const quat quad_rotation = body::ik::normalize(quat{ quad.pose.orientation[0], quad.pose.orientation[1], quad.pose.orientation[2], quad.pose.orientation[3] });
		const vec3 local{ (u - 0.5f) * quad.width, (0.5f - v) * quad.height, 0.0f };
		const vec3 world = body::ik::rotate(quad_rotation, local);
		point[0] = quad.pose.position[0] + world.x;
		point[1] = quad.pose.position[1] + world.y;
		point[2] = quad.pose.position[2] + world.z;
	}

	stereo::pose screen_in_front(const stereo::pose& head, float distance)
	{
		const float qx = head.orientation[0], qy = head.orientation[1], qz = head.orientation[2], qw = head.orientation[3];
		// The head's forward (-z) flattened onto the floor; looking straight up or down keeps -z.
		float fx = -2.0f * (qx * qz + qw * qy);
		float fz = -(1.0f - 2.0f * (qx * qx + qy * qy));
		const float length = std::sqrt(fx * fx + fz * fz);
		if (length < 1e-4f)
		{
			fx = 0.0f;
			fz = -1.0f;
		}
		else
		{
			fx /= length;
			fz /= length;
		}
		const float yaw = std::atan2(-fx, -fz);
		stereo::pose screen;
		screen.position[0] = head.position[0] + fx * distance;
		screen.position[1] = head.position[1];
		screen.position[2] = head.position[2] + fz * distance;
		screen.orientation[0] = 0.0f;
		screen.orientation[1] = std::sin(0.5f * yaw);
		screen.orientation[2] = 0.0f;
		screen.orientation[3] = std::cos(0.5f * yaw);
		return screen;
	}
}
