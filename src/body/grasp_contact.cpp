#include "body/grasp_contact.hpp"

#include <algorithm>
#include <cmath>

#include "body/finger_pose.hpp"

namespace bfvr::body::contact
{
	float signed_distance(const box& shape, ik::vec3 point)
	{
		const ik::vec3 p = ik::rotate(ik::conjugate(shape.frame), point - shape.centre);
		const ik::vec3 q{ std::fabs(p.x) - shape.half.x, std::fabs(p.y) - shape.half.y, std::fabs(p.z) - shape.half.z };
		const ik::vec3 outside{ std::fmax(q.x, 0.0f), std::fmax(q.y, 0.0f), std::fmax(q.z, 0.0f) };
		return ik::length(outside) + std::fmin(std::fmax(q.x, std::fmax(q.y, q.z)), 0.0f);
	}

	void couple_free_fingers(float (&curl)[5], const bool (&touched)[5], float slack)
	{
		float limit = -1.0f;
		for (int finger = 1; finger < 5; ++finger)
		{
			if (touched[finger])
			{
				limit = std::fmax(limit, curl[finger]);
			}
		}
		if (limit < 0.0f)
		{
			return;
		}
		for (int finger = 1; finger < 5; ++finger)
		{
			if (!touched[finger])
			{
				curl[finger] = std::fmin(curl[finger], std::fmin(1.0f, limit + slack));
			}
		}
	}

	cup_placement cup_magazine(ik::vec3 palm_centre, ik::vec3 palm_normal, ik::vec3 finger_direction, const magazine_size& size, float palm_gap)
	{
		cup_placement out;
		if (ik::length(palm_normal) < 1e-4f)
		{
			return out;
		}
		const ik::vec3 y = ik::normalize(palm_normal);
		const ik::vec3 along = finger_direction - y * ik::dot(finger_direction, y);
		if (ik::length(along) < 1e-4f)
		{
			return out;
		}
		const ik::vec3 z = ik::normalize(along);
		out.frame = fingers::from_basis(ik::cross(y, z), y, z);
		out.top = palm_centre + y * (palm_gap + size.height);
		out.body.centre = palm_centre + y * (palm_gap + 0.5f * size.height);
		out.body.frame = out.frame;
		out.body.half = { 0.5f * size.width, 0.5f * size.height, 0.5f * size.depth };
		out.valid = true;
		return out;
	}

	ik::vec3 pinch_point(ik::vec3 index_side, ik::vec3 thumb_tip, float knob_half, float finger_radius)
	{
		const ik::vec3 towards = thumb_tip - index_side;
		const float apart = ik::length(towards);
		if (apart < 1e-5f)
		{
			return index_side;
		}
		// Centred between the two; never closer to the index than resting on it.
		return index_side + towards * (std::fmax(0.5f * apart, knob_half + finger_radius) / apart);
	}

	bool cup_grip(bool cupped, float palm_up, float enter, float leave)
	{
		if (!std::isfinite(palm_up))
		{
			return cupped;
		}
		return cupped ? palm_up > leave : palm_up >= enter;
	}

	float support_slide::update(bool held, float along, float rest, float back, float forward, bool lock)
	{
		const float low = -std::fmax(back, 0.0f);
		const float high = std::fmax(forward, 0.0f);
		const float wanted = std::isfinite(along) ? std::fmax(low, std::fmin(high, along - rest)) : 0.0f;
		if (!held || !lock)
		{
			latched_ = false;
			return rest + wanted;
		}
		if (!latched_)
		{
			latched_ = true;
			offset_ = wanted;
		}
		// A range changed while held (a setting) still bounds the kept place.
		return rest + std::fmax(low, std::fmin(high, offset_));
	}

	ik::vec3 rack_fist_offset(bool underhand, float side, const rack_fist_offsets& offsets)
	{
		const float s = side > 0.0f ? 1.0f : side < 0.0f ? -1.0f : 0.0f;
		return underhand ? ik::vec3{ s * offsets.under_out, -offsets.under_down, 0.0f } : ik::vec3{ s * offsets.over_out, offsets.over_up, 0.0f };
	}

	bool underhand_rack(bool underhand, float palm_up, float enter, float leave)
	{
		if (!std::isfinite(palm_up))
		{
			return underhand;
		}
		return palm_up >= enter ? true : palm_up <= leave ? false : underhand;
	}

	void tilt_magazine(ik::vec3& top, ik::quat& rotation, ik::quat seated, float angle, float pivot_drop)
	{
		if (angle == 0.0f)
		{
			return;
		}
		const ik::quat gun = ik::normalize(rotation * ik::conjugate(seated));
		const ik::vec3 pivot = top + ik::rotate(gun, ik::vec3{ 0.0f, -pivot_drop, 0.0f });
		const ik::vec3 axis = ik::rotate(gun, ik::vec3{ 0.0f, 0.0f, 1.0f });
		// About +z a positive angle takes +y towards -x: the top goes left (+x) for a negative one.
		const float s = std::sin(-0.5f * angle);
		const ik::quat turn{ axis.x * s, axis.y * s, axis.z * s, std::cos(-0.5f * angle) };
		top = pivot + ik::rotate(turn, top - pivot);
		rotation = ik::normalize(turn * rotation);
	}

	box magazine_box(ik::vec3 top, ik::quat rotation, ik::quat seated, const magazine_size& size)
	{
		const ik::quat gun = ik::normalize(rotation * ik::conjugate(seated));
		box out;
		out.frame = gun;
		out.centre = top + ik::rotate(gun, ik::vec3{ 0.0f, -0.5f * size.height, 0.0f });
		out.half = { 0.5f * size.width, 0.5f * size.height, 0.5f * size.depth };
		return out;
	}

	void beside_magazine(ik::vec3& top, ik::quat rotation, ik::quat seated, const magazine_size& size, float gap)
	{
		const ik::quat gun = ik::normalize(rotation * ik::conjugate(seated));
		top = top + ik::rotate(gun, ik::vec3{ -(size.width + gap), 0.0f, 0.0f });
	}

	void slide_rest::hold(unsigned key, float seconds, ik::bone_transform& slide)
	{
		const bool finite = std::isfinite(slide.position.x) && std::isfinite(slide.position.y) && std::isfinite(slide.position.z);
		if (key != key_)
		{
			reset();
			key_ = key;
		}
		if (!finite)
		{
			if (valid_)
			{
				slide = rest_;
			}
			return;
		}
		const float dx = slide.position.x - last_.position.x, dy = slide.position.y - last_.position.y, dz = slide.position.z - last_.position.z;
		const bool still = has_last_ && std::sqrt(dx * dx + dy * dy + dz * dz) <= steady_metres;
		still_ = still ? still_ + (std::isfinite(seconds) && seconds > 0.0f ? seconds : 0.0f) : 0.0f;
		if (!still)
		{
			last_ = slide; // still: the reference stays, so a slow creep is not taken for stillness
		}
		has_last_ = true;
		if (!valid_ || still_ >= steady_seconds)
		{
			rest_ = slide; // the first pose (a gun is drawn at rest), then whatever the animation holds still
			valid_ = true;
		}
		slide = rest_;
	}
}
