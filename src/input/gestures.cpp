#include "input/gestures.hpp"

#include <algorithm>
#include <cmath>

#include "body/arm_ik.hpp"

namespace bfvr::input::gestures
{
	namespace
	{
		using body::ik::quat;
		using body::ik::vec3;

		vec3 position_of(const pose& p)
		{
			return { p.position[0], p.position[1], p.position[2] };
		}

		quat rotation_of(const pose& p)
		{
			return body::ik::normalize(quat{ p.orientation[0], p.orientation[1], p.orientation[2], p.orientation[3] });
		}

		vec3 to_vec(const float (&v)[3])
		{
			return { v[0], v[1], v[2] };
		}

		void store(vec3 v, float (&out)[3])
		{
			out[0] = v.x;
			out[1] = v.y;
			out[2] = v.z;
		}

		vec3 forward(const pose& p)
		{
			return body::ik::rotate(rotation_of(p), { 0, 0, -1 });
		}

		float angle_between(vec3 a, vec3 b)
		{
			const float lengths = body::ik::length(a) * body::ik::length(b);
			if (lengths <= 1e-9f)
			{
				return 3.14159265f;
			}
			return std::acos(std::clamp(body::ik::dot(a, b) / lengths, -1.0f, 1.0f));
		}

		// Rotation angle between two orientations.
		float rotation_angle(const pose& a, const pose& b)
		{
			const quat qa = rotation_of(a);
			const quat qb = rotation_of(b);
			const float d = std::fabs(qa.x * qb.x + qa.y * qb.y + qa.z * qb.z + qa.w * qb.w);
			return 2.0f * std::acos(std::min(d, 1.0f));
		}

		// offset is given for one hand; x is mirrored for the other.
		vec3 point_on(const pose& grip, const float (&offset)[3], bool mirror)
		{
			const float mirrored[3]{ mirror ? -offset[0] : offset[0], offset[1], offset[2] };
			float out[3];
			transform_point(grip, mirrored, out);
			return to_vec(out);
		}

		// Hides after the stay condition failed for delay seconds; shows after the show condition
		// held for dwell seconds.
		void advance(bool& visible, float& timer, bool show, bool stay, float dwell, float delay, float seconds)
		{
			if (!visible)
			{
				timer = show ? timer + seconds : 0.0f;
				if (show && timer >= dwell)
				{
					visible = true;
					timer = 0;
				}
				return;
			}
			timer = stay ? 0.0f : timer + seconds;
			if (timer >= delay && !stay)
			{
				visible = false;
				timer = 0;
			}
		}
	}

	void transform_point(const pose& p, const float (&offset)[3], float (&out)[3])
	{
		store(position_of(p) + body::ik::rotate(rotation_of(p), to_vec(offset)), out);
	}

	float gaze_angle(const pose& head, const float (&point)[3])
	{
		return angle_between(forward(head), to_vec(point) - position_of(head));
	}

	void palm_normal(const pose& grip, int hand, float (&out)[3])
	{
		store(body::ik::rotate(rotation_of(grip), { hand == 1 ? -1.0f : 1.0f, 0, 0 }), out);
	}

	float head_pitch_down(const pose& head)
	{
		return std::asin(std::clamp(-forward(head).y, -1.0f, 1.0f));
	}

	bool forearm_in_view(const pose& head, const pose& grip, const forearm_glance_config& config)
	{
		const vec3 point = point_on(grip, config.offset, config.hand == 1);
		const vec3 to_eyes = position_of(head) - point;
		if (body::ik::length(to_eyes) > config.max_distance || head_pitch_down(head) < config.min_head_pitch)
		{
			return false;
		}
		float normal[3];
		palm_normal(grip, config.hand, normal);
		float target[3];
		store(point, target);
		return gaze_angle(head, target) <= config.show_cone && body::ik::dot(to_vec(normal), body::ik::normalize(to_eyes)) >= config.facing;
	}

	bool forearm_glance::update(const pose& head, const pose& grip, bool tracked, float seconds, bool busy)
	{
		bool show = false;
		bool stay = false;
		if (tracked && !busy)
		{
			show = forearm_in_view(head, grip, config_);
			const vec3 point = point_on(grip, config_.offset, config_.hand == 1);
			float target[3];
			store(point, target);
			float normal[3];
			palm_normal(grip, config_.hand, normal);
			stay = gaze_angle(head, target) <= config_.hide_cone && body::ik::dot(to_vec(normal), position_of(head) - point) > 0;
		}
		advance(visible_, timer_, show, stay, config_.dwell, config_.hide_delay, std::max(seconds, 0.0f));
		return visible_;
	}

	void forearm_glance::reset()
	{
		visible_ = false;
		timer_ = 0;
	}

	bool gun_in_inspect_pose(const pose& head, const pose& grip, const gun_inspect_config& config)
	{
		const vec3 point = point_on(grip, config.offset, config.hand == 0);
		const vec3 to_eyes = position_of(head) - point;
		if (body::ik::length(to_eyes) > config.max_distance)
		{
			return false;
		}
		float target[3];
		store(point, target);
		const vec3 side = body::ik::rotate(rotation_of(grip), { 1, 0, 0 });
		return gaze_angle(head, target) <= config.show_cone && std::fabs(body::ik::dot(side, body::ik::normalize(to_eyes))) >= config.side_facing;
	}

	bool gun_in_mag_check_pose(const pose& head, const pose& grip, const gun_inspect_config& config)
	{
		const vec3 point = point_on(grip, config.mag_offset, config.hand == 0);
		if (body::ik::length(position_of(head) - point) > config.max_distance)
		{
			return false;
		}
		float target[3];
		store(point, target);
		const vec3 up = body::ik::rotate(rotation_of(grip), { 0, 1, 0 });
		return gaze_angle(head, target) <= config.show_cone && up.y <= config.upside_down;
	}

	bool gun_inspect::update(const pose& head, const pose& grip, bool tracked, float seconds)
	{
		seconds = std::max(seconds, 0.0f);
		bool show = false;
		bool stay = false;
		float hold = config_.hold;
		if (tracked)
		{
			bool steady = has_last_ && seconds > 0;
			if (steady)
			{
				const float blend = config_.smoothing > 0 ? std::min(seconds / config_.smoothing, 1.0f) : 1.0f;
				speed_ += (body::ik::length(position_of(grip) - position_of(last_)) / seconds - speed_) * blend;
				angular_speed_ += (rotation_angle(grip, last_) / seconds - angular_speed_) * blend;
				steady = speed_ <= config_.max_speed && angular_speed_ <= config_.max_angular_speed;
			}
			const bool mag = config_.mag_check && gun_in_mag_check_pose(head, grip, config_);
			const bool side = config_.side && gun_in_inspect_pose(head, grip, config_);
			show = steady && (mag || side);
			if (mag)
			{
				hold = config_.mag_hold;
			}
			float target[3];
			store(point_on(grip, config_.offset, config_.hand == 0), target);
			float magazine[3];
			store(point_on(grip, config_.mag_offset, config_.hand == 0), magazine);
			stay = std::min(gaze_angle(head, target), gaze_angle(head, magazine)) <= config_.hide_cone;
			last_ = grip;
			has_last_ = true;
		}
		else
		{
			has_last_ = false;
			speed_ = angular_speed_ = 0;
		}
		advance(visible_, timer_, show, stay, hold, config_.hide_delay, seconds);
		return visible_;
	}

	void gun_inspect::reset()
	{
		visible_ = false;
		timer_ = 0;
		has_last_ = false;
		speed_ = angular_speed_ = 0;
	}
}
