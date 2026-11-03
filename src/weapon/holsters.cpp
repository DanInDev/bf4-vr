#include "weapon/holsters.hpp"

#include <cmath>

namespace bfvr::weapon
{
	namespace
	{
		constexpr float pi = 3.14159265f;

		float wrap(float angle)
		{
			while (angle > pi)
			{
				angle -= 2.0f * pi;
			}
			while (angle < -pi)
			{
				angle += 2.0f * pi;
			}
			return angle;
		}
	}

	void holsters::reset()
	{
		*this = holsters(config_);
	}

	holster_result holsters::update(const holster_frame& frame)
	{
		holster_result result;
		const bool pressed = frame.gun_grip && !last_grip_;
		last_grip_ = frame.gun_grip;
		const bool off_pressed = frame.off_grip && !last_off_grip_;
		last_off_grip_ = frame.off_grip;
		const bool gun_hand = frame.hand_valid;
		const bool off_hand = frame.off_hand_valid && !frame.off_busy;
		if (!frame.head_valid || (!gun_hand && !off_hand))
		{
			return result;
		}

		// The body follows the head's yaw only past the slack.
		if (!body_known_)
		{
			body_yaw_ = frame.head_yaw;
			body_known_ = true;
		}
		const float off = wrap(frame.head_yaw - body_yaw_);
		if (off > config_.yaw_slack)
		{
			body_yaw_ = wrap(body_yaw_ + off - config_.yaw_slack);
		}
		else if (off < -config_.yaw_slack)
		{
			body_yaw_ = wrap(body_yaw_ + off + config_.yaw_slack);
		}

		// Body axes in OpenXR LOCAL: forward = (-sin, 0, -cos) of the yaw, right = (cos, 0, -sin).
		const float s = std::sin(body_yaw_), c = std::cos(body_yaw_);
		const float side = frame.left_handed ? -1.0f : 1.0f;
		float best = 1e9f, off_best = 1e9f;
		result.has_centres = true;
		int index = 0;
		const auto distance = [](const v3& a, const v3& b)
		{
			const float dx = a.x - b.x, dy = a.y - b.y, dz = a.z - b.z;
			return std::sqrt(dx * dx + dy * dy + dz * dz);
		};
		for (const holster_zone& zone : config_.zones)
		{
			const float x = zone.offset.x * side; // toward the gun side
			const float z = zone.offset.z;        // back
			const v3 at{ frame.head.x + x * c + z * s, frame.head.y + zone.offset.y, frame.head.z - x * s + z * c };
			result.centres[index] = at;
			result.radii[index++] = zone.radius;
			if (gun_hand)
			{
				const float d = distance(frame.gun_hand, at);
				if (d < zone.radius && d < best)
				{
					best = d;
					result.zone = zone.which;
				}
			}
			if (off_hand && zone.off_hand)
			{
				const float d = distance(frame.off_hand, at);
				if (d < zone.radius && d < off_best)
				{
					off_best = d;
					result.off_zone = zone.which;
				}
			}
		}
		if (pressed && result.zone != holster::none)
		{
			result.draw = result.zone;
		}
		else if (off_pressed && result.off_zone != holster::none)
		{
			result.draw = result.off_zone;
		}
		return result;
	}

	const char* holster_name(holster which)
	{
		switch (which)
		{
		case holster::none: return "none";
		case holster::primary: return "primary";
		case holster::secondary: return "secondary";
		case holster::knife: return "knife";
		case holster::gadget1: return "gadget1";
		case holster::gadget2: return "gadget2";
		}
		return "?";
	}
}
