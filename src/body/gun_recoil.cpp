#include "body/gun_recoil.hpp"

#include <algorithm>
#include <cmath>

namespace bfvr::body
{
	recoil_offset gun_recoil::decayed(double now) const
	{
		if (last_ < 0.0 || now < last_ || now - last_ >= 2.0)
		{
			return {};
		}
		const float elapsed = static_cast<float>(now - last_);
		const float pitch = std::exp(-elapsed / (config_.pitch_recovery * recovery_));
		return { state_.degrees * pitch, state_.back * std::exp(-elapsed / (config_.back_recovery * recovery_)), state_.yaw * pitch };
	}

	float gun_recoil::random()
	{
		seed_ = seed_ * 1664525u + 1013904223u;
		return static_cast<float>(seed_ >> 8) / 16777216.0f;
	}

	void gun_recoil::shot(double now, int shots, float strength, bool two_handed, const recoil_profile& gun, float one_hand)
	{
		if (shots <= 0 || !(strength > 0.0f))
		{
			return;
		}
		const float hold = strength * (two_handed ? config_.two_handed_scale : 1.0f);
		// #60: the one-hand penalty, blended in by its setting; none for a pistol or with the support hand.
		const float penalty = !two_handed && !gun.pistol && std::isfinite(one_hand) ? std::clamp(one_hand, 0.0f, 1.0f) : 0.0f;
		const auto blend = [&](float plain, float one_handed) { return plain + (one_handed - plain) * penalty; };
		const recoil_offset before = decayed(now);
		const float first = before.degrees < config_.rest_degrees ? std::max(1.0f, gun.first_shot) : 1.0f;
		const float vertical = hold * std::max(0.0f, gun.vertical) * (static_cast<float>(shots) + first - 1.0f);
		const float max_degrees = blend(config_.max_degrees, config_.one_hand_max_degrees);
		float degrees = before.degrees + config_.degrees_per_shot * vertical * blend(1.0f, config_.one_hand_climb);
		float yaw = before.yaw;
		for (int i = 0; i < shots; ++i)
		{
			yaw += hold * (gun.yaw_min + (gun.yaw_max - gun.yaw_min) * random());
			if (penalty > 0.0f)
			{
				// Thrown about: sideways either way, mostly up.
				yaw += penalty * hold * config_.one_hand_scatter * (2.0f * random() - 1.0f);
				degrees += penalty * hold * config_.one_hand_scatter * (1.5f * random() - 0.5f);
			}
		}
		state_.degrees = std::clamp(degrees, -0.25f * max_degrees, max_degrees);
		state_.back = std::min(blend(config_.max_back, config_.one_hand_max_back), before.back + config_.back_per_shot * vertical * blend(1.0f, config_.one_hand_back));
		const float max_yaw = blend(config_.max_yaw, config_.one_hand_max_yaw);
		state_.yaw = std::clamp(yaw, -max_yaw, max_yaw);
		recovery_ = std::clamp(gun.recovery, 0.25f, 4.0f) * blend(1.0f, config_.one_hand_recovery);
		last_ = now;
	}

	recoil_offset gun_recoil::at(double now) const
	{
		return decayed(now);
	}

	recoil_profile profile_from_game(const game_recoil& gun, const game_recoil& reference, const recoil_config& config)
	{
		const auto usable = [](float value) { return std::isfinite(value) && value > 0.0f; };
		if (!usable(gun.vertical) || !usable(reference.vertical) || !std::isfinite(gun.horizontal_min) || !std::isfinite(gun.horizontal_max))
		{
			return {};
		}
		recoil_profile profile;
		profile.vertical = std::clamp(gun.vertical / reference.vertical, 0.2f, 4.0f);
		profile.first_shot = std::isfinite(gun.first_shot) ? std::clamp(gun.first_shot, 1.0f, 4.0f) : 1.0f;
		// The game's angles are its small camera kick; the drawn gun shows them at the vertical kick's
		// gain, halved sideways so the muzzle wanders without leaving the sights.
		const float gain = 0.5f * config.degrees_per_shot / reference.vertical;
		profile.yaw_min = std::clamp(std::min(gun.horizontal_min, gun.horizontal_max) * gain, -config.max_yaw, config.max_yaw);
		profile.yaw_max = std::clamp(std::max(gun.horizontal_min, gun.horizontal_max) * gain, -config.max_yaw, config.max_yaw);
		if (usable(gun.decrease) && usable(reference.decrease))
		{
			profile.recovery = std::clamp(reference.decrease / gun.decrease, 0.5f, 2.0f);
		}
		return profile;
	}
}
