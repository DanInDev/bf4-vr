#include "stereo/scope.hpp"

#include <atomic>

#include <windows.h>

#include <cmath>
#include <cstdio>
#include <mutex>

#include "body/arm_pose.hpp"
#include "common/settings_store.hpp"
#include "input/vr_controls.hpp"
#include "stereo/eye_pass.hpp"
#include "stereo/lens_math.hpp"
#include "stereo/scope_compose.hpp"
#include "stereo/scope_math.hpp"
#include "weapon/sight_state.hpp"

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
		// #139 scope_glass: the picture is read by angle inside the optic's own glass; its render FOV is set from the
		// reach the glass needs (lens_math::glass_picture_reach), the disc over 45 % of the render's height (the scissor
		// keeps the rest out: sharper than 30 % for ~2x the scope render's pixels).
		constexpr scope_math::lens glass_lens{ 0.148f, 0.11f, 0.45f };
		// #39, sights live without the game's aim: with the magnifier flipped in (sight_state), or a sight
		// the game has zoomed through before, the lens picture also runs at the hip, at the zoom learned
		// while the game last aimed through it (the AK-12 magnifier's measured 1.73x until then), for an
		// eye within this far of the sight line.
		constexpr float magnifier_default = 1.73f;
		constexpr float hip_scope_reach = 0.03f;

		std::mutex lock;
		scope_request request;
		float game_fov{};
		float hip_fov{ 45.0f * degree };
		ULONGLONG aiming_changed_at{};
		bool was_aiming{};
		scope_math::magnification_memory learned;
		weapon::sight_state last_sight;
		std::atomic<float> optic_power{};
		bool last_at_hip{};

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
		const weapon::sight_state sight = weapon::read_sight_state();
		const std::lock_guard guard(lock);
		// The game's zoom for this weapon and level, once its zoom-in has settled.
		if (aiming && now - aiming_changed_at > hip_settle_ms && sight.valid && sight.current > 0)
		{
			learned.remember(sight.weapon, sight.current, game_zoom);
		}
		float zoom = game_zoom;
		bool wanted = aiming && game_zoom >= min_magnification;
		// At the hip: the magnifier (marked in the data), or any sight whose zoom the game has shown (a 4x).
		// Until the game has shown a level's zoom: the magnifier's measured 1.73x, a magnified scope's
		// nominal zoom (weapon/optics), else none.
		// #138: first the game's own zoom data for the selected level (every optic, no name table): a red dot or
		// holo comes out below min_magnification, so it gets no picture.
		const float data_zoom = weapon::zoom_power(sight);
		const bool magnifier = sight.selected_type == weapon::sight_type::magnifier;
		const bool scope_level = sight.selected_type == weapon::sight_type::none && sight.optics.magnified;
		const float unlearned = magnifier ? magnifier_default : scope_level ? sight.optics.magnification : 1.0f;
		const float hip_zoom = data_zoom > 0.0f ? data_zoom
			: sight.valid && sight.selected > 0 ? learned.lookup(sight.weapon, sight.selected, unlearned) : 1.0f;
		const bool at_hip = !aiming && hip_zoom >= min_magnification;
		optic_power = sight.valid && sight.selected > 0 ? hip_zoom : 0.0f;
		if (at_hip)
		{
			zoom = hip_zoom;
			wanted = true;
		}
		last_sight = sight;
		last_at_hip = at_hip;
		scope_request next;
		next.at = now;
		next.magnification = zoom * store.real("scope_zoom", 1.0f);
		const body::weapon_points points = body::latest_weapon_points();
		if (store.boolean("scope", true) && wanted && points.has_sight && points.has_root
			&& now - points.at < weapon_fresh_ms && head.valid)
		{
			float direction[3];
			sight_direction(points, direction);
			const float off[2] = { perpendicular(head.eyes[0], points.sight_eye, direction), perpendicular(head.eyes[1], points.sight_eye, direction) };
			const int sighting = off[0] <= off[1] ? 0 : 1;
			const pose& eye = head.eyes[sighting];
			next.eye = sighting;
			// With the glass on, the picture is read by angle (the optic's glass or a virtual eyepiece): the wide render.
			const scope_math::lens& optic = store.boolean("scope_glass", true) ? glass_lens : magnifier_lens;
			const scope_math::plan plan = scope_math::plan_scope(eye, points.sight_eye, points.root_rotation, points.sight_direction, next.magnification, optic);
			if (plan.valid && (!at_hip || off[sighting] <= hip_scope_reach))
			{
				next.active = true;
				next.relative = scope_math::relative_pose(rendered_eye, plan.camera);
				next.render_fov_y = plan.render_fov_y;
				// #139: the scope render keeps the eye's orientation (the gun's turn does not reach it, measured): for the
				// glass it is widened to reach the sight line plus the field stop around it (lens_math).
				if (&optic == &glass_lens)
				{
					const float* q = rendered_eye.orientation;
					const float conj[4]{ -q[0], -q[1], -q[2], q[3] };
					const float t[3]{ 2 * (conj[1] * direction[2] - conj[2] * direction[1]), 2 * (conj[2] * direction[0] - conj[0] * direction[2]),
						2 * (conj[0] * direction[1] - conj[1] * direction[0]) };
					const float in_view[3]{ direction[0] + conj[3] * t[0] + (conj[1] * t[2] - conj[2] * t[1]),
						direction[1] + conj[3] * t[1] + (conj[2] * t[0] - conj[0] * t[2]), direction[2] + conj[3] * t[2] + (conj[0] * t[1] - conj[1] * t[0]) };
					const float reach = glass_picture_reach(in_view, next.magnification);
					if (reach > 0.0f && reach / optic.crop < 11.0f)
					{
						next.render_fov_y = 2.0f * std::atan(reach / optic.crop);
					}
				}
				next.crop = optic.crop;
				next.quad = plan.quad;
				next.quad_size = plan.quad_size;
			}
		}
		game_fov = game_fov_y;
		request = next;
	}

	float optic_power_now()
	{
		return optic_power.load(std::memory_order_relaxed);
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
		weapon::sight_state sight;
		bool at_hip{};
		int learned_count{};
		{
			const std::lock_guard guard(lock);
			r = request;
			game = game_fov;
			hip = hip_fov;
			sight = last_sight;
			at_hip = last_at_hip;
			learned_count = learned.size();
		}
		char text[640];
		std::snprintf(text, sizeof(text),
			"scope game_fov=%.2f hip_fov=%.2f magnification=%.3f aiming=%d at_hip=%d active=%d eye=%d render_fov=%.2f quad=(%.3f %.3f %.3f) size=%.3f relative=(%.3f %.3f %.3f)"
			"\nsight weapon=%s level selected=%d current=%d type=%d learned=%d\n%s",
			game / degree, hip / degree, r.magnification, input::game_aiming() ? 1 : 0, at_hip ? 1 : 0, r.active ? 1 : 0, r.eye, r.render_fov_y / degree, r.quad.position[0],
			r.quad.position[1], r.quad.position[2], r.quad_size, r.relative.position[0], r.relative.position[1], r.relative.position[2], sight.weapon, sight.selected,
			sight.current, static_cast<int>(sight.selected_type), learned_count, scope_compose_report().c_str());
		return text;
	}
}
