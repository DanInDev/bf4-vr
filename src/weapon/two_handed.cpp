#include "weapon/two_handed.hpp"

#include <windows.h>
#include <xinput.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <iterator>
#include <mutex>

#include "body/arm_pose.hpp"
#include "input/haptics.hpp"
#include "input/vr_controls.hpp"
#include "stereo/tracking.hpp"
#include "weapon/grab_debug.hpp"
#include "weapon/physical_grenade.hpp"
#include "weapon/physical_reload.hpp"
#include "weapon/two_hand.hpp"

namespace bfvr::weapon
{
	namespace
	{
		std::atomic<bool> enabled{ true };
		std::atomic<float> convergence{ 25.0f };
		std::atomic<bool> shot_from_gun{ true }; // live setting shot_line
		bool sight_line{};                        // the drawn line is the gun's measured sight, not its barrel
		float sight_degrees[2]{};                 // the sight vs the barrel: up, right (report)
		std::atomic<bool> sight_ads{ true };      // live setting sight_ads
		bool sight_aiming{};
		bool sight_from_ads{};
		float eye_from_optic[3]{}; // report
		unsigned long long sight_changes{};
		ULONGLONG last_trigger_at{};
		constexpr float sight_enter_metres = 0.025f; // eye this close to the sight line: aiming
		constexpr float sight_leave_metres = 0.04f;
		constexpr float sight_eye_relief = 0.30f;     // up to this far behind the game's eye point
		bool drawn_line{};                       // this update aimed along the drawn gun
		std::atomic<bool> claim{};
		std::atomic<bool> trigger_toggles{ true };
		std::atomic<bool> claim_trigger{};
		std::atomic<ULONGLONG> pulse_until{};
		constexpr ULONGLONG pulse_ms = 120;
		std::uint64_t toggles{};
		std::mutex lock_;
		two_hand_grip grip;
		two_hand_input last_input; // for the report: why a grab did or did not happen
		float support_smoothed[2]{};
		bool support_known{};
		two_hand_output last;
		float last_shot[3]{};
		float last_point[3]{};
		bool shot_valid{};
		std::uint64_t grabs{};
		LARGE_INTEGER last_update{};

		void clear()
		{
			grip.reset();
			last = {};
			shot_valid = false;
			claim = false;
			claim_trigger = false;
			const float identity[4]{ 0, 0, 0, 1 };
			const float none[3]{};
			body::set_gun_hand_rotation(false, identity);
			body::set_support_hand(0.0f);
			input::set_aim_direction(false, none);
			sight_aiming = false;
			input::set_sight_aim(false);
		}
	}

	void set_two_handed(bool on)
	{
		enabled = on;
	}

	void set_two_hand_lock(bool lock)
	{
		const std::lock_guard guard(lock_);
		two_hand_config config = grip.config();
		config.lock = lock;
		grip.configure(config);
	}

	void set_aim_convergence(float metres)
	{
		convergence = metres;
	}

	void set_shot_from_gun(bool gun)
	{
		shot_from_gun = gun;
	}

	void set_accessory_trigger(bool on)
	{
		trigger_toggles = on;
	}

