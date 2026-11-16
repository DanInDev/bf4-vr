#include "body/hand_collision.hpp"

#include <algorithm>
#include <cmath>

namespace bfvr::body::contact
{
	namespace
	{
		float axis_of(ik::vec3 v, int axis)
		{
			return axis == 0 ? v.x : axis == 1 ? v.y : v.z;
		}

		ik::vec3 unit(int axis, float sign)
		{
			return axis == 0 ? ik::vec3{ sign, 0, 0 } : axis == 1 ? ik::vec3{ 0, sign, 0 } : ik::vec3{ 0, 0, sign };
		}

		// The face a point outside the (expanded) box is beyond the most.
		int outside_face(ik::vec3 local, ik::vec3 expanded)
		{
			int best = 0;
			float most = -1e9f;
			for (int axis = 0; axis < 3; ++axis)
			{
				const float beyond = std::fabs(axis_of(local, axis)) - axis_of(expanded, axis);
				if (beyond > most)
				{
					most = beyond;
					best = axis;
				}
			}
			return (axis_of(local, best) < 0.0f ? -1 : 1) * (best + 1);
		}

		// The face nearest to a point inside the expanded box.
		int nearest_face(ik::vec3 local, ik::vec3 expanded)
		{
			int best = 0;
			float least = 1e9f;
			for (int axis = 0; axis < 3; ++axis)
			{
				const float to_face = axis_of(expanded, axis) - std::fabs(axis_of(local, axis));
				if (to_face < least)
				{
					least = to_face;
					best = axis;
				}
			}
			return (axis_of(local, best) < 0.0f ? -1 : 1) * (best + 1);
		}
	}

	ik::vec3 hand_collider::resolve(const box* boxes, int box_count, const sphere* spheres, int sphere_count, float give_up, float give_up_through)
	{
		touching_ = false;
		depth_ = 0.0f;
		ik::vec3 total{};
		box_count = std::min(box_count, max_boxes);
		if (sphere_count <= 0)
		{
			return total;
		}
		for (int b = 0; b < box_count; ++b)
		{
			const box& shape = boxes[b];
			const ik::quat to_local = ik::conjugate(shape.frame);
			// The first sphere (the palm) decides which side of the box the hand is on.
			const sphere& reference = spheres[0];
			const ik::vec3 reference_local = ik::rotate(to_local, reference.centre + total - shape.centre);
			const ik::vec3 reference_expanded = shape.half + ik::vec3{ reference.radius, reference.radius, reference.radius };
			bool inside = false;
			for (int s = 0; s < sphere_count && !inside; ++s)
			{
				const ik::vec3 local = ik::rotate(to_local, spheres[s].centre + total - shape.centre);
				const float r = spheres[s].radius;
				inside = std::fabs(local.x) < shape.half.x + r && std::fabs(local.y) < shape.half.y + r && std::fabs(local.z) < shape.half.z + r;
			}
			int face = side_[b];
			bool passed = false;
			if (!inside && face != 0)
			{
				// Through the box within one frame: beyond the opposite face of the remembered one, inside the outline.
				const int axis = std::abs(face) - 1;
				const float sign = face > 0 ? 1.0f : -1.0f;
				bool within = true;
				for (int other = 0; other < 3; ++other)
				{
					if (other != axis && std::fabs(axis_of(reference_local, other)) >= axis_of(reference_expanded, other))
					{
						within = false;
					}
				}
				passed = within && sign * axis_of(reference_local, axis) <= -axis_of(reference_expanded, axis);
			}
			if (!inside && !passed)
			{
				side_[b] = outside_face(reference_local, reference_expanded);
				continue;
			}
			// Through from a known side: past the box's middle along that axis (or beyond it) counts as through.
			if (inside && face != 0)
			{
				const int axis = std::abs(face) - 1;
				const float sign = face > 0 ? 1.0f : -1.0f;
				passed = sign * axis_of(reference_local, axis) < 0.0f;
			}
			if (face == 0)
			{
				face = nearest_face(reference_local, reference_expanded);
			}
			const int axis = std::abs(face) - 1;
			const float sign = face > 0 ? 1.0f : -1.0f;
			float need = 0.0f;
			for (int s = 0; s < sphere_count; ++s)
			{
				const ik::vec3 local = ik::rotate(to_local, spheres[s].centre + total - shape.centre);
				const ik::vec3 expanded = shape.half + ik::vec3{ spheres[s].radius, spheres[s].radius, spheres[s].radius };
				bool across = true;
				for (int other = 0; other < 3; ++other)
				{
					if (other != axis && std::fabs(axis_of(local, other)) >= axis_of(expanded, other))
					{
						across = false;
					}
				}
				// A sphere inside the outline and short of the face (or past the far one, after a slap) is moved to it.
				if (across)
				{
					need = std::max(need, axis_of(expanded, axis) - sign * axis_of(local, axis));
				}
			}
			if (need <= 0.0f)
			{
				side_[b] = face;
				continue;
			}
			if (need > (passed ? give_up_through : give_up))
			{
				side_[b] = 0;
				continue;
			}
			side_[b] = face;
			depth_ = std::max(depth_, need);
			touching_ = true;
			total = total + ik::rotate(shape.frame, unit(axis, sign)) * need;
		}
		return total;
	}

	void hand_collider::reset()
	{
		for (int& side : side_)
		{
			side = 0;
		}
		touching_ = false;
		depth_ = 0.0f;
	}

	int gun_boxes(const gun_points& points, const gun_shape& shape, box (&boxes)[hand_collider::max_boxes])
	{
		constexpr ik::quat level{ 0, 0, 0, 1 };
		const ik::vec3& top = points.magazine_top;
		// Along the barrel: from behind the gun hand to ahead of the magazine (a bullpup's grip is ahead of its
		// magazine: the receiver then spans both).
		const float back = std::min(points.grip.z - shape.receiver_back, top.z - shape.receiver_ahead);
		const float front = std::max(top.z + shape.receiver_ahead, points.grip.z + shape.receiver_back);
		boxes[0] = { { 0.0f, top.y + 0.5f * shape.receiver_height, 0.5f * (back + front) }, level,
			{ shape.half_width, 0.5f * shape.receiver_height, 0.5f * (front - back) } };
		int count = 1;
		const float end = points.support.z + shape.handguard_beyond;
		if (end - front > 0.02f)
		{
			boxes[count++] = { { 0.0f, top.y + shape.barrel_above, 0.5f * (front + end) }, level,
				{ shape.half_width, shape.handguard_half_height, 0.5f * (end - front) } };
		}
		return count;
	}
}
