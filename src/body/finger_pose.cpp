#include "body/finger_pose.hpp"

#include <algorithm>
#include <cmath>

namespace bfvr::body::fingers
{
	namespace
	{
		constexpr float radians = 3.14159265f / 180.0f;

		// Bend in degrees per joint (knuckle, middle, tip) at curl 0 (open) and 1 (fist). The thumb
		// has two bending joints (Thumb2, Thumb3). The game's own grips sit between: the AK
		// handguard ~62/45/43, the pistol grip ~83/56/67 (live, 2026-11-03).
		struct range
		{
			float open, fist;
		};
		constexpr range ranges[count][joints] = {
			{ { -10, 40 }, { -10, 55 }, { 0, 0 } }, // thumb
			{ { 4, 85 }, { 5, 100 }, { 3, 70 } },   // index
			{ { 4, 88 }, { 5, 100 }, { 3, 70 } },   // middle
			{ { 5, 90 }, { 5, 100 }, { 3, 70 } },   // ring
			{ { 6, 92 }, { 5, 100 }, { 3, 70 } },   // pinky
		};

		float clamp01(float v)
		{
			return std::clamp(v, 0.0f, 1.0f);
		}

		ik::quat nlerp(ik::quat a, ik::quat b, float t)
		{
			const float s = a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w < 0 ? -1.0f : 1.0f;
			return ik::normalize({ a.x + (s * b.x - a.x) * t, a.y + (s * b.y - a.y) * t, a.z + (s * b.z - a.z) * t, a.w + (s * b.w - a.w) * t });
		}

		void set_all(hand_shape& shape, float animated, float curl)
		{
			std::fill(std::begin(shape.animated), std::end(shape.animated), animated);
			std::fill(std::begin(shape.curl), std::end(shape.curl), curl);
		}

		hand_shape curled(float thumb_curl, float index_curl, float middle_curl, float ring_curl, float pinky_curl)
		{
			hand_shape shape;
			set_all(shape, 0.0f, 0.0f);
			shape.curl[thumb] = thumb_curl;
			shape.curl[index] = index_curl;
			shape.curl[middle] = middle_curl;
			shape.curl[ring] = ring_curl;
			shape.curl[pinky] = pinky_curl;
			return shape;
		}
	}

	hand_shape shape_for(task what, const finger_input& input)
	{
		switch (what)
		{
		case task::item:
		case task::support:
			return {};
		case task::gun_grip:
		{
			hand_shape shape;
			// Off the trigger the index lies straight along the frame (trigger discipline).
			if (input.valid && !input.trigger_touch && input.trigger < 0.05f)
			{
				shape.animated[index] = 0.0f;
				shape.curl[index] = 0.05f;
			}
			return shape;
		}
		case task::magazine:
			return curled(0.45f, 0.5f, 0.55f, 0.6f, 0.65f);
		case task::handle:
			return curled(0.3f, 0.75f, 0.8f, 0.85f, 0.9f);
		case task::reach:
			return curled(0.0f, 0.05f, 0.05f, 0.05f, 0.08f);
		case task::free:
			break;
		}
		if (!input.valid)
		{
			return curled(0.2f, 0.3f, 0.3f, 0.32f, 0.35f); // relaxed
		}
		const float squeeze = clamp01(input.squeeze);
		const float grip = 0.12f + 0.83f * squeeze;
		const float index_curl = input.trigger_touch || input.trigger > 0.05f ? 0.3f + 0.6f * clamp01(input.trigger) : 0.08f;
		const float thumb_curl = input.thumb_touch ? 0.55f : 0.1f + 0.2f * squeeze;
		return curled(thumb_curl, index_curl, grip, grip, std::min(1.0f, grip + 0.03f));
	}

	float bend_of(ik::quat rotation)
	{
		// Twist about z: the z and w parts of the quaternion.
		if (std::fabs(rotation.z) < 1e-8f && std::fabs(rotation.w) < 1e-8f)
		{
			return 0.0f;
		}
		float angle = 2.0f * std::atan2(rotation.z, rotation.w);
		if (angle > 3.14159265f)
		{
			angle -= 6.2831853f;
		}
		else if (angle < -3.14159265f)
		{
			angle += 6.2831853f;
		}
		return angle;
	}

