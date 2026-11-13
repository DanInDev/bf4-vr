#include "body/body_calibration.hpp"

#include <algorithm>
#include <cmath>

namespace bfvr::body::calibration
{
	namespace
	{
		constexpr float crown_above_eyes = 0.11f;
		constexpr float fingertips_past_grip = 0.09f;
		constexpr float shoulder_ratio = 0.2f; // shoulder joints apart, of stature
		constexpr float min_stature = 1.2f;
		constexpr float max_stature = 2.2f;

		float horizontal(const ik::vec3& a, const ik::vec3& b)
		{
			const float dx = a.x - b.x;
			const float dz = a.z - b.z;
			return std::sqrt(dx * dx + dz * dz);
		}
	}

	float stature_from_eyes(float eye_height)
	{
		return eye_height + crown_above_eyes;
	}

	float stature_from_span(float span)
	{
		return span + 2.0f * fingertips_past_grip;
	}

	float arm_reach(float span, float stature)
	{
		return 0.5f * (span - shoulder_ratio * stature);
	}

	body_shape shape_for(const measurement& measured, const reference& ref)
	{
		body_shape shape;
		shape.arm_length = ref.arm_length;
		if (!measured.valid || ref.stature <= 0.0f)
		{
			return shape;
		}
		shape.scale = measured.stature / ref.stature;
		const float ref_reach = arm_reach(ref.stature - 2.0f * fingertips_past_grip, ref.stature); // span = stature
		const float reach = arm_reach(measured.span, measured.stature);
		if (ref_reach > 0.0f && reach > 0.0f)
		{
			shape.arm_length = std::clamp(ref.arm_length * reach / ref_reach, 0.8f, 1.5f);
		}
		shape.shoulder_width = std::clamp(ref.shoulder_anchor * (shape.scale - 1.0f), -0.1f, 0.2f);
		return shape;
	}

	bool needs_confirmation(float stored_height, float measured_height, float tolerance)
	{
		return stored_height > 0.0f && std::fabs(measured_height - stored_height) > tolerance;
	}

	bool t_pose::in_pose(const sample& now) const
	{
		if (!now.head_valid || !now.hands_valid || !now.left_trigger || !now.right_trigger)
		{
			return false;
		}
		const float span = horizontal(now.left, now.right);
		const ik::vec3 middle = (now.left + now.right) * 0.5f;
		const auto at_shoulders = [&](const ik::vec3& hand) {
			return hand.y <= now.head.y + config_.max_above_eyes && hand.y >= now.head.y - config_.max_below_eyes;
		};
		return span >= config_.min_span && at_shoulders(now.left) && at_shoulders(now.right)
			&& std::fabs(now.left.y - now.right.y) <= config_.max_level_difference && horizontal(middle, now.head) <= config_.max_off_centre;
	}

	t_pose::state t_pose::update(const sample& now)
	{
		if (state_ == state::done)
		{
			return state_;
		}
		if (!in_pose(now))
		{
			reset();
			return state_;
		}
		if (state_ == state::waiting)
		{
			state_ = state::holding;
			start_left_ = now.left;
			start_right_ = now.right;
		}
		else if (ik::length(now.left - start_left_) > config_.max_motion || ik::length(now.right - start_right_) > config_.max_motion)
		{
			// Still moving into the pose: the hold starts again from here.
			reset();
			state_ = state::holding;
			start_left_ = now.left;
			start_right_ = now.right;
		}
		held_ += now.seconds;
		span_sum_ += horizontal(now.left, now.right);
		head_sum_ += now.head.y;
		++samples_;
		if (now.floor_valid)
		{
			floor_sum_ += now.floor_y;
			++floor_samples_;
		}
		if (held_ < config_.hold_seconds || samples_ == 0)
		{
			return state_;
		}
		measurement m;
		m.span = span_sum_ / static_cast<float>(samples_);
		m.standing_head_y = head_sum_ / static_cast<float>(samples_);
		m.from_floor = floor_samples_ * 2 >= samples_;
		if (m.from_floor)
		{
			m.eye_height = m.standing_head_y - floor_sum_ / static_cast<float>(floor_samples_);
			m.stature = stature_from_eyes(m.eye_height);
			if (std::fabs(m.stature - stature_from_span(m.span)) > config_.max_floor_disagreement)
			{
				// The STAGE floor has been seen 11 cm off between two runs; the span does not move.
				m.from_floor = false;
				m.floor_doubted = true;
			}
		}
		if (!m.from_floor)
		{
			m.stature = stature_from_span(m.span);
			m.eye_height = m.stature - crown_above_eyes;
		}
		m.valid = std::isfinite(m.stature) && m.stature >= min_stature && m.stature <= max_stature;
		if (!m.valid)
		{
			reset(); // not a person standing (a floor far off): measure again
			return state_;
		}
		result_ = m;
		state_ = state::done;
		return state_;
	}

	void t_pose::reset()
	{
		state_ = state::waiting;
		held_ = 0.0f;
		span_sum_ = head_sum_ = floor_sum_ = 0.0f;
		samples_ = floor_samples_ = 0;
		result_ = {};
	}
}
