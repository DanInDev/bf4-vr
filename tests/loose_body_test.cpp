// Checks things let go of (src/physics/loose_body, #47, #61): a magazine-sized box falls, bounces, tips onto
// a flat side and rests on the ground under its corners (flat, a step, a slope), never sinks into it, lies its
// time and goes only unseen; with nothing below it falls and goes.
#include <cmath>
#include <cstdio>

#include "physics/loose_body.hpp"

using namespace bfvr::physics;

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

	bool near(float a, float b, float tolerance)
	{
		return std::fabs(a - b) <= tolerance;
	}

	// Ground at y = height, plus a raised block (x > step_x) at step_height, or a slope about z.
	class test_ground : public ground_source
	{
	public:
		ground_plane::kind what{ ground_plane::kind::plane };
		float height{ 0.0f };
		float step_x{ 1e9f };
		float step_height{ 0.0f };
		float slope_degrees{ 0.0f };
		int asked{};

		ground_plane below(const v3& p) override
		{
			++asked;
			if (what != ground_plane::kind::plane)
			{
				return { what };
			}
			if (slope_degrees != 0.0f)
			{
				const float a = slope_degrees * 0.017453293f;
				return { ground_plane::kind::plane, { 0.0f, height, 0.0f }, { -std::sin(a), std::cos(a), 0.0f } };
			}
			const float y = p.x > step_x ? step_height : height;
			return { ground_plane::kind::plane, { p.x, y, p.z }, { 0.0f, 1.0f, 0.0f } };
		}
	};

	q4 about_z(float degrees)
	{
		const float half = 0.5f * degrees * 0.017453293f;
		return { 0.0f, 0.0f, std::sin(half), std::cos(half) };
	}

	struct run
	{
		float lowest_clearance{ 1e9f };
		float seconds{};
	};

	// Runs until at rest (or the time is up), tracking the lowest corner.
	run settle(loose_body& body, ground_source& ground, float most = 6.0f, float fallback = NAN)
	{
		run r;
		while (body.active() && !body.resting() && r.seconds < most)
		{
			body.update(frame, ground, fallback, true);
			r.seconds += frame;
			if (body.touching())
			{
				r.lowest_clearance = std::fmin(r.lowest_clearance, body.clearance());
			}
		}
		return r;
	}

	// A magazine: thin across (x), long down its body (y), the pose point at its top.
	loose_shape magazine()
	{
		loose_shape shape;
		shape.centre = { 0.0f, -0.095f, 0.0f };
		shape.half = { 0.015f, 0.095f, 0.04f };
		return shape;
	}

	// How the thin axis (x) of the box lies: 1 = straight up or down (lying on a flat side).
	float flat_side_up(const loose_body& body)
	{
		const q4 q = body.pose_rotation();
		// The box x axis in the world, its y part.
		const float y = 2.0f * (q.x * q.y + q.w * q.z);
		return std::fabs(y);
	}

	void test_drop_tipped()
	{
		test_ground ground;
		loose_body body;
		body.start({ 0.0f, 1.0f, 0.0f }, about_z(30.0f), {}, {}, magazine());
		expect(near(body.pose_position().y, 1.0f, 1e-5f), "start: the pose is where it was let go");
		const run r = settle(body, ground);
		expect(body.resting(), "dropped from 1 m: comes to rest");
		expect(r.seconds < 4.0f, "dropped from 1 m: at rest within 4 s");
		expect(r.lowest_clearance > -0.02f, "never more than 2 cm into the floor");
		expect(flat_side_up(body) > 0.9f, "tips onto a flat side");
		expect(near(body.centre().y, 0.015f, 0.012f), "lies on its thin side: centre a half thickness up");
	}

	void test_throw()
	{
		test_ground ground;
		loose_body body;
		body.start({ 0.0f, 1.2f, 0.0f }, q4{}, { 3.0f, 1.0f, 0.0f }, { 0.0f, 0.0f, -8.0f }, magazine());
		const run r = settle(body, ground);
		expect(body.resting() && body.centre().x > 1.0f, "thrown sideways at 3 m/s: lands over a metre away and rests");
		expect(body.bounces() >= 1, "thrown: bounces at least once");
		expect(r.lowest_clearance > -0.03f, "thrown: never deep in the floor");
		expect(body.centre().y < 0.1f, "thrown: lies on the floor");
	}

	void test_step()
	{
		test_ground ground;
		ground.step_x = 0.2f;
		ground.step_height = 0.3f;
		loose_body body;
		body.start({ 0.45f, 1.0f, 0.0f }, about_z(80.0f), {}, {}, magazine());
		settle(body, ground);
		expect(body.resting() && body.centre().y > 0.29f && body.centre().y < 0.4f, "over a raised step: lands on the step, not the floor");
	}

	void test_slope()
	{
		test_ground ground;
		ground.slope_degrees = 15.0f;
		loose_body body;
		body.start({ 0.0f, 0.8f, 0.0f }, about_z(90.0f), {}, {}, magazine());
		const run r = settle(body, ground, 8.0f);
		expect(body.resting(), "on a 15 degree slope: comes to rest (it grips)");
		expect(r.lowest_clearance > -0.02f, "on the slope: not into it");
	}

	void test_nothing_below()
	{
		test_ground ground;
		ground.what = ground_plane::kind::none;
		loose_body body;
		body.start({ 0.0f, 1.0f, 0.0f }, q4{}, {}, {}, magazine());
		float t = 0.0f;
		while (body.update(frame, ground, NAN, false) && t < 10.0f)
		{
			t += frame;
		}
		expect(!body.active() && t > 3.5f && t < 4.5f, "nothing below: falls, gone after 4 s");
	}

	void test_unknown_uses_fallback()
	{
		test_ground ground;
		ground.what = ground_plane::kind::unknown;
		loose_body body;
		body.start({ 0.0f, 1.0f, 0.0f }, about_z(90.0f), {}, {}, magazine());
		settle(body, ground, 6.0f, -0.5f);
		expect(body.resting() && near(body.centre().y, -0.5f + 0.015f, 0.015f), "ground not known yet: lands on the fallback floor");
	}

	void test_lie_then_go_unseen()
	{
		test_ground ground;
		loose_body body;
		body.start({ 0.0f, 0.3f, 0.0f }, about_z(90.0f), {}, {}, magazine());
		settle(body, ground);
		expect(body.resting(), "lie: at rest first");
		for (int i = 0; i < static_cast<int>(40.0f / frame); ++i)
		{
			body.update(frame, ground, NAN, true);
		}
		expect(body.active(), "lie: still there after 40 s while seen");
		body.update(frame, ground, NAN, false);
		expect(!body.active(), "lie: goes once not seen");
	}

	void test_rests_quickly_from_low()
	{
		test_ground ground;
		loose_body body;
		// Nearly upright, tilted past its tipping angle (about 9 degrees across the thin side); let go straight and
		// still (or tilted less) it may stay standing, as a real one can.
		body.start({ 0.0f, 0.25f, 0.0f }, about_z(15.0f), { 0.1f, 0.0f, 0.0f }, { 0.0f, 0.0f, 1.5f }, magazine());
		const run r = settle(body, ground);
		expect(body.resting() && r.seconds < 3.0f, "tilted 15 degrees, let go low: rests within 3 s");
		expect(flat_side_up(body) > 0.9f || std::fabs(body.centre().y - 0.04f) < 0.01f, "tilted past tipping: topples onto a side");
	}
}

int main()
{
	test_drop_tipped();
	test_throw();
	test_step();
	test_slope();
	test_nothing_below();
	test_unknown_uses_fallback();
	test_lie_then_go_unseen();
	test_rests_quickly_from_low();
	if (failures)
	{
		std::printf("loose_body: %d failed\n", failures);
		return 1;
	}
	std::printf("loose_body: all checks passed\n");
	return 0;
}