	void update_two_handed()
	{
		const std::lock_guard guard(lock_);
		LARGE_INTEGER now{}, frequency{};
		QueryPerformanceCounter(&now);
		QueryPerformanceFrequency(&frequency);
		const float seconds = last_update.QuadPart ? std::fmin(static_cast<float>(now.QuadPart - last_update.QuadPart) / static_cast<float>(frequency.QuadPart), 0.1f) : 0.0f;
		last_update = now;

		// Index 1 is the gun hand in either handedness (input/handedness).
		const stereo::hand_sample hands = stereo::latest_hand_sample();
		if (!hands.valid[1])
		{
			clear();
			return;
		}

		two_hand_output out;
		std::copy(std::begin(hands.aim[1].orientation), std::end(hands.aim[1].orientation), out.rotation);
		if (enabled.load(std::memory_order_relaxed))
		{
			two_hand_input input;
			input.tracked = hands.valid[0];
			std::copy(std::begin(hands.aim[1].orientation), std::end(hands.aim[1].orientation), input.gun.orientation);
			// Wrists, as drawn: the gun's support offset is measured between its hand bones (wrists).
			body::wrist_of(1, hands.grip[1], input.gun.position);
			body::wrist_of(0, hands.grip[0], input.off_hand);
			// The gun's own support point moves with its animations (an empty-gun or reload pose drops
			// it 10 cm): eased over a third of a second so a locked barrel never jumps.
			float support[2]{};
			if (body::support_offset(support))
			{
				const float ease = support_known ? std::fmin(seconds / 0.33f, 1.0f) : 1.0f;
				for (int axis = 0; axis < 2; ++axis)
				{
					support_smoothed[axis] += (support[axis] - support_smoothed[axis]) * ease;
				}
				support_known = true;
			}
			input.support_offset[0] = support_smoothed[0];
			input.support_offset[1] = support_smoothed[1];
			const input::gamepad_state pad = input::latest();
			input.grip = (pad.buttons & XINPUT_GAMEPAD_LEFT_SHOULDER) != 0;
			input.trigger = static_cast<float>(pad.left_trigger) / 255.0f;
			// A new grab waits while the hand is inside a reload zone; a hold already on the handguard only
			// ends when the reload really takes the hand (a magazine, the handle), not when it passes a zone.
			input.busy = grenade_in_hand() || (last.held ? reload_engages_off_hand() : reload_owns_off_hand());
			input.seconds = seconds;
			out = grip.update(input);
			last_input = input;
			if (out.held && !last.held)
			{
				++grabs;
				bfvr::input::haptic(0, bfvr::input::pulses::grab);
				bfvr::input::haptic(1, bfvr::input::pulses::tick);
			}
			else if (!out.held && last.held)
			{
				bfvr::input::haptic(0, bfvr::input::pulses::tick);
			}
		}
		else
		{
			grip.reset();
		}
		last = out;
		bfvr::input::set_two_handed_haptics(out.held);
		claim = out.claim_grip;
		const bool by_trigger = trigger_toggles.load(std::memory_order_relaxed);
		claim_trigger = by_trigger && out.claim_trigger;
		if (by_trigger && out.toggle_accessory)
		{
			bfvr::input::haptic(0, bfvr::input::pulses::click);
			pulse_until = GetTickCount64() + pulse_ms;
			++toggles;
		}
		body::set_gun_hand_rotation(out.weight > 0, out.rotation);
		body::set_support_hand(out.weight);
		if (grab_debug_enabled())
		{
			const body::weapon_points points = body::latest_weapon_points();
			if (points.has_support)
			{
				publish_grab_zone(grab_slot::handguard, points.support, grip.config().grab_reach, out.claim_grip);
			}
		}

		const float origin[3]{}; // the game camera is the OpenXR LOCAL origin (the eye pose composes onto it)
		const float metres = convergence.load(std::memory_order_relaxed);
		// shot_line = gun: along the gun as drawn (the optic, else Wep_Root, along its barrel), so the
		// reticle and the bullets sit on what you see; controller: the controller's aim ray.
		float start[3]{ hands.grip[1].position[0], hands.grip[1].position[1], hands.grip[1].position[2] };
		float line[4]{ out.rotation[0], out.rotation[1], out.rotation[2], out.rotation[3] };
		const body::weapon_points points = body::latest_weapon_points();
		drawn_line = shot_from_gun.load(std::memory_order_relaxed) && points.valid && points.has_root && GetTickCount64() - points.at < 250;
		if (drawn_line)
		{
			// The sight line where the game's animation shows it (the eye point behind the sight, along
			// the sight), else the optic or Wep_Root along the barrel.
			const float* from = points.has_sight ? points.sight_eye : points.has_optic ? points.optic : points.root;
			std::copy(from, from + 3, start);
			drawn_barrel_rotation(points.root_rotation, line, points.has_sight ? points.sight_direction : nullptr);
		}
		sight_line = drawn_line && points.has_sight;
		sight_from_ads = points.sight_from_ads;
		if (points.has_sight && points.has_optic && points.has_root)
		{
			// The sight's eye point relative to Wep_Scope1, in Wep_Root axes (+x left, +y up, +z barrel).
			const float d[3]{ points.sight_eye[0] - points.optic[0], points.sight_eye[1] - points.optic[1], points.sight_eye[2] - points.optic[2] };
			const body::ik::quat root{ points.root_rotation[0], points.root_rotation[1], points.root_rotation[2], points.root_rotation[3] };
			const body::ik::vec3 local = body::ik::rotate(body::ik::conjugate(root), { d[0], d[1], d[2] });
			eye_from_optic[0] = local.x;
			eye_from_optic[1] = local.y;
			eye_from_optic[2] = local.z;
		}
		if (points.has_sight)
		{
			// Wep_Root axes: +z barrel, +y up, +x left.
			sight_degrees[0] = std::atan2(points.sight_direction[1], points.sight_direction[2]) * 57.29578f;
			sight_degrees[1] = std::atan2(-points.sight_direction[0], points.sight_direction[2]) * 57.29578f;
		}
		converged_aim(origin, start, line, metres, last_shot);
		const float* q = line;
		const float barrel[3]{ -2 * (q[0] * q[2] + q[1] * q[3]), -2 * (q[1] * q[2] - q[0] * q[3]), -(1 - 2 * (q[0] * q[0] + q[1] * q[1])) };
		for (int axis = 0; axis < 3; ++axis)
		{
			last_point[axis] = start[axis] + barrel[axis] * std::fmax(metres, 0.5f);
		}
		shot_valid = true;
		input::set_aim_direction(true, last_shot);

		// sight_ads: an eye behind the sight holds the game's aim down sights (its lens clears, ADS
		// accuracy); a wider band to leave than to enter, so a steady aim does not flicker.
		bool aiming = false;
		if (sight_ads.load(std::memory_order_relaxed) && sight_line)
		{
			if (const stereo::head_sample head = stereo::latest_head_sample(); head.valid)
			{
				const float reach = sight_aiming ? sight_leave_metres : sight_enter_metres;
				for (const stereo::pose& eye : head.eyes)
				{
					const float at[3]{ eye.position[0], eye.position[1], eye.position[2] };
					aiming = aiming || eye_behind_sight(at, start, barrel, reach, sight_eye_relief);
				}
			}
		}
		// The drawn recoil kicks the sight by centimetres: firing keeps the aim until 0.4 s after the last shot.
		const ULONGLONG ticks = GetTickCount64();
		if (input::latest().right_trigger > 30)
		{
			last_trigger_at = ticks;
		}
		if (sight_aiming && !aiming && sight_ads.load(std::memory_order_relaxed) && sight_line && ticks - last_trigger_at < 400)
		{
			aiming = true;
		}
		if (aiming != sight_aiming)
		{
			++sight_changes;
		}
		sight_aiming = aiming;
		input::set_sight_aim(aiming);
	}

