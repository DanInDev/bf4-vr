#include "stereo/scripted_view.hpp"

#include <cmath>

namespace bfvr::stereo
{
	bool scripted_view::update(const scripted_input& input, float seconds)
	{
		// Without a soldier (a scene without the player) or dead: at once. Without an aim reading:
		// keep the last state rather than guess.
		if (!input.soldier || input.dead)
		{
			active_ = true;
			timer_ = 0.0f;
			return active_;
		}
		if (!input.aim_known || !std::isfinite(input.deviation_degrees))
		{
			return active_;
		}
		const bool against = active_ ? input.deviation_degrees < config_.leave_degrees : input.deviation_degrees > config_.enter_degrees;
		timer_ = against ? timer_ + seconds : 0.0f;
		if (timer_ >= (active_ ? config_.leave_seconds : config_.enter_seconds))
		{
			active_ = !active_;
			timer_ = 0.0f;
		}
		return active_;
	}

	float aim_deviation_degrees(const float (&camera_forward)[3], float aimer_yaw, float aimer_pitch)
	{
		const float length = std::sqrt(camera_forward[0] * camera_forward[0] + camera_forward[1] * camera_forward[1] + camera_forward[2] * camera_forward[2]);
		if (!(length > 1e-6f))
		{
			return NAN;
		}
		const float yaw = -aimer_yaw;
		const float aim[3] = { std::sin(yaw) * std::cos(aimer_pitch), std::sin(aimer_pitch), std::cos(yaw) * std::cos(aimer_pitch) };
		const float dot = (camera_forward[0] * aim[0] + camera_forward[1] * aim[1] + camera_forward[2] * aim[2]) / length;
		return std::acos(std::fmax(-1.0f, std::fmin(1.0f, dot))) * 57.29578f;
	}
}
