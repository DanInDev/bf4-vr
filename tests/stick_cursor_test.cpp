// Checks the menu stick cursor (src/input/stick_cursor, #34) and input::point_on_quad: a resting or drifting stick
// moves nothing, full tilt crosses the screen in about 1.7 s, a small tilt moves slowly, the cursor stays on the
// screen, up moves up as far as sideways in metres, a stall moves no further than a short frame, and the dot's
// point on the screen is where the laser test reports the same coordinates.
#include <cmath>
#include <cstdio>

#include "input/pointer.hpp"
#include "input/stick_cursor.hpp"

using namespace bfvr::input;

namespace
{
	int failures = 0;

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
}

int main()
{
	const float frame = 1.0f / 60.0f;
	const float square = 1.0f;     // the BF4 image is square (2160x2160)
	const float wide = 16.0f / 9.0f;

	// Rest and drift inside the dead zone: no move, no "moving".
	{
		stick_cursor cursor;
		expect(!cursor.update(0.0f, 0.0f, frame, square), "a resting stick is not moving");
		expect(!cursor.update(0.1f, -0.08f, frame, square), "drift inside the dead zone is not moving");
		expect(near(cursor.u(), 0.5f) && near(cursor.v(), 0.5f), "rest and drift leave the cursor at the centre");
	}

	// Full tilt right: 0.6 screen widths per second, so half the screen in ~0.83 s, then it stops at the edge.
	{
		stick_cursor cursor;
		for (int i = 0; i < 30; ++i)
		{
			expect(cursor.update(1.0f, 0.0f, frame, square), "full tilt is moving");
		}
		expect(near(cursor.u(), 0.8f, 0.002f), "half a second of full tilt moves 0.3 screen widths");
		expect(near(cursor.v(), 0.5f), "sideways tilt does not move up or down");
		for (int i = 0; i < 120; ++i)
		{
			cursor.update(1.0f, 0.0f, frame, square);
		}
		expect(near(cursor.u(), 1.0f), "the cursor stops at the right edge");
	}

	// Up is up (v falls), and on a wide screen it moves as many metres up as sideways: v by aspect times u.
	{
		stick_cursor cursor;
		cursor.update(0.0f, 1.0f, 0.1f, square);
		expect(cursor.v() < 0.5f, "stick up moves the cursor up");
		stick_cursor sideways, upwards;
		sideways.update(1.0f, 0.0f, 0.1f, wide);
		upwards.update(0.0f, 1.0f, 0.1f, wide);
		expect(near((0.5f - upwards.v()) / (sideways.u() - 0.5f), wide), "up moves aspect times further in v");
	}

	// A small tilt moves much slower than full tilt (fine placement).
	{
		stick_cursor slow, fast;
		slow.update(0.4f, 0.0f, 0.1f, square);
		fast.update(1.0f, 0.0f, 0.1f, square);
		expect(slow.u() - 0.5f < 0.25f * (fast.u() - 0.5f), "a 0.4 tilt moves under a quarter of full tilt's speed");
		expect(slow.u() > 0.5f, "a 0.4 tilt still moves");
	}

	// A stall (a 2 s frame) moves no further than the longest step.
	{
		stick_cursor cursor;
		cursor.update(1.0f, 0.0f, 2.0f, square);
		expect(near(cursor.u(), 0.5f + 0.6f * 0.1f), "a 2 s frame moves as far as a 0.1 s one");
	}

	// Bad input moves nothing; reset clamps.
	{
		stick_cursor cursor;
		expect(!cursor.update(NAN, 0.0f, frame, square), "NaN is ignored");
		expect(!cursor.update(1.0f, 0.0f, frame, 0.0f), "a zero aspect is ignored");
		cursor.reset(1.5f, -0.2f);
		expect(near(cursor.u(), 1.0f) && near(cursor.v(), 0.0f), "reset keeps the cursor on the screen");
	}

	// point_on_quad is where point_at reports the same u, v (a turned screen 2 m ahead).
	{
		quad_target screen;
		screen.width = 3.2f;
		screen.height = 1.8f;
		const float yaw = 0.6f;
		screen.pose.position[0] = 0.3f;
		screen.pose.position[1] = 1.6f;
		screen.pose.position[2] = -2.0f;
		screen.pose.orientation[1] = std::sin(0.5f * yaw);
		screen.pose.orientation[3] = std::cos(0.5f * yaw);
		float point[3];
		point_on_quad(screen, 0.25f, 0.7f, point);
		// A ray from in front of the screen through that point.
		const float normal[3] = { std::sin(yaw), 0.0f, std::cos(yaw) };
		bfvr::stereo::pose aim;
		for (int axis = 0; axis < 3; ++axis)
		{
			aim.position[axis] = point[axis] + 1.5f * normal[axis];
		}
		aim.orientation[1] = std::sin(0.5f * yaw);
		aim.orientation[3] = std::cos(0.5f * yaw);
		const pointer_hit hit = point_at(aim, screen);
		expect(hit.hit, "a ray straight at the dot's point hits the screen");
		expect(near(hit.u, 0.25f) && near(hit.v, 0.7f), "the laser reports the dot's coordinates");
		point_on_quad(screen, 0.5f, 0.5f, point);
		expect(near(point[0], 0.3f) && near(point[1], 1.6f) && near(point[2], -2.0f), "the centre is the screen's position");
	}

	if (failures == 0)
	{
		std::printf("stick_cursor: all checks passed\n");
	}
	return failures == 0 ? 0 : 1;
}