	void set_sight_ads(bool on)
	{
		sight_ads = on;
	}

	bool aim_point(float (&point)[3])
	{
		const std::lock_guard guard(lock_);
		std::copy(std::begin(last_point), std::end(last_point), point);
		return shot_valid;
	}

	bool off_hand_on_gun()
	{
		return claim.load(std::memory_order_relaxed);
	}

	input::gamepad_state filter_two_hand_pad(const input::gamepad_state& pad)
	{
		input::gamepad_state out = pad;
		if (claim.load(std::memory_order_relaxed))
		{
			out.buttons &= static_cast<std::uint16_t>(~XINPUT_GAMEPAD_LEFT_SHOULDER);
		}
		if (claim_trigger.load(std::memory_order_relaxed))
		{
			out.left_trigger = 0;
		}
		if (GetTickCount64() < pulse_until.load(std::memory_order_relaxed))
		{
			out.buttons |= XINPUT_GAMEPAD_DPAD_UP;
			out.connected = true;
		}
		return out;
	}

	std::string two_hand_report()
	{
		const std::lock_guard guard(lock_);
		const float* q = last.rotation;
		const float barrel[3]{ -2 * (q[0] * q[2] + q[1] * q[3]), -2 * (q[1] * q[2] - q[0] * q[3]), -(1 - 2 * (q[0] * q[0] + q[1] * q[1])) };
		const two_hand_input& in = last_input;
		char text[768];
		const int length = std::snprintf(text, sizeof(text), "two_handed=%d held=%d weight=%.2f grabs=%llu accessory_trigger=%d toggles=%llu barrel=(%.3f %.3f %.3f) shot=%s(%.3f %.3f %.3f) shot_line=%s convergence=%.1f aim_controller=%d",
			enabled.load() ? 1 : 0, last.held ? 1 : 0, last.weight, static_cast<unsigned long long>(grabs), trigger_toggles.load() ? 1 : 0,
			static_cast<unsigned long long>(toggles), barrel[0], barrel[1], barrel[2],
			shot_valid ? "" : "invalid", last_shot[0], last_shot[1], last_shot[2], sight_line ? "sight" : drawn_line ? "gun" : "controller", convergence.load(), input::controller_aim_active() ? 1 : 0);
		if (length > 0 && length < static_cast<int>(sizeof(text)))
		{
			std::snprintf(text + length, sizeof(text) - length, "\nsight up=%.2f right=%.2f deg from_ads=%d sight_ads=%d aiming=%d changes=%llu eye_from_optic=(%.3f %.3f %.3f)\ninput tracked=%d grip=%d busy=%d on_handguard=%d support=(%.3f %.3f) gun_wrist=(%.3f %.3f %.3f) off_wrist=(%.3f %.3f %.3f) mode=%s",
				sight_degrees[0], sight_degrees[1], sight_from_ads ? 1 : 0, sight_ads.load() ? 1 : 0, sight_aiming ? 1 : 0, sight_changes, eye_from_optic[0], eye_from_optic[1], eye_from_optic[2], in.tracked ? 1 : 0, in.grip ? 1 : 0, in.busy ? 1 : 0, on_handguard(in.gun, in.off_hand, grip.config()) ? 1 : 0, in.support_offset[0], in.support_offset[1],
				in.gun.position[0], in.gun.position[1], in.gun.position[2], in.off_hand[0], in.off_hand[1], in.off_hand[2], grip.config().lock ? "lock" : "soft");
		}
		return text;
	}
}
