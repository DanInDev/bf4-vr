// Checks the two-handed grip (src/weapon/two_hand): grabbing the handguard, no jump at the grab,
// steering the barrel with the hand line, blending, release rules, and the converged shot direction.
#include <cmath>
#include <cstdio>
#include <initializer_list>

#include "body/arm_ik.hpp"
#include "weapon/two_hand.hpp"

using namespace bfvr;
using namespace bfvr::weapon;
using body::ik::quat;
using body::ik::vec3;

namespace
{
	int failures = 0;
	constexpr float frame = 1.0f / 90.0f;

	void expect(bool condition, const char* what)
	{
		if (!condition)
		{
			std::printf("FAIL: %s\n", what);
			++failures;
		}
	}

	bool near(float a, float b, float tolerance = 1e-3f)
	{
		return std::fabs(a - b) <= tolerance;
	}

	stereo::pose gun_at(vec3 position, quat rotation = {})
	{
		stereo::pose p;
		p.position[0] = position.x;
		p.position[1] = position.y;
		p.position[2] = position.z;
		p.orientation[0] = rotation.x;
		p.orientation[1] = rotation.y;
		p.orientation[2] = rotation.z;
		p.orientation[3] = rotation.w;
		return p;
	}

	two_hand_input frame_with(const stereo::pose& gun, vec3 off, bool grip, bool busy = false, bool tracked = true)
	{
		two_hand_input input;
		input.tracked = tracked;
		input.gun = gun;
		input.off_hand[0] = off.x;
		input.off_hand[1] = off.y;
		input.off_hand[2] = off.z;
		input.grip = grip;
		input.busy = busy;
		input.seconds = frame;
		return input;
	}

	vec3 barrel(const two_hand_output& out)
	{
		return body::ik::rotate(body::ik::normalize(quat{ out.rotation[0], out.rotation[1], out.rotation[2], out.rotation[3] }), { 0, 0, -1 });
	}

	two_hand_output run(two_hand_grip& grip, const two_hand_input& input, float seconds)
	{
		two_hand_output out;
		for (float t = 0; t < seconds - 1e-6f; t += frame)
		{
			out = grip.update(input);
		}
		return out;
	}

	const stereo::pose level_gun = gun_at({ 0.2f, -0.3f, -0.3f });
	const vec3 handguard{ 0.2f, -0.34f, -0.62f }; // 32 cm ahead of the grip, 4 cm below the barrel line

	void test_handguard_zone()
	{
		const two_hand_config config;
		const float on[3]{ handguard.x, handguard.y, handguard.z };
		const float too_near[3]{ 0.2f, -0.3f, -0.33f };
		const float too_far[3]{ 0.2f, -0.3f, -1.1f };
		const float beside[3]{ 0.4f, -0.3f, -0.6f };
		const float behind[3]{ 0.2f, -0.3f, 0.0f };
		expect(on_handguard(level_gun, on, config), "hand on the handguard");
		expect(!on_handguard(level_gun, too_near, config), "hand at the grip is not the handguard");
		expect(!on_handguard(level_gun, too_far, config), "beyond the muzzle");
		expect(!on_handguard(level_gun, beside, config), "20 cm beside the barrel");
		expect(!on_handguard(level_gun, behind, config), "behind the gun hand");
	}

	void test_grab_and_steer()
	{
		two_hand_grip grip;
		two_hand_output out = grip.update(frame_with(level_gun, handguard, true));
		expect(out.held && out.claim_grip, "grip press on the handguard grabs");
		expect(near(barrel(out).z, -1.0f, 1e-4f), "no jump at the grab");
		out = run(grip, frame_with(level_gun, handguard, true), 0.2f);
		expect(near(out.weight, 1.0f) && near(barrel(out).z, -1.0f, 1e-4f), "holding still keeps the barrel");

		// Off hand moves 20 cm to the right: the barrel swings right, the gun hand did not turn.
		const vec3 moved = handguard + vec3{ 0.2f, 0, 0 };
		out = run(grip, frame_with(level_gun, moved, true), 0.05f);
		const vec3 line_before = body::ik::normalize(handguard - vec3{ 0.2f, -0.3f, -0.3f });
		const vec3 line_after = body::ik::normalize(moved - vec3{ 0.2f, -0.3f, -0.3f });
		const quat q = body::ik::normalize(quat{ out.rotation[0], out.rotation[1], out.rotation[2], out.rotation[3] });
		expect(body::ik::length(body::ik::rotate(q, line_before) - line_after) < 1e-3f, "the remembered hand line follows the hands");
		expect(barrel(out).x > 0.4f, "barrel swings towards the off hand");

		// Hands crossing (closer than min_separation): no NaN, the last two-handed rotation stays.
		out = grip.update(frame_with(level_gun, vec3{ 0.2f, -0.3f, -0.32f }, true));
		expect(std::isfinite(out.rotation[0]) && barrel(out).x > 0.4f, "hands together keep the last rotation");
	}

	void test_blend_and_release()
	{
		two_hand_grip grip;
		two_hand_output out = grip.update(frame_with(level_gun, handguard, true));
		expect(near(out.weight, frame / grip.config().blend_time), "blends in over blend_time");
		out = run(grip, frame_with(level_gun, handguard + vec3{ 0.2f, 0, 0 }, true), 0.2f);
		expect(near(out.weight, 1.0f), "fully two-handed after the blend");

		out = grip.update(frame_with(level_gun, handguard + vec3{ 0.2f, 0, 0 }, false));
		expect(!out.held && !out.claim_grip && out.weight < 1.0f && out.weight > 0.0f, "release starts blending out");
		out = run(grip, frame_with(level_gun, handguard + vec3{ 0.2f, 0, 0 }, false), 0.2f);
		expect(near(out.weight, 0) && near(barrel(out).z, -1.0f, 1e-4f), "one-handed again after the blend");
	}

