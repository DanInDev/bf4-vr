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
#include "input/vr_controls.hpp"
#include "stereo/tracking.hpp"
#include "weapon/physical_grenade.hpp"
#include "weapon/physical_reload.hpp"
#include "weapon/two_hand.hpp"

namespace bfvr::weapon
{
	namespace
	{
		std::atomic<bool> enabled{ true };
		std::atomic<float> convergence{ 25.0f };
		std::atomic<bool> claim{};
		std::mutex lock;
		two_hand_grip grip;
		two_hand_output last;
		float last_shot[3]{};
		bool shot_valid{};
		std::uint64_t grabs{};
		LARGE_INTEGER last_update{};

		void clear()
		{
			grip.reset();
			last = {};
			shot_valid = false;
			claim = false;
			const float identity[4]{ 0, 0, 0, 1 };
			const float none[3]{};
			body::set_gun_hand_rotation(false, identity);
			input::set_aim_direction(false, none);
		}
	}

	void set_two_handed(bool on)
	{
		enabled = on;
	}

	void set_aim_convergence(float metres)
	{
		convergence = metres;
	}

	void update_two_handed()
	{
		const std::lock_guard guard(lock);
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
			std::copy(std::begin(hands.grip[1].position), std::end(hands.grip[1].position), input.gun.position);
			std::copy(std::begin(hands.grip[0].position), std::end(hands.grip[0].position), input.off_hand);
			input.grip = (input::latest().buttons & XINPUT_GAMEPAD_LEFT_SHOULDER) != 0;
			input.busy = grenade_in_hand() || reload_owns_off_hand();
			input.seconds = seconds;
			out = grip.update(input);
			if (out.held && !last.held)
			{
				++grabs;
			}
		}
		else
		{
			grip.reset();
		}
		last = out;
		claim = out.claim_grip;
		body::set_gun_hand_rotation(out.weight > 0, out.rotation);

		const float origin[3]{}; // the game camera is the OpenXR LOCAL origin (the eye pose composes onto it)
		converged_aim(origin, hands.grip[1].position, out.rotation, convergence.load(std::memory_order_relaxed), last_shot);
		shot_valid = true;
		input::set_aim_direction(true, last_shot);
	}

	input::gamepad_state filter_two_hand_pad(const input::gamepad_state& pad)
	{
		input::gamepad_state out = pad;
		if (claim.load(std::memory_order_relaxed))
		{
			out.buttons &= static_cast<std::uint16_t>(~XINPUT_GAMEPAD_LEFT_SHOULDER);
		}
		return out;
	}

	std::string two_hand_report()
	{
		const std::lock_guard guard(lock);
		const float* q = last.rotation;
		const float barrel[3]{ -2 * (q[0] * q[2] + q[1] * q[3]), -2 * (q[1] * q[2] - q[0] * q[3]), -(1 - 2 * (q[0] * q[0] + q[1] * q[1])) };
		char text[320];
		std::snprintf(text, sizeof(text), "two_handed=%d held=%d weight=%.2f grabs=%llu barrel=(%.3f %.3f %.3f) shot=%s(%.3f %.3f %.3f) convergence=%.1f aim_controller=%d",
			enabled.load() ? 1 : 0, last.held ? 1 : 0, last.weight, static_cast<unsigned long long>(grabs), barrel[0], barrel[1], barrel[2],
			shot_valid ? "" : "invalid", last_shot[0], last_shot[1], last_shot[2], convergence.load(), input::controller_aim_active() ? 1 : 0);
		return text;
	}
}
