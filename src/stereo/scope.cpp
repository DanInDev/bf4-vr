#include "stereo/scope.hpp"

#include <windows.h>

#include <cmath>
#include <cstdio>
#include <mutex>

#include "body/arm_pose.hpp"
#include "common/settings_store.hpp"
#include "input/vr_controls.hpp"
#include "stereo/scope_compose.hpp"
#include "stereo/scope_math.hpp"

namespace bfvr::stereo
{
	namespace
	{
		constexpr float degree = 3.14159265f / 180.0f;
		// The game zooms the red dot 45 -> 40 deg (1.14x): no lens picture below this.
		constexpr float min_magnification = 1.25f;
		constexpr ULONGLONG hip_settle_ms = 1000; // the zoom eases back after aiming
		constexpr ULONGLONG weapon_fresh_ms = 250;
		// AK-12 flip magnifier, measured against the drawn gun in ADS (re/notes/weapon.md, "Magnified sights"):
		// its housing 522 px apart between the eyes (64 mm) = 14.9 cm ahead of the sight's eye point, the
		// rear lens opening 1.45 cm in radius.
		constexpr scope_math::lens magnifier_lens{ 0.148f, 0.0145f, 0.3f };

		std::mutex lock;
		scope_request request;
		float game_fov{};
		float hip_fov{ 45.0f * degree };
		ULONGLONG aiming_changed_at{};
		bool was_aiming{};

		float perpendicular(const pose& eye, const float (&point)[3], const float (&direction)[3])
		{
			const float d[3] = { eye.position[0] - point[0], eye.position[1] - point[1], eye.position[2] - point[2] };
			const float along = d[0] * direction[0] + d[1] * direction[1] + d[2] * direction[2];
			const float p[3] = { d[0] - along * direction[0], d[1] - along * direction[1], d[2] - along * direction[2] };
			return std::sqrt(p[0] * p[0] + p[1] * p[1] + p[2] * p[2]);
		}

		void sight_direction(const body::weapon_points& points, float (&direction)[3])
		{
			const float* q = points.root_rotation;
			const float* v = points.sight_direction;
			const float t[3] = { 2 * (q[1] * v[2] - q[2] * v[1]), 2 * (q[2] * v[0] - q[0] * v[2]), 2 * (q[0] * v[1] - q[1] * v[0]) };
			direction[0] = v[0] + q[3] * t[0] + (q[1] * t[2] - q[2] * t[1]);
			direction[1] = v[1] + q[3] * t[1] + (q[2] * t[0] - q[0] * t[2]);
			direction[2] = v[2] + q[3] * t[2] + (q[0] * t[1] - q[1] * t[0]);
			const float n = std::sqrt(direction[0] * direction[0] + direction[1] * direction[1] + direction[2] * direction[2]);
			for (float& c : direction)
			{
				c = n > 0.0f ? c / n : 0.0f;
			}
		}
	}

	void scope_on_view_copy(float game_fov_y, const pose& rendered_eye, const head_sample& head)
	{
		const ULONGLONG now = GetTickCount64();
		const bool aiming = input::game_aiming();
		if (aiming != was_aiming)
		{
			aiming_changed_at = now;
			was_aiming = aiming;
		}
		if (!aiming && now - aiming_changed_at > hip_settle_ms && game_fov_y > 10.0f * degree && game_fov_y < 170.0f * degree)
		{
			hip_fov = game_fov_y;
		}
		const settings::registry& store = settings::global();
		const float game_zoom = scope_math::magnification(hip_fov, game_fov_y);
		scope_request next;
		next.at = now;
		next.magnification = game_zoom * store.real("scope_zoom", 1.0f);
		const body::weapon_points points = body::latest_weapon_points();
		if (store.boolean("scope", true) && aiming && game_zoom >= min_magnification && points.has_sight && points.has_root
			&& now - points.at < weapon_fresh_ms && head.valid)
		{
			float direction[3];
			sight_direction(points, direction);
			const int sighting = perpendicular(head.eyes[0], points.sight_eye, direction) <= perpendicular(head.eyes[1], points.sight_eye, direction) ? 0 : 1;
			const pose& eye = head.eyes[sighting];
			next.eye = sighting;
			const scope_math::plan plan = scope_math::plan_scope(eye, points.sight_eye, points.root_rotation, points.sight_direction, next.magnification, magnifier_lens);
			if (plan.valid)
			{
				next.active = true;
				next.relative = scope_math::relative_pose(rendered_eye, plan.camera);
				next.render_fov_y = plan.render_fov_y;
				next.crop = magnifier_lens.crop;
				next.quad = plan.quad;
				next.quad_size = plan.quad_size;
			}
		}
		const std::lock_guard guard(lock);
		game_fov = game_fov_y;
		request = next;
	}

	scope_request latest_scope_request()
	{
		const std::lock_guard guard(lock);
		return request;
	}

	std::string scope_report()
	{
		scope_request r;
		float game{}, hip{};
		{
			const std::lock_guard guard(lock);
			r = request;
			game = game_fov;
			hip = hip_fov;
		}
		char text[512];
		std::snprintf(text, sizeof(text),
			"scope game_fov=%.2f hip_fov=%.2f magnification=%.3f aiming=%d active=%d eye=%d render_fov=%.2f quad=(%.3f %.3f %.3f) size=%.3f relative=(%.3f %.3f %.3f)\n%s",
			game / degree, hip / degree, r.magnification, input::game_aiming() ? 1 : 0, r.active ? 1 : 0, r.eye, r.render_fov_y / degree, r.quad.position[0], r.quad.position[1],
			r.quad.position[2], r.quad_size, r.relative.position[0], r.relative.position[1], r.relative.position[2], scope_compose_report().c_str());
		return text;
	}
}
