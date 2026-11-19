// Checks the virtual steering wheel (src/input/drive_wheel): two hands turn it by the line between them
// wherever its centre is, one hand by its angle around the centre, it stops at full lock and comes back
// from it at once, springs back when let go, and the triggers become throttle and brake.
#include <cmath>
#include <cstdio>

#include "input/drive_wheel.hpp"

using namespace bfvr::input;

namespace
{
	int failures = 0;
	constexpr float deg = 3.14159265f / 180.0f;
	constexpr float frame = 1.0f / 90.0f;
	constexpr float radius = 0.18f;

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

	// Both hands at 9 and 3 o'clock on a wheel turned by `angle` (anticlockwise), centre shifted by (cx, cy).
	wheel_input both(float angle, float cx = 0.0f, float cy = 0.0f)
	{
		wheel_input in;
		in.seconds = frame;
		in.hands[0] = { true, cx - radius * std::cos(angle), cy - radius * std::sin(angle) };
		in.hands[1] = { true, cx + radius * std::cos(angle), cy + radius * std::sin(angle) };
		return in;
	}

	wheel_output turn_both(drive_wheel& wheel, float to, float cx = 0.0f, float cy = 0.0f)
	{
		wheel_output out{};
		for (int step = 0; step <= 30; ++step)
		{
			out = wheel.update(both(to * step / 30.0f, cx, cy));
		}
		return out;
	}
}

int main()
{
	{
		drive_wheel wheel;
		const wheel_output out = turn_both(wheel, 45 * deg);
		expect(out.hands == 2, "both hands hold the wheel");
		expect(near(out.angle, 45 * deg), "two hands turn the wheel by the line between them");
		expect(near(out.steer, -0.5f), "45 degrees to the left of a 90 degree lock steers half left");
	}
	{
		drive_wheel wheel;
		const wheel_output out = turn_both(wheel, -30 * deg, 0.12f, -0.09f);
		expect(near(out.angle, -30 * deg), "two hands steer true with the centre 15 cm off");
		expect(out.steer > 0.3f, "turned clockwise steers right");
	}
	{
		drive_wheel wheel;
		wheel_output out{};
		for (int step = 0; step <= 40; ++step)
		{
			const float a = 90 * deg + 60 * deg * step / 40.0f; // right hand from 12 o'clock towards 10
			wheel_input in;
			in.seconds = frame;
			in.hands[1] = { true, radius * std::cos(a), radius * std::sin(a) };
			out = wheel.update(in);
		}
		expect(out.hands == 1 && near(out.angle, 60 * deg), "one hand turns the wheel around its centre");
	}
	{
		drive_wheel wheel;
		wheel_input in;
		in.seconds = frame;
		in.hands[1] = { true, 0.01f, 0.01f };
		wheel.update(in);
		in.hands[1] = { true, -0.01f, 0.01f };
		const wheel_output out = wheel.update(in);
		expect(near(out.angle, 0.0f), "a hand at the hub does not spin the wheel");
	}
	{
		drive_wheel wheel;
		turn_both(wheel, 150 * deg);
		expect(near(wheel.angle(), 90 * deg), "the wheel stops at full lock");
		wheel_output out{};
		for (int step = 0; step <= 10; ++step)
		{
			out = wheel.update(both(150 * deg - 20 * deg * step / 10.0f));
		}
		expect(near(out.angle, 70 * deg, 2e-3f), "turning back leaves the lock at once");
	}
	{
		drive_wheel wheel;
		turn_both(wheel, 60 * deg);
		wheel_input free;
		free.seconds = frame;
		wheel_output out{};
		for (int step = 0; step < 90; ++step)
		{
			out = wheel.update(free);
		}
		expect(out.hands == 0 && near(out.angle, 0.0f), "let go, the wheel springs back to straight");
		turn_both(wheel, 10 * deg);
		expect(near(wheel.angle(), 10 * deg), "grabbed again it does not jump");
	}
	{
		drive_wheel wheel;
		turn_both(wheel, 20 * deg);
		wheel_input in = both(20 * deg);
		in.hands[0].held = false; // left lets go, right keeps turning
		wheel.update(in);
		in.hands[1] = { true, radius * std::cos(40 * deg), radius * std::sin(40 * deg) };
		const wheel_output out = wheel.update(in);
		expect(near(out.angle, 40 * deg), "from two hands to one the wheel carries on from where it was");
	}
	{
		drive_wheel wheel;
		wheel_input in;
		in.seconds = frame;
		in.throttle = 1.0f;
		expect(near(wheel.update(in).drive, 1.0f), "right trigger: full throttle");
		in.throttle = 0.0f;
		in.brake = 0.53f;
		expect(near(wheel.update(in).drive, -0.5f, 0.01f), "left trigger: brake and reverse");
		in.brake = 0.03f;
		expect(near(wheel.update(in).drive, 0.0f), "a resting trigger does nothing");
		in.brake = 0.0f;
		in.stick_x = -0.8f;
		in.stick_y = 0.6f;
		const wheel_output out = wheel.update(in);
		expect(near(out.steer, -0.8f) && near(out.drive, 0.6f), "a free wheel and no triggers leave the car to the left stick");
		wheel_input held = both(0.0f);
		held.stick_x = -0.8f;
		expect(near(wheel.update(held).steer, 0.0f), "a held wheel overrules the stick");
	}

	if (failures)
	{
		std::printf("%d failure(s)\n", failures);
		return 1;
	}
	std::printf("drive_wheel: all checks passed\n");
	return 0;
}