	void test_grip_claimed_before_the_grab()
	{
		two_hand_grip grip;
		expect(grip.update(frame_with(level_gun, handguard, false)).claim_grip, "hand on the handguard: grip held back before it closes");
		expect(!grip.update(frame_with(level_gun, vec3{ -0.3f, -0.3f, -0.2f }, false)).claim_grip, "hand elsewhere: grip is the game's");
		expect(!grip.update(frame_with(level_gun, handguard, false, true)).claim_grip, "busy hand on the handguard: the reload owns it");
	}

	void test_refusals()
	{
		two_hand_grip grip;
		expect(!grip.update(frame_with(level_gun, handguard, true, true)).held, "busy off hand does not grab");
		grip.reset();

		// Grip already held when the hand arrives: no grab until it is pressed again.
		grip.update(frame_with(level_gun, vec3{ -0.3f, -0.3f, -0.2f }, true));
		expect(!grip.update(frame_with(level_gun, handguard, true)).held, "held grip moved onto the gun does not grab");
		grip.update(frame_with(level_gun, handguard, false));
		expect(grip.update(frame_with(level_gun, handguard, true)).held, "a new press grabs");

		expect(!grip.update(frame_with(level_gun, handguard, true, true)).held, "becoming busy releases");
		grip.reset();
		grip.update(frame_with(level_gun, handguard, true));
		expect(!grip.update(frame_with(level_gun, handguard, true, false, false)).held, "tracking loss releases");
		grip.reset();
		expect(!grip.update(frame_with(level_gun, vec3{ 0.2f, -0.3f, -0.6f } + vec3{ 0.3f, 0, 0 }, true)).held, "press away from the gun does not grab");
	}

	two_hand_input with_trigger(two_hand_input input, float trigger)
	{
		input.trigger = trigger;
		return input;
	}

	void test_accessory_trigger()
	{
		two_hand_grip grip;
		two_hand_output out = grip.update(with_trigger(frame_with(level_gun, handguard, false), 1.0f));
		expect(!out.toggle_accessory && !out.claim_trigger, "trigger without holding the gun is the game's");
		grip.update(frame_with(level_gun, handguard, false));

		out = grip.update(frame_with(level_gun, handguard, true));
		expect(out.held && out.claim_trigger && !out.toggle_accessory, "holding claims the trigger");
		out = grip.update(with_trigger(frame_with(level_gun, handguard, true), 1.0f));
		expect(out.toggle_accessory, "trigger press while holding toggles");
		int toggles = 0;
		for (const float value : { 1.0f, 0.9f, 0.4f, 0.8f, 1.0f })
		{
			toggles += grip.update(with_trigger(frame_with(level_gun, handguard, true), value)).toggle_accessory ? 1 : 0;
		}
		expect(toggles == 0, "held trigger and wobble above the release do not repeat");
		grip.update(with_trigger(frame_with(level_gun, handguard, true), 0.1f));
		expect(grip.update(with_trigger(frame_with(level_gun, handguard, true), 0.7f)).toggle_accessory, "release and press again toggles again");

		// Trigger already held when the gun is grabbed: no toggle until it is pressed anew.
		two_hand_grip second;
		second.update(with_trigger(frame_with(level_gun, handguard, false), 1.0f));
		out = second.update(with_trigger(frame_with(level_gun, handguard, true), 1.0f));
		expect(out.held && !out.toggle_accessory, "trigger held at the grab does not toggle");
		second.update(with_trigger(frame_with(level_gun, handguard, true), 0.0f));
		expect(second.update(with_trigger(frame_with(level_gun, handguard, true), 1.0f)).toggle_accessory, "then a new press toggles");

		// Letting go of the grip hands the trigger back.
		out = second.update(with_trigger(frame_with(level_gun, handguard, false), 0.0f));
		expect(!out.claim_trigger, "release gives the trigger back");
	}

	void test_converged_aim()
	{
		const float origin[3]{ 0, 0, 0 };
		const float grip[3]{ 0.2f, -0.3f, -0.3f };
		const float level[4]{ 0, 0, 0, 1 };
		float direction[3];
		converged_aim(origin, grip, level, 25.0f, direction);
		expect(near(direction[0] * direction[0] + direction[1] * direction[1] + direction[2] * direction[2], 1.0f), "unit direction");
		// The shot ray from the origin meets the barrel line 25 m down the barrel.
		const float t = 25.3f / -direction[2];
		expect(near(direction[0] * t, 0.2f, 1e-3f) && near(direction[1] * t, -0.3f, 1e-3f), "rays meet at the convergence distance");

		// A barrel turned 90 degrees left aims left.
		const quat left{ 0, std::sin(0.785398f), 0, std::cos(0.785398f) };
		const float turned[4]{ left.x, left.y, left.z, left.w };
		converged_aim(origin, grip, turned, 25.0f, direction);
		expect(direction[0] < -0.95f, "turned barrel aims left");
	}
}

int main()
{
	test_handguard_zone();
	test_grab_and_steer();
	test_blend_and_release();
	test_refusals();
	test_grip_claimed_before_the_grab();
	test_accessory_trigger();
	test_converged_aim();
	if (failures)
	{
		std::printf("%d failure(s)\n", failures);
		return 1;
	}
	std::printf("two_hand: all checks passed\n");
	return 0;
}
