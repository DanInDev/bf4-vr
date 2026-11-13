#include "physics/loose_body.hpp"

#include <algorithm>
#include <cmath>

namespace bfvr::physics
{
	namespace
	{
		v3 add(v3 a, v3 b) { return { a.x + b.x, a.y + b.y, a.z + b.z }; }
		v3 sub(v3 a, v3 b) { return { a.x - b.x, a.y - b.y, a.z - b.z }; }
		v3 scale(v3 a, float k) { return { a.x * k, a.y * k, a.z * k }; }
		float dot(v3 a, v3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
		v3 cross(v3 a, v3 b) { return { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x }; }
		float length(v3 a) { return std::sqrt(dot(a, a)); }
		bool finite(v3 a) { return std::isfinite(a.x) && std::isfinite(a.y) && std::isfinite(a.z); }

		q4 multiply(q4 a, q4 b)
		{
			return { a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y, a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
				a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w, a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z };
		}
		q4 conjugate(q4 q) { return { -q.x, -q.y, -q.z, q.w }; }
		q4 normalized(q4 q)
		{
			const float n = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
			return n > 1e-6f ? q4{ q.x / n, q.y / n, q.z / n, q.w / n } : q4{};
		}
		v3 rotate(q4 q, v3 v)
		{
			const v3 u{ q.x, q.y, q.z };
			const v3 t = scale(cross(u, v), 2.0f);
			return add(add(v, scale(t, q.w)), cross(u, t));
		}
		v3 clamp_length(v3 v, float most)
		{
			const float n = length(v);
			return n > most && n > 0.0f ? scale(v, most / n) : v;
		}
	}

	void loose_body::start(const v3& pose_position, const q4& pose_rotation, const v3& velocity, const v3& spin, const loose_shape& shape)
	{
		shape_ = shape;
		shape_.axes = normalized(shape.axes);
		shape_.mass = shape.mass > 0.01f ? shape.mass : 0.01f;
		active_ = true;
		resting_ = touching_ = ever_known_ = false;
		age_ = still_for_ = rest_age_ = 0.0f;
		clearance_ = 0.0f;
		bounces_ = 0;
		rotation_ = normalized(pose_rotation);
		const v3 offset = rotate(rotation_, shape_.centre);
		centre_ = add(pose_position, offset);
		spin_ = finite(spin) ? clamp_length(spin, config_.max_spin) : v3{};
		const v3 pose_velocity = finite(velocity) ? velocity : v3{};
		velocity_ = clamp_length(add(pose_velocity, cross(spin_, offset)), config_.max_speed);
	}

	v3 loose_body::pose_position() const
	{
		return sub(centre_, rotate(rotation_, shape_.centre));
	}