	ik::quat with_bend(ik::quat rotation, float angle)
	{
		// rotation = swing * twist(z): keep the swing (spread), replace the twist.
		const float current = bend_of(rotation);
		const ik::quat untwist{ 0, 0, std::sin(-0.5f * current), std::cos(-0.5f * current) };
		const ik::quat swing = rotation * untwist;
		return ik::normalize(swing * ik::quat{ 0, 0, std::sin(0.5f * angle), std::cos(0.5f * angle) });
	}

	float bend_at(finger which, int joint, float curl)
	{
		if (which < 0 || which >= count || joint < 0 || joint >= joints)
		{
			return 0.0f;
		}
		const range r = ranges[which][joint];
		return (r.open + (r.fist - r.open) * clamp01(curl)) * radians;
	}

	ik::quat shaped(ik::quat animated, finger which, int joint, const hand_shape& shape)
	{
		if (which < 0 || which >= count)
		{
			return animated;
		}
		const float keep = clamp01(shape.animated[which]);
		if (keep >= 1.0f)
		{
			return animated;
		}
		const ik::quat drawn = with_bend(animated, bend_at(which, joint, shape.curl[which]));
		return keep <= 0.0f ? drawn : nlerp(drawn, animated, keep);
	}

	ik::quat from_basis(ik::vec3 x, ik::vec3 y, ik::vec3 z)
	{
		// Shepperd's method on the matrix with columns x, y, z.
		const float trace = x.x + y.y + z.z;
		ik::quat q;
		if (trace > 0.0f)
		{
			const float s = 2.0f * std::sqrt(trace + 1.0f);
			q = { (y.z - z.y) / s, (z.x - x.z) / s, (x.y - y.x) / s, 0.25f * s };
		}
		else if (x.x > y.y && x.x > z.z)
		{
			const float s = 2.0f * std::sqrt(1.0f + x.x - y.y - z.z);
			q = { 0.25f * s, (y.x + x.y) / s, (z.x + x.z) / s, (y.z - z.y) / s };
		}
		else if (y.y > z.z)
		{
			const float s = 2.0f * std::sqrt(1.0f + y.y - x.x - z.z);
			q = { (y.x + x.y) / s, 0.25f * s, (z.y + y.z) / s, (z.x - x.z) / s };
		}
		else
		{
			const float s = 2.0f * std::sqrt(1.0f + z.z - x.x - y.y);
			q = { (z.x + x.z) / s, (z.y + y.z) / s, 0.25f * s, (x.y - y.x) / s };
		}
		return ik::normalize(q);
	}

	grasp grasp_of(ik::vec3 index_knuckle, ik::vec3 pinky_knuckle, ik::vec3 middle_knuckle, ik::vec3 middle_tip, ik::vec3 thumb_tip)
	{
		grasp out;
		const ik::vec3 across = index_knuckle - pinky_knuckle;
		const ik::vec3 curl = middle_tip - middle_knuckle;
		if (ik::length(across) < 1e-4f || ik::length(curl) < 1e-4f)
		{
			return out;
		}
		const ik::vec3 y = ik::normalize(across);
		const ik::vec3 along = curl - y * ik::dot(curl, y);
		if (ik::length(along) < 1e-4f)
		{
			return out;
		}
		const ik::vec3 z = ik::normalize(along);
		out.frame = from_basis(ik::cross(y, z), y, z);
		out.centre = (middle_knuckle + middle_tip + thumb_tip) * (1.0f / 3.0f);
		out.valid = true;
		return out;
	}

	const hand_shape& shape_smoother::update(const hand_shape& target, float seconds, float time_constant)
	{
		if (!started_)
		{
			reset(target);
			return current_;
		}
		const float t = time_constant > 0.0f ? 1.0f - std::exp(-std::max(seconds, 0.0f) / time_constant) : 1.0f;
		for (int f = 0; f < count; ++f)
		{
			current_.animated[f] += (target.animated[f] - current_.animated[f]) * t;
			current_.curl[f] += (target.curl[f] - current_.curl[f]) * t;
		}
		return current_;
	}
}
