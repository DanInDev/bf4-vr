// Checks the body calibration (src/body/body_calibration): the T-pose is recognised only with both
// triggers, the hands out at shoulder height and level, held still for a second; the measurement from
// the floor or from the span; and the body shape anchored to the reference (1.78 m: nothing changes).
#include <cmath>
#include <cstdio>

#include "body/body_calibration.hpp"
#include "body/body_size.hpp"

using namespace bfvr::body;
using namespace bfvr::body::calibration;

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

	// A person with their eyes at eye_height above a floor at -1.6 (LOCAL), arms out: grips span apart at
	// shoulder height (25 cm below the eyes), straight out to the sides.
	sample pose(float span, bool triggers = true, float eye_height = 1.67f, bool floor = true)
	{
		sample s;
		s.seconds = frame;
		s.head_valid = s.hands_valid = true;
		const float floor_y = -1.6f;
		s.head = { 0.0f, floor_y + eye_height, 0.0f };
		s.left = { -0.5f * span, s.head.y - 0.25f, 0.02f };
		s.right = { 0.5f * span, s.head.y - 0.25f, 0.02f };
		s.left_trigger = s.right_trigger = triggers;
		s.floor_valid = floor;
		s.floor_y = floor_y;
		return s;
	}

	t_pose::state hold(t_pose& detector, const sample& s, float seconds)
	{
		t_pose::state state = t_pose::state::waiting;
		for (float t = 0.0f; t < seconds; t += frame)
		{
			state = detector.update(s);
		}
		return state;
	}

	void test_pose_rules()
	{
		t_pose detector;
		expect(detector.in_pose(pose(1.6f)), "arms out, both triggers: a T-pose");
		expect(!detector.in_pose(pose(1.6f, false)), "no triggers: not calibrating (gameplay)");
		sample one = pose(1.6f);
		one.left_trigger = false;
		expect(!detector.in_pose(one), "one trigger: not calibrating");
		expect(!detector.in_pose(pose(0.6f)), "hands together (aiming): not a T-pose");
		sample low = pose(1.6f);
		low.left.y -= 0.5f;
		low.right.y -= 0.5f;
		expect(!detector.in_pose(low), "hands at the hips: not a T-pose");
		sample tilted = pose(1.6f);
		tilted.left.y += 0.2f;
		expect(!detector.in_pose(tilted), "one hand much higher: not level");
		sample ahead = pose(1.6f);
		ahead.left.z -= 0.6f;
		ahead.right.z -= 0.6f;
		expect(!detector.in_pose(ahead), "both hands out ahead: not to the sides");
		sample untracked = pose(1.6f);
		untracked.hands_valid = false;
		expect(!detector.in_pose(untracked), "hands not tracked");
	}

	void test_hold()
	{
		t_pose detector;
		expect(hold(detector, pose(1.6f), 1.0f) == t_pose::state::holding && detector.progress() > 0.4f && detector.progress() < 0.6f, "a second: holding, half way");
		expect(detector.update(pose(1.6f, false)) == t_pose::state::waiting && detector.progress() == 0.0f, "letting go starts over");
		expect(hold(detector, pose(1.6f), 1.5f) == t_pose::state::holding, "#50: one and a half seconds is not enough");
		expect(hold(detector, pose(1.6f), 0.6f) == t_pose::state::done, "two seconds held: done");
		expect(detector.update(pose(0.5f, false)) == t_pose::state::done, "done stays done");

		// Moving into the pose: the hold starts again where the hands came to rest.
		t_pose moving;
		hold(moving, pose(1.4f), 0.6f);
		const t_pose::state after_move = moving.update(pose(1.6f));
		expect(after_move == t_pose::state::holding && moving.progress() < 0.05f, "a hand moving 10 cm restarts the hold");
		expect(hold(moving, pose(1.6f), 2.1f) == t_pose::state::done && near(moving.result().span, 1.6f), "measured where it stopped");
	}

	void test_measurement()
	{
		t_pose with_floor;
		hold(with_floor, pose(1.6f, true, 1.67f), 2.1f);
		const measurement& m = with_floor.result();
		expect(m.valid && m.from_floor && near(m.eye_height, 1.67f) && near(m.stature, 1.78f), "from the floor: eyes 1.67 m, stature 1.78 m");
		expect(near(m.standing_head_y, -1.6f + 1.67f), "the standing head height in LOCAL (the crouch reference)");

		t_pose no_floor;
		hold(no_floor, pose(1.6f, true, 1.67f, false), 2.1f);
		expect(no_floor.result().valid && !no_floor.result().from_floor && near(no_floor.result().stature, 1.78f), "no floor: stature from the span (fingertips 9 cm past the grips)");

		// #50: the owner's two runs, 2026-11-12: the floor read 1.86 m, then 1.97 m; the span (1.65 m between
		// the grips, 1.83 m from the span) stayed. Within 10 cm the floor is trusted; past it the span wins.
		t_pose good_floor;
		hold(good_floor, pose(1.65f, true, 1.75f), 2.1f);
		expect(good_floor.result().from_floor && near(good_floor.result().stature, 1.86f), "floor 1.86 m, span 1.83 m: the floor");
		t_pose bad_floor;
		hold(bad_floor, pose(1.65f, true, 1.86f), 2.1f);
		expect(!bad_floor.result().from_floor && bad_floor.result().floor_doubted && near(bad_floor.result().stature, 1.83f), "floor 1.97 m, span 1.83 m: the span, floor doubted");

		t_pose odd;
		hold(odd, pose(1.6f, true, 3.5f), 2.1f);
		expect(odd.result().valid && odd.result().floor_doubted && near(odd.result().stature, 1.78f), "a floor far off (a 3.6 m person): the span's 1.78 m instead");
		t_pose tiny;
		expect(hold(tiny, pose(0.9f, true, 1.67f, false), 2.1f) != t_pose::state::done, "grips 0.9 m apart: not a T-pose, nothing measured");
	}

	void test_confirmation()
	{
		expect(!needs_confirmation(0.0f, 1.97f), "nothing stored yet: taken");
		expect(!needs_confirmation(1.86f, 1.90f), "4 cm off: taken");
		expect(needs_confirmation(1.86f, 1.97f), "11 cm off: needs a second T-pose");
		expect(needs_confirmation(1.86f, 1.80f), "6 cm shorter: needs a second T-pose");
	}

	void test_size()
	{
		const body_size standard = size_for({});
		expect(near(standard.scale, 1.0f) && near(standard.shoulder_drop, 0.22f) && near(standard.chest_depth, 0.10f) && near(standard.hip_drop, 0.75f),
			"not calibrated: the reference layout");
		const body_size tall = size_for({ 1.958f });
		expect(near(tall.scale, 1.1f) && near(tall.shoulder_drop, 0.242f) && near(tall.hip_drop, 0.825f), "1.958 m: every measure 10 % larger");
		const body_size manual = size_for({ 1.958f, 0.20f, 0.14f, 0.70f });
		expect(near(manual.scale, 1.1f) && near(manual.shoulder_drop, 0.20f) && near(manual.chest_depth, 0.14f) && near(manual.hip_drop, 0.70f),
			"measures set by hand win over the height");
		const body_size odd = size_for({ 1.958f, 0.9f, 0.01f, 2.0f });
		expect(near(odd.shoulder_drop, 0.242f) && near(odd.chest_depth, 0.11f) && near(odd.hip_drop, 0.825f), "values outside a body's range: from the height");
	}

	void test_shape()
	{
		measurement reference_body;
		reference_body.valid = true;
		reference_body.stature = 1.78f;
		reference_body.span = 1.60f;
		const body_shape same = shape_for(reference_body);
		expect(near(same.scale, 1.0f) && near(same.arm_length, 1.15f) && near(same.shoulder_width, 0.0f), "the reference body: today's tuning unchanged");

		measurement small;
		small.valid = true;
		small.stature = 1.60f;
		small.span = 1.42f;
		const body_shape s = shape_for(small);
		expect(s.scale < 0.91f && s.scale > 0.89f, "1.60 m: zones 10 % closer");
		expect(s.arm_length < 1.15f && s.arm_length > 0.95f, "shorter arms");
		expect(s.shoulder_width < 0.0f && s.shoulder_width > -0.03f, "narrower shoulders");

		measurement long_arms = reference_body;
		long_arms.span = 1.75f;
		expect(shape_for(long_arms).arm_length > 1.25f && near(shape_for(long_arms).scale, 1.0f), "same height, longer span: longer arms only");

		expect(near(shape_for(measurement{}).arm_length, 1.15f) && near(shape_for(measurement{}).scale, 1.0f), "no measurement: the defaults");
		measurement huge = reference_body;
		huge.span = 3.0f;
		expect(shape_for(huge).arm_length <= 1.5f, "clamped to the setting's range");
	}
}

int main()
{
	test_pose_rules();
	test_hold();
	test_measurement();
	test_confirmation();
	test_size();
	test_shape();
	if (failures)
	{
		std::printf("%d failure(s)\n", failures);
		return 1;
	}
	std::printf("body_calibration: all checks passed\n");
	return 0;
}
