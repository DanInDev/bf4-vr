#include "weapon/free_hands.hpp"

#include <cmath>

namespace bfvr::weapon
{
	void free_hands::reset()
	{
		dropped_ = false;
		open_for_ = 0.0f;
		last_grip_ = true;
	}

	free_hands_result free_hands::update(const free_hands_frame& frame)
	{
		free_hands_result result;
		const bool pressed = frame.grip && !last_grip_;
		last_grip_ = frame.grip;
		if (!frame.enabled || !frame.gun_in_hand || frame.weapon_changed)
		{
			// Off, a gadget or the knife out, or a new weapon drawn: whatever was dropped is in hand again.
			reset();
			last_grip_ = frame.grip;
			return result;
		}
		if (!dropped_)
		{
			// While the grip belongs to something else (or tracking is lost) the gun stays in hand.
			if (frame.grip || frame.busy || !frame.hand_valid)
			{
				open_for_ = 0.0f;
				return result;
			}
			open_for_ += frame.seconds > 0.0f ? frame.seconds : 0.0f;
			if (open_for_ + 1e-4f < config_.release_seconds)
			{
				return result;
			}
			dropped_ = true;
			position_ = { frame.gun_hand.x, frame.floor_y + config_.lie_height, frame.gun_hand.z };
			yaw_ = frame.hand_yaw;
			result.event = free_hands_event::dropped;
		}
		const float dx = frame.gun_hand.x - position_.x, dy = frame.gun_hand.y - position_.y, dz = frame.gun_hand.z - position_.z;
		const bool near = frame.hand_valid && std::sqrt(dx * dx + dy * dy + dz * dz) < config_.pickup_reach;
		if (result.event == free_hands_event::none && pressed && near && !frame.busy)
		{
			dropped_ = false;
			open_for_ = 0.0f;
			result.event = free_hands_event::picked_up;
			return result;
		}
		result.dropped = true;
		result.position = position_;
		result.yaw = yaw_;
		result.block_fire = true;
		result.in_reach = near;
		return result;
	}
}
