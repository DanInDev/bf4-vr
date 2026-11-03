#include "weapon/two_hand.hpp"

#include <algorithm>
#include <cmath>

#include "body/arm_ik.hpp"

namespace bfvr::weapon
{
	namespace
	{
		using body::ik::quat;
		using body::ik::vec3;

		vec3 to_vec(const float (&v)[3])
		{
			return { v[0], v[1], v[2] };
		}

		quat to_quat(const float (&q)[4])
		{
			return body::ik::normalize(quat{ q[0], q[1], q[2], q[3] });
		}

		void store(quat q, float (&out)[4])
		{
			out[0] = q.x;
			out[1] = q.y;
			out[2] = q.z;
			out[3] = q.w;
		}

		// Normalised linear blend on the shorter arc; plenty for blending over a tenth of a second.
		quat blend(quat a, quat b, float t)
		{
			const float d = a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
			const float s = d < 0 ? -1.0f : 1.0f;
			return body::ik::normalize(quat{ a.x + (s * b.x - a.x) * t, a.y + (s * b.y - a.y) * t, a.z + (s * b.z - a.z) * t, a.w + (s * b.w - a.w) * t });
		}
	}

	bool on_handguard(const stereo::pose& gun, const float (&off_hand)[3], const two_hand_config& config)
	{
		const vec3 barrel = body::ik::rotate(to_quat(gun.orientation), { 0, 0, -1 });
		const vec3 offset = to_vec(off_hand) - to_vec(gun.position);
		const float along = body::ik::dot(offset, barrel);
		if (along < config.grab_near || along > config.grab_far)
		{
			return false;
		}
		return body::ik::length(offset - barrel * along) <= config.grab_reach;
	}

	two_hand_output two_hand_grip::update(const two_hand_input& input)
	{
		const quat one_handed = to_quat(input.gun.orientation);
		const vec3 line = to_vec(input.off_hand) - to_vec(input.gun.position);
		const float separation = body::ik::length(line);
		const bool pressed = input.grip && !last_grip_;
		last_grip_ = input.grip;
		const bool trigger_was_down = trigger_down_;
		trigger_down_ = trigger_down_ ? input.trigger > config_.trigger_release : input.trigger >= config_.trigger_press;

		if (!held_ && pressed && input.tracked && !input.busy && on_handguard(input.gun, input.off_hand, config_))
		{
			// Remember the hand line in the gun hand's axes: the gun keeps its pose at the grab and
			// from then on turns with the line.
			const vec3 local = body::ik::rotate(body::ik::conjugate(one_handed), line * (1.0f / separation));
			line_in_gun_[0] = local.x;
			line_in_gun_[1] = local.y;
			line_in_gun_[2] = local.z;
			held_ = true;
		}
		untracked_ = held_ && !input.tracked ? untracked_ + std::max(input.seconds, 0.0f) : 0.0f;
		if (held_ && (!input.grip || input.busy || untracked_ > config_.release_grace))
		{
			held_ = false;
		}

		if (input.tracked && separation >= config_.min_separation && (held_ || weight_ > 0))
		{
			if (config_.lock)
			{
				// An off hand pulled back past the grip (still gripping) says nothing about the barrel: the
				// gun keeps the last two-handed direction rather than swinging round towards the player.
				if (body::ik::dot(line, body::ik::rotate(one_handed, { 0, 0, -1 })) >= config_.grab_near)
				{
					// The barrel through the gun hand, parallel to the hand line once the off hand's place
					// beside the barrel is taken out; that place turns with the gun, so settle it twice.
					quat rotation = one_handed;
					for (int pass = 0; pass < 3; ++pass)
					{
						const vec3 beside = body::ik::rotate(rotation, { input.support_offset[0], input.support_offset[1], 0 });
						const vec3 along = line - beside;
						if (body::ik::length(along) < config_.min_separation)
						{
							break;
						}
						rotation = body::ik::normalize(body::ik::from_to(body::ik::rotate(rotation, { 0, 0, -1 }), body::ik::normalize(along)) * rotation);
					}
					store(rotation, two_handed_);
				}
			}
			else
			{
				const vec3 remembered = body::ik::rotate(one_handed, to_vec(line_in_gun_));
				store(body::ik::normalize(body::ik::from_to(remembered, line * (1.0f / separation)) * one_handed), two_handed_);
			}
		}

		const float rate = config_.blend_time > 0 ? std::max(input.seconds, 0.0f) / config_.blend_time : 1.0f;
		weight_ = std::clamp(weight_ + (held_ ? rate : -rate), 0.0f, 1.0f);

		two_hand_output output;
		output.held = held_;
		output.weight = weight_;
		output.claim_grip = held_ || (input.tracked && !input.busy && on_handguard(input.gun, input.off_hand, config_));
		output.claim_trigger = held_;
		output.toggle_accessory = held_ && trigger_down_ && !trigger_was_down;
		store(weight_ > 0 ? blend(one_handed, to_quat(two_handed_), weight_) : one_handed, output.rotation);
		return output;
	}

	void two_hand_grip::reset()
	{
		*this = two_hand_grip(config_);
	}

	void drawn_barrel_rotation(const float (&root_rotation)[4], float (&rotation)[4], const float* sight_direction)
	{
		// root * (180 degrees about y): -z of the result is +z of the root; with a sight line, first
		// turned from the barrel onto it (in Wep_Root axes).
		body::ik::quat onto_sight{};
		if (sight_direction)
		{
			onto_sight = body::ik::from_to({ 0, 0, 1 }, { sight_direction[0], sight_direction[1], sight_direction[2] });
		}
		const body::ik::quat turned = body::ik::normalize(to_quat(root_rotation) * onto_sight * body::ik::quat{ 0, 1, 0, 0 });
		rotation[0] = turned.x;
		rotation[1] = turned.y;
		rotation[2] = turned.z;
		rotation[3] = turned.w;
	}

	bool eye_behind_sight(const float (&eye)[3], const float (&sight_eye)[3], const float (&direction)[3], float max_offset, float max_behind)
	{
		float v[3];
		float along = 0.0f;
		for (int axis = 0; axis < 3; ++axis)
		{
			v[axis] = eye[axis] - sight_eye[axis];
			along += v[axis] * direction[axis];
		}
		float off = 0.0f;
		for (int axis = 0; axis < 3; ++axis)
		{
			const float d = v[axis] - along * direction[axis];
			off += d * d;
		}
		return along >= -max_behind && along <= 0.05f && off <= max_offset * max_offset;
	}

	void converged_aim(const float (&origin)[3], const float (&grip)[3], const float (&rotation)[4], float convergence, float (&direction)[3])
	{
		const vec3 barrel = body::ik::rotate(to_quat(rotation), { 0, 0, -1 });
		const vec3 target = to_vec(grip) + barrel * std::max(convergence, 0.5f);
		const vec3 aim = body::ik::normalize(target - to_vec(origin));
		direction[0] = aim.x;
		direction[1] = aim.y;
		direction[2] = aim.z;
	}
}
