#include "weapon/free_hands.hpp"

#include <cmath>

namespace bfvr::weapon
{
	void free_hands::reset()
	{
		dropped_ = false;
		slung_ = false;
		in_off_hand_ = false;
		in_off_grip_ = false;
		open_for_ = 0.0f;
		off_open_for_ = 0.0f;
		last_grip_ = true;
		last_off_grip_ = true;
	}

	free_hands_result free_hands::update(const free_hands_frame& frame)
	{
		free_hands_result result;
		const bool pressed = frame.grip && !last_grip_;
		last_grip_ = frame.grip;
		const bool off_pressed = frame.off_grip && !last_off_grip_;
		last_off_grip_ = frame.off_grip;
		if (!frame.enabled || !frame.gun_in_hand || frame.weapon_changed)
		{
			// Off, a gadget or the knife out, or a new weapon drawn: whatever was dropped is in hand again.
			reset();
			last_grip_ = frame.grip;
			return result;
		}
		const float step = frame.seconds > 0.0f ? frame.seconds : 0.0f;
		if (in_off_grip_)
		{
			// Held by the pistol grip in the off hand: it fires with the off-hand trigger. Letting go drops it
			// (onto the sling or the floor); the gun hand picks it up from there.
			result.in_off_grip = true;
			if (frame.off_grip || !frame.off_valid)
			{
				off_open_for_ = 0.0f;
				return result;
			}
			off_open_for_ += step;
			if (off_open_for_ + 1e-4f < config_.release_seconds)
			{
				return result;
			}
			in_off_grip_ = false;
			dropped_ = true;
			slung_ = frame.sling && frame.sling_valid;
			position_ = { frame.off_hand.x, frame.floor_y + config_.lie_height, frame.off_hand.z };
			yaw_ = frame.off_yaw;
			result.in_off_grip = false;
			result.event = free_hands_event::dropped;
		}
		else if (in_off_hand_)
		{
			// Held by the handguard: the gun hand takes it back at the pistol grip, the off hand drops it.
			result.in_off_hand = true;
			result.block_fire = true;
			float gap = 1e9f;
			if (frame.has_grip_point)
			{
				const float gx = frame.gun_hand.x - frame.grip_point.x, gy = frame.gun_hand.y - frame.grip_point.y, gz = frame.gun_hand.z - frame.grip_point.z;
				gap = std::sqrt(gx * gx + gy * gy + gz * gz);
			}
			result.grip_in_reach = frame.hand_valid && gap < config_.regrip_reach;
			if (pressed && result.grip_in_reach && !frame.busy)
			{
				in_off_hand_ = false;
				open_for_ = 0.0f;
				result.in_off_hand = false;
				result.block_fire = false;
				result.event = free_hands_event::taken_back;
				return result;
			}
			if (frame.off_grip || !frame.off_valid)
			{
				off_open_for_ = 0.0f;
				return result;
			}
			off_open_for_ += step;
			if (off_open_for_ + 1e-4f < config_.release_seconds)
			{
				return result;
			}
			in_off_hand_ = false;
			dropped_ = true;
			slung_ = frame.sling && frame.sling_valid;
			position_ = { frame.off_hand.x, frame.floor_y + config_.lie_height, frame.off_hand.z };
			yaw_ = frame.off_yaw;
			result.in_off_hand = false;
			result.event = free_hands_event::dropped;
		}
		else if (!dropped_)
		{
			// While the grip belongs to something else (or tracking is lost) the gun stays in hand.
			if (frame.grip || frame.busy || !frame.hand_valid)
			{
				open_for_ = 0.0f;
				return result;
			}
			open_for_ += step;
			if (open_for_ + 1e-4f < config_.release_seconds)
			{
				return result;
			}
			open_for_ = 0.0f;
			if (frame.off_holds && frame.off_valid)
			{
				// The off hand has the handguard: it keeps the gun, the gun hand is free.
				in_off_hand_ = true;
				off_open_for_ = 0.0f;
				result.in_off_hand = true;
				result.block_fire = true;
				result.event = free_hands_event::handed_to_off;
				return result;
			}
			dropped_ = true;
			slung_ = frame.sling && frame.sling_valid;
			position_ = { frame.gun_hand.x, frame.floor_y + config_.lie_height, frame.gun_hand.z };
			yaw_ = frame.hand_yaw;
			result.event = free_hands_event::dropped;
		}
		if (slung_ && frame.sling_valid)
		{
			// On the sling it goes where the body goes.
			position_ = frame.sling_at;
			yaw_ = frame.sling_yaw;
		}
		const float dx = frame.gun_hand.x - position_.x, dy = frame.gun_hand.y - position_.y, dz = frame.gun_hand.z - position_.z;
		const bool near = frame.hand_valid && std::sqrt(dx * dx + dy * dy + dz * dz) < (slung_ ? config_.sling_reach : config_.pickup_reach);
		// #49: off the sling at its holster; the holster saw its own press (a frame earlier), so the grip only has to
		// be still closed. A gun on the floor is picked up where it lies.
		if (result.event == free_hands_event::none && ((pressed && near) || (frame.holster_take && frame.grip && slung_)) && !frame.busy)
		{
			dropped_ = false;
			slung_ = false;
			open_for_ = 0.0f;
			result.event = free_hands_event::picked_up;
			return result;
		}
		// The off hand may pick it up too: it holds it by the pistol grip.
		const float ox = frame.off_hand.x - position_.x, oy = frame.off_hand.y - position_.y, oz = frame.off_hand.z - position_.z;
		const bool off_near = frame.off_valid && std::sqrt(ox * ox + oy * oy + oz * oz) < (slung_ ? config_.sling_reach : config_.pickup_reach);
		if (result.event == free_hands_event::none && off_pressed && off_near && !frame.off_busy)
		{
			dropped_ = false;
			slung_ = false;
			in_off_grip_ = true;
			off_open_for_ = 0.0f;
			result.in_off_grip = true;
			result.event = free_hands_event::picked_up_off;
			return result;
		}
		result.off_in_reach = off_near;
		result.dropped = true;
		result.slung = slung_;
		result.position = position_;
		result.yaw = yaw_;
		result.block_fire = true;
		result.in_reach = near;
		return result;
	}
}
