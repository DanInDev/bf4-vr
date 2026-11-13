#include "weapon/magazine_flight.hpp"

#include <algorithm>
#include <cmath>

namespace bfvr::weapon
{
	namespace
	{
		float length(const v3& a)
		{
			return std::sqrt(a.x * a.x + a.y * a.y + a.z * a.z);
		}

		q4 multiply(const q4& a, const q4& b)
		{
			return { a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y, a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
				a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w, a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z };
		}

		q4 normalized(const q4& q)
		{
			const float n = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
			return n > 1e-6f ? q4{ q.x / n, q.y / n, q.z / n, q.w / n } : q4{};
		}
	}

	void magazine_flight::start(const v3& position, const q4& rotation, const v3& velocity, bool floor_known, float floor_y)
	{
		active_ = true;
		resting_ = false;
		age_ = rest_age_ = 0.0f;
		position_ = position;
		rotation_ = rotation;
		velocity_ = std::isfinite(velocity.x) && std::isfinite(velocity.y) && std::isfinite(velocity.z) ? velocity : v3{};
		const float speed = length(velocity_);
		if (speed > config_.max_speed)
		{
			const float k = config_.max_speed / speed;
			velocity_ = { velocity_.x * k, velocity_.y * k, velocity_.z * k };
		}
		floor_known_ = floor_known && std::isfinite(floor_y) && floor_y < position.y;
		floor_y_ = floor_y;
		// Tumble end over end about the horizontal axis across the throw (x when thrown straight down or not at all).
		const float horizontal = std::sqrt(velocity_.x * velocity_.x + velocity_.z * velocity_.z);
		spin_axis_ = horizontal > 0.05f ? v3{ velocity_.z / horizontal, 0.0f, -velocity_.x / horizontal } : v3{ 1.0f, 0.0f, 0.0f };
		spin_ = std::min(config_.max_spin, config_.spin_per_speed * std::min(length(velocity_), config_.max_speed));
	}

	bool magazine_flight::update(float seconds)
	{
		if (!active_)
		{
			return false;
		}
		seconds = std::clamp(seconds, 0.0f, 0.1f);
		age_ += seconds;
		if (age_ >= config_.max_seconds || (!floor_known_ && age_ >= config_.fallback_seconds))
		{
			active_ = false;
			return false;
		}
		if (resting_)
		{
			rest_age_ += seconds;
			if (rest_age_ >= config_.rest_seconds)
			{
				active_ = false;
			}
			return active_;
		}
		velocity_.y -= config_.gravity * seconds;
		position_ = { position_.x + velocity_.x * seconds, position_.y + velocity_.y * seconds, position_.z + velocity_.z * seconds };
		if (spin_ > 0.0f)
		{
			const float half = 0.5f * spin_ * seconds;
			const float s = std::sin(half);
			rotation_ = normalized(multiply(q4{ spin_axis_.x * s, spin_axis_.y * s, spin_axis_.z * s, std::cos(half) }, rotation_));
		}
		const float rest_y = floor_y_ + config_.magazine_half_length;
		if (floor_known_ && position_.y <= rest_y && velocity_.y < 0.0f)
		{
			position_.y = rest_y;
			velocity_ = { velocity_.x * config_.floor_friction, -velocity_.y * config_.bounce, velocity_.z * config_.floor_friction };
			spin_ *= 0.4f;
			if (length(velocity_) < config_.rest_speed)
			{
				velocity_ = {};
				spin_ = 0.0f;
				resting_ = true;
			}
		}
		return true;
	}
}
