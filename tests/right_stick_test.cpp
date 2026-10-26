// Checks the right-stick state machine (src/input/right_stick): deadzone, snap turn with
// hysteresis and cooldown, smooth turn, stance flicks and the axis lock against diagonals.
#include <cmath>
#include <cstdio>

#include "input/right_stick.hpp"

using namespace bfvr::input;

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

	bool near(float a, float b, float tolerance = 1e-5f)
	{
		return std::fabs(a - b) <= tolerance;
	}

	// Holds the stick for the given time and sums what comes out.
	struct totals
	{
		float turn{};
		int snaps{};
		int up{};
		int down{};
	};

	totals hold(right_stick& stick, float x, float y, float seconds)
	{
		totals result;
		for (float t = 0; t < seconds - 1e-6f; t += frame)
		{
			const right_stick_output out = stick.update(x, y, frame);
			result.turn += out.turn;
			result.snaps += out.turn != 0 ? 1 : 0;
			result.up += out.stance == stance_command::up ? 1 : 0;
			result.down += out.stance == stance_command::down ? 1 : 0;
		}
		return result;
	}

	void test_deadzone()
	{
		right_stick stick;
		const totals drift = hold(stick, 0.15f, 0.1f, 2.0f);
		expect(drift.snaps == 0 && drift.up == 0 && drift.down == 0, "stick drift inside the deadzone does nothing");
	}

	void test_snap()
	{
		right_stick stick;
		const float angle = stick.config().snap_angle;

		totals right = hold(stick, 1.0f, 0.0f, 1.0f);
		expect(right.snaps == 1 && near(right.turn, angle), "holding right snaps exactly once");
		hold(stick, 0, 0, 0.1f);
		totals left = hold(stick, -0.9f, 0.05f, 0.2f);
		expect(left.snaps == 1 && near(left.turn, -angle), "left snaps the other way");

		// Hysteresis: wobbling between press and release thresholds does not re-fire.
		stick.reset();
		hold(stick, 0.8f, 0, frame);
		totals wobble{};
		for (int i = 0; i < 40; ++i)
		{
			const totals step = hold(stick, i % 2 ? 0.75f : 0.5f, 0, frame);
			wobble.snaps += step.snaps;
		}
		expect(wobble.snaps == 0, "wobble above the release threshold does not re-fire");
		stick.reset();

		// Cooldown: a second flick right away fires only when the cooldown ends, and only once.
		hold(stick, 1.0f, 0, frame);
		hold(stick, 0, 0, frame);
		totals quick = hold(stick, 1.0f, 0, 0.1f);
		expect(quick.snaps == 0, "flick inside the cooldown waits");
		quick = hold(stick, 1.0f, 0, 0.3f);
		expect(quick.snaps == 1, "held flick fires once the cooldown ends");

		// A flick released inside the cooldown is dropped, not queued.
		stick.reset();
		hold(stick, 1.0f, 0, frame);
		hold(stick, 0, 0, frame);
		hold(stick, 1.0f, 0, 0.05f);
		const int late = hold(stick, 0, 0, 0.3f).snaps + hold(stick, 0.5f, 0, 0.5f).snaps;
		expect(late == 0, "dropped flick never fires after the cooldown");

		// Re-centring through the release band (not the deadzone) also re-arms.
		stick.reset();
		hold(stick, 1.0f, 0, frame);
		hold(stick, 0.3f, 0, 0.5f);
		expect(hold(stick, 1.0f, 0, frame).snaps == 1, "release threshold re-arms without the deadzone");
	}

	void test_smooth()
	{
		right_stick_config config;
		config.turn = turn_mode::smooth;
		right_stick stick(config);
		const totals full = hold(stick, 1.0f, 0, 1.0f);
		expect(near(full.turn, config.smooth_speed, 0.05f), "full deflection turns smooth_speed per second");
		stick.reset();
		const totals half = hold(stick, -0.6f, 0, 1.0f);
		expect(near(half.turn, -config.smooth_speed * 0.5f, 0.05f), "deflection rescaled from the deadzone edge");
		stick.reset();
		const totals edge = hold(stick, config.deadzone + 0.001f, 0, 1.0f);
		expect(std::fabs(edge.turn) < 0.01f, "no jump at the deadzone edge");
		stick.reset();
		expect(hold(stick, 0, 1.0f, 1.0f).turn == 0, "vertical stick does not smooth-turn");
	}

	void test_off()
	{
		right_stick_config config;
		config.turn = turn_mode::off;
		right_stick stick(config);
		expect(hold(stick, 1.0f, 0, 1.0f).turn == 0, "turn off ignores the stick");
	}

	void test_stance()
	{
		right_stick stick;
		totals down = hold(stick, 0, -1.0f, 1.0f);
		expect(down.down == 1 && down.up == 0 && down.turn == 0, "holding down sends one down command");
		hold(stick, 0, 0, 0.1f);
		down = hold(stick, 0.1f, -0.9f, 0.2f);
		expect(down.down == 1, "second flick down sends another");
		hold(stick, 0, 0, 0.1f);
		totals up = hold(stick, 0, 1.0f, 0.2f);
		expect(up.up == 1 && up.down == 0, "flick up sends up");

		// Hysteresis on the vertical axis.
		stick.reset();
		hold(stick, 0, 0.9f, frame);
		int extra = 0;
		for (int i = 0; i < 40; ++i)
		{
			extra += hold(stick, 0, i % 2 ? 0.8f : 0.45f, frame).up;
		}
		expect(extra == 0, "vertical wobble above release does not repeat");

		right_stick_config config;
		config.stance_flicks = false;
		right_stick quiet(config);
		expect(hold(quiet, 0, -1.0f, 1.0f).down == 0, "stance flicks can be switched off");
	}

	void test_axis_lock()
	{
		right_stick stick;
		// A diagonal at 45 degrees claims neither axis.
		totals diagonal = hold(stick, 0.7f, 0.7f, 1.0f);
		expect(diagonal.snaps == 0 && diagonal.up == 0, "pure diagonal does nothing");
		hold(stick, 0, 0, 0.1f);

		// Turn first, then sweep the stick round to the top without centring: no stance change.
		totals sweep{};
		for (int i = 0; i <= 90; ++i)
		{
			const float a = static_cast<float>(i) * 3.14159265f / 180.0f;
			const totals step = hold(stick, std::cos(a), std::sin(a), frame);
			sweep.snaps += step.snaps;
			sweep.up += step.up;
		}
		expect(sweep.snaps == 1 && sweep.up == 0, "rim sweep from right to up keeps the turn gesture");
		hold(stick, 0, 0, 0.5f);

		// Stance first, then sweep to the side: no turn.
		totals other{};
		for (int i = 90; i >= 0; --i)
		{
			const float a = static_cast<float>(i) * 3.14159265f / 180.0f;
			const totals step = hold(stick, std::cos(a), std::sin(a), frame);
			other.snaps += step.snaps;
			other.up += step.up;
		}
		expect(other.up == 1 && other.snaps == 0, "rim sweep from up to right keeps the stance gesture");
	}

	void test_robustness()
	{
		right_stick stick;
		const right_stick_output out = stick.update(5.0f, 0, -1.0f);
		expect(near(out.turn, stick.config().snap_angle), "out-of-range input clamped, negative time ignored");
	}

	void test_next_stance()
	{
		expect(next_stance(stance::stand, stance_command::down) == stance::crouch, "stand down -> crouch");
		expect(next_stance(stance::crouch, stance_command::down) == stance::prone, "crouch down -> prone");
		expect(next_stance(stance::prone, stance_command::down) == stance::prone, "prone down stays");
		expect(next_stance(stance::prone, stance_command::up) == stance::crouch, "prone up -> crouch");
		expect(next_stance(stance::crouch, stance_command::up) == stance::stand, "crouch up -> stand");
		expect(next_stance(stance::stand, stance_command::up) == stance::stand, "stand up stays");
		expect(next_stance(stance::crouch, stance_command::none) == stance::crouch, "none keeps");
	}
}

int main()
{
	test_deadzone();
	test_snap();
	test_smooth();
	test_off();
	test_stance();
	test_axis_lock();
	test_robustness();
	test_next_stance();
	if (failures)
	{
		std::printf("%d failure(s)\n", failures);
		return 1;
	}
	std::printf("right_stick: all checks passed\n");
	return 0;
}