	void loose_body::step(float dt, ground_source& ground, float fallback_y)
	{
		velocity_.y -= config_.gravity * dt;
		const float drag = std::exp(-config_.air_drag * dt);
		velocity_ = scale(velocity_, drag);
		spin_ = scale(spin_, drag);
		centre_ = add(centre_, scale(velocity_, dt));
		const float turn = length(spin_) * dt;
		if (turn > 1e-7f)
		{
			const v3 axis = scale(spin_, 1.0f / length(spin_));
			const float s = std::sin(0.5f * turn);
			rotation_ = normalized(multiply(q4{ axis.x * s, axis.y * s, axis.z * s, std::cos(0.5f * turn) }, rotation_));
		}

		// The box's corners against the ground under each of them.
		const q4 box = multiply(rotation_, shape_.axes);
		const v3 h = shape_.half;
		const float inertia_scale = shape_.mass / 3.0f;
		const v3 inverse_inertia{ 1.0f / (inertia_scale * (h.y * h.y + h.z * h.z)), 1.0f / (inertia_scale * (h.x * h.x + h.z * h.z)),
			1.0f / (inertia_scale * (h.x * h.x + h.y * h.y)) };
		const auto apply_inverse_inertia = [&](v3 world)
		{
			const v3 local = rotate(conjugate(box), world);
			return rotate(box, { local.x * inverse_inertia.x, local.y * inverse_inertia.y, local.z * inverse_inertia.z });
		};
		struct contact
		{
			v3 r;
			v3 normal;
			float depth;
		};
		contact contacts[8];
		int count = 0;
		float lowest = 1e9f;
		v3 push{};
		float deepest = 0.0f;
		for (int corner = 0; corner < 8; ++corner)
		{
			const v3 sign{ corner & 1 ? 1.0f : -1.0f, corner & 2 ? 1.0f : -1.0f, corner & 4 ? 1.0f : -1.0f };
			const v3 r = rotate(box, { sign.x * h.x, sign.y * h.y, sign.z * h.z });
			const v3 at = add(centre_, r);
			ground_plane plane = ground.below(at);
			if (plane.what == ground_plane::kind::unknown && std::isfinite(fallback_y))
			{
				plane = { ground_plane::kind::plane, { at.x, fallback_y, at.z }, { 0, 1, 0 } };
			}
			if (plane.what != ground_plane::kind::plane)
			{
				continue;
			}
			ever_known_ = true;
			const float depth = dot(plane.normal, sub(at, plane.point));
			lowest = std::min(lowest, depth);
			if (depth < 0.0f)
			{
				contacts[count++] = { r, plane.normal, depth };
				if (depth < deepest)
				{
					deepest = depth;
					push = scale(plane.normal, -depth);
				}
			}
		}
		clearance_ = lowest < 1e8f ? lowest : 0.0f;
		touching_ = count > 0;
		if (!touching_)
		{
			return;
		}
		centre_ = add(centre_, push);
		const float inverse_mass = 1.0f / shape_.mass;
		bool impact = false;
		for (int iteration = 0; iteration < 4; ++iteration)
		{
			for (int i = 0; i < count; ++i)
			{
				const contact& c = contacts[i];
				const v3 at_velocity = add(velocity_, cross(spin_, c.r));
				const float normal_speed = dot(at_velocity, c.normal);
				if (normal_speed >= 0.0f)
				{
					continue;
				}
				const bool bounce = iteration == 0 && -normal_speed > config_.bounce_speed;
				impact = impact || bounce;
				const float restitution = bounce ? config_.restitution : 0.0f;
				const v3 rn = cross(c.r, c.normal);
				const float normal_mass = inverse_mass + dot(c.normal, cross(apply_inverse_inertia(rn), c.r));
				const float jn = -(1.0f + restitution) * normal_speed / normal_mass;
				velocity_ = add(velocity_, scale(c.normal, jn * inverse_mass));
				spin_ = add(spin_, apply_inverse_inertia(cross(c.r, scale(c.normal, jn))));
				// Friction against the sliding at the corner, at most friction * the normal push.
				const v3 after = add(velocity_, cross(spin_, c.r));
				const v3 sliding = sub(after, scale(c.normal, dot(after, c.normal)));
				const float slide_speed = length(sliding);
				if (slide_speed > 1e-5f)
				{
					const v3 t = scale(sliding, 1.0f / slide_speed);
					const v3 rt = cross(c.r, t);
					const float tangent_mass = inverse_mass + dot(t, cross(apply_inverse_inertia(rt), c.r));
					const float jt = -std::min(slide_speed / tangent_mass, config_.friction * jn);
					velocity_ = add(velocity_, scale(t, jt * inverse_mass));
					spin_ = add(spin_, apply_inverse_inertia(cross(c.r, scale(t, jt))));
				}
			}
		}
		if (impact)
		{
			++bounces_;
		}
		spin_ = scale(spin_, std::exp(-config_.contact_damping * dt));
	}

	bool loose_body::update(float seconds, ground_source& ground, float fallback_y, bool seen)
	{
		if (!active_)
		{
			return false;
		}
		seconds = std::clamp(std::isfinite(seconds) ? seconds : 0.0f, 0.0f, 0.1f);
		age_ += seconds;
		if (resting_)
		{
			rest_age_ += seconds;
			if (rest_age_ >= config_.lie_seconds && !seen)
			{
				active_ = false;
			}
			return active_;
		}
		const int steps = std::clamp(static_cast<int>(std::ceil(seconds / config_.substep)), 1, 40);
		const float dt = seconds / static_cast<float>(steps);
		for (int i = 0; i < steps && seconds > 0.0f; ++i)
		{
			step(dt, ground, fallback_y);
			if (touching_ && length(velocity_) < config_.rest_speed && length(spin_) < config_.rest_spin)
			{
				still_for_ += dt;
				if (still_for_ >= config_.rest_settle)
				{
					resting_ = true;
					velocity_ = spin_ = {};
					break;
				}
			}
			else
			{
				still_for_ = 0.0f;
			}
		}
		if (!touching_ && !resting_ && age_ >= config_.max_fall_seconds && (!ever_known_ || !seen))
		{
			active_ = false;
		}
		return active_;
	}
}
