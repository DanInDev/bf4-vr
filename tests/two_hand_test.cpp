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

	two_hand_config soft()
	{
		two_hand_config config;
		config.lock = false;
		return config;
	}

	void test_grab_and_steer()
	{
		two_hand_grip grip(soft());
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
		expect(grip.update(frame_with(level_gun, handguard, true, false, false)).held, "a tracking blip keeps the hold");
		expect(!run(grip, frame_with(level_gun, handguard, true, false, false), 0.3f).held, "tracking lost longer than the grace releases");
		grip.reset();
		expect(!grip.update(frame_with(level_gun, vec3{ 0.2f, -0.3f, -0.6f } + vec3{ 0.3f, 0, 0 }, true)).held, "press away from the gun does not grab");
	}

	two_hand_input supported(two_hand_input input, float x, float y)
	{
		input.support_offset[0] = x;
		input.support_offset[1] = y;
		return input;
	}

	// Angle in degrees between two directions.
	float degrees_between(vec3 a, vec3 b)
	{
		return std::acos(std::fmax(-1.0f, std::fmin(1.0f, body::ik::dot(body::ik::normalize(a), body::ik::normalize(b))))) * 57.29578f;
	}

	void test_lock()
	{
		const vec3 gun_hand{ 0.2f, -0.3f, -0.3f };
		// The gun holds the off hand 4 cm below its barrel line: a hand there keeps the barrel level.
		two_hand_grip grip;
		two_hand_output out = run(grip, supported(frame_with(level_gun, handguard, true), 0, -0.04f), 0.3f);
		expect(out.held && near(out.weight, 1.0f), "lock: holding");
		expect(degrees_between(barrel(out), { 0, 0, -1 }) < 0.1f, "lock: hand where the gun holds it, barrel level");

		// Without the offset the barrel tilts towards the hand: atan(4/32) = 7.1 degrees down.
		two_hand_grip plain;
		out = run(plain, frame_with(level_gun, handguard, true), 0.3f);
		expect(near(degrees_between(barrel(out), { 0, 0, -1 }), 7.13f, 0.1f) && barrel(out).y < 0, "lock: barrel on the hand line");

		// A gun hand turned 18 degrees off the hand line snaps onto it (no remembered offset).
		const quat off_line{ 0, std::sin(0.157f), 0, std::cos(0.157f) };
		two_hand_grip snapped;
		out = run(snapped, supported(frame_with(gun_at(gun_hand, off_line), handguard, true), 0, -0.04f), 0.3f);
		expect(degrees_between(barrel(out), { 0, 0, -1 }) < 0.5f, "lock: the hand line wins over the gun hand's yaw");

		// The off hand slides along the handguard: the barrel stays put.
		out = run(grip, supported(frame_with(level_gun, handguard + vec3{ 0, 0, -0.12f }, true), 0, -0.04f), 0.1f);
		expect(degrees_between(barrel(out), { 0, 0, -1 }) < 0.1f, "lock: sliding along the handguard keeps the barrel");

		// Off hand 20 cm right and 10 cm up of where it was: the barrel points at it.
		const vec3 moved = handguard + vec3{ 0.2f, 0.1f, 0 };
		out = run(grip, supported(frame_with(level_gun, moved, true), 0, -0.04f), 0.1f);
		const vec3 expected = (moved - vec3{ 0, -0.04f, 0 }) - gun_hand;
		expect(degrees_between(barrel(out), expected) < 1.0f, "lock: barrel follows the off hand");

		// Pulling the off hand back behind the grip while still gripping: the barrel keeps its direction.
		const vec3 before = barrel(out);
		out = run(grip, supported(frame_with(level_gun, gun_hand + vec3{ 0.0f, -0.1f, 0.2f }, true), 0, -0.04f), 0.1f);
		expect(out.held && degrees_between(barrel(out), before) < 0.1f, "lock: an off hand behind the grip never turns the gun round");

		// Rolling the gun hand about the barrel cants the gun but keeps the barrel on the hand line.
		const quat roll{ 0, 0, std::sin(0.35f), std::cos(0.35f) };
		two_hand_grip canted;
		out = run(canted, frame_with(gun_at(gun_hand, roll), handguard + vec3{ 0, 0.04f, 0 }, true), 0.3f);
		const quat q = body::ik::normalize(quat{ out.rotation[0], out.rotation[1], out.rotation[2], out.rotation[3] });
		const vec3 up = body::ik::rotate(q, { 0, 1, 0 });
		expect(degrees_between(barrel(out), { 0, 0, -1 }) < 0.1f && up.x < -0.5f, "lock: the gun hand's roll cants the gun");
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

	void test_drawn_barrel()
	{
		// A drawn gun whose Wep_Root (+z = barrel) points 30 degrees up and to the left of straight ahead.
		const quat yaw{ 0, std::sin(0.5f * 2.6f), 0, std::cos(0.5f * 2.6f) };   // +z turned to face mostly -z, a little left
		const quat pitch{ std::sin(-0.5f * 0.3f), 0, 0, std::cos(-0.5f * 0.3f) };
		const quat root = yaw * pitch;
		const float root_rotation[4]{ root.x, root.y, root.z, root.w };
		float rotation[4];
		drawn_barrel_rotation(root_rotation, rotation);
		const body::ik::vec3 along = body::ik::rotate(root, { 0, 0, 1 });
		const body::ik::vec3 barrel = body::ik::rotate(quat{ rotation[0], rotation[1], rotation[2], rotation[3] }, { 0, 0, -1 });
		expect(near(along.x, barrel.x) && near(along.y, barrel.y) && near(along.z, barrel.z), "the drawn barrel is Wep_Root's +z");
		const float origin[3]{ 0, 0, 0 };
		const float sight[3]{ 0.1f, -0.2f, -0.4f };
		float direction[3];
		converged_aim(origin, sight, rotation, 25.0f, direction);
		const float target[3]{ sight[0] + along.x * 25.0f, sight[1] + along.y * 25.0f, sight[2] + along.z * 25.0f };
		const float length = std::sqrt(target[0] * target[0] + target[1] * target[1] + target[2] * target[2]);
		expect(near(direction[0], target[0] / length, 1e-4f) && near(direction[1], target[1] / length, 1e-4f), "shots meet the drawn sight line 25 m out");

		// A sight line 1.4 degrees right of and 0.7 degrees below the barrel (AK-12, measured): the line
		// follows the sight, turned with the gun.
		const float sight_direction[3]{ -0.0244f, -0.0122f, 0.99963f };
		drawn_barrel_rotation(root_rotation, rotation, sight_direction);
		const body::ik::vec3 sighted = body::ik::rotate(root, body::ik::normalize(body::ik::vec3{ sight_direction[0], sight_direction[1], sight_direction[2] }));
		const body::ik::vec3 line = body::ik::rotate(quat{ rotation[0], rotation[1], rotation[2], rotation[3] }, { 0, 0, -1 });
		expect(near(sighted.x, line.x, 1e-4f) && near(sighted.y, line.y, 1e-4f) && near(sighted.z, line.z, 1e-4f), "the line follows the sight, not the barrel");
		expect(body::ik::dot(line, barrel) < std::cos(0.02f), "the sight line differs from the barrel");

		// Looking through the sight: an eye near the line behind the game's eye point.
		const float eye_point[3]{ 0.03f, -0.01f, -0.20f };
		const float ahead[3]{ 0.0f, 0.0f, -1.0f };
		const float behind[3]{ 0.035f, -0.005f, -0.08f };   // 12 cm of eye relief, 0.7 cm off the line
		const float beside[3]{ 0.065f, -0.01f, -0.10f };    // 3.5 cm to the side
		const float far_back[3]{ 0.03f, -0.01f, 0.20f };    // 40 cm behind
		const float past[3]{ 0.03f, -0.01f, -0.30f };       // in front of the sight
		expect(eye_behind_sight(behind, eye_point, ahead, 0.025f, 0.30f), "an eye behind the sight looks through it");
		expect(!eye_behind_sight(beside, eye_point, ahead, 0.025f, 0.30f), "3.5 cm off the line: not through the sight");
		expect(eye_behind_sight(beside, eye_point, ahead, 0.04f, 0.30f), "within the wider band to leave");
		expect(!eye_behind_sight(far_back, eye_point, ahead, 0.025f, 0.30f), "too far behind the sight");
		expect(!eye_behind_sight(past, eye_point, ahead, 0.025f, 0.30f), "the eye in front of the sight");
	}
}

int main()
{
	test_handguard_zone();
	test_grab_and_steer();
	test_lock();
	test_blend_and_release();
	test_refusals();
	test_grip_claimed_before_the_grab();
	test_accessory_trigger();
	test_converged_aim();
	test_drawn_barrel();
	if (failures)
	{
		std::printf("%d failure(s)\n", failures);
		return 1;
	}
	std::printf("two_hand: all checks passed\n");
	return 0;
}
