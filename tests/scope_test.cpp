// Checks the magnified-sight maths (src/stereo/scope_math): the game's magnification, the scope camera
// along the sight line, the lens FOV from its size and distance, and moving a game camera by a pose in
// its own frame.
#include <cmath>
#include <cstdio>

#include "stereo/scope_math.hpp"

using namespace bfvr::stereo;
using namespace bfvr::stereo::scope_math;

namespace
{
	int failures = 0;
	constexpr float degree = 3.14159265f / 180.0f;

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

	// Rotates v by the pose's orientation.
	void rotate(const pose& p, const float (&v)[3], float (&out)[3])
	{
		const float x = p.orientation[0], y = p.orientation[1], z = p.orientation[2], w = p.orientation[3];
		const float t[3] = { 2 * (y * v[2] - z * v[1]), 2 * (z * v[0] - x * v[2]), 2 * (x * v[1] - y * v[0]) };
		out[0] = v[0] + w * t[0] + (y * t[2] - z * t[1]);
		out[1] = v[1] + w * t[1] + (z * t[0] - x * t[2]);
		out[2] = v[2] + w * t[2] + (x * t[1] - y * t[0]);
	}

	void game_magnification()
	{
		// Live 2026-11-04: hip 45 deg, AK-12 red dot 40 deg, flip magnifier 27 deg.
		expect(near(magnification(45 * degree, 27 * degree), 1.725f, 0.005f), "magnifier 1.73x");
		expect(near(magnification(45 * degree, 40 * degree), 1.138f, 0.005f), "red dot 1.14x");
		expect(magnification(45 * degree, 45 * degree) == 1.0f, "hip: none");
		expect(magnification(45 * degree, 60 * degree) == 1.0f, "wider: none");
		expect(magnification(0.0f, 27 * degree) == 1.0f, "no hip value: none");
	}

	void scope_along_the_sight()
	{
		// Gun pointing straight ahead (-z), unrotated Wep_Root would point +z: turn 180 deg about y.
		const float rotation[4] = { 0, 1, 0, 0 };
		const float sight[3] = { 0, 0, 1 };
		const float eye_point[3] = { 0.03f, -0.05f, -0.1f };
		pose eye;
		eye.position[0] = 0.032f;
		eye.position[1] = -0.05f;
		eye.position[2] = -0.1f;
		lens optic;
		optic.distance = 0.08f;
		optic.radius = 0.012f;
		optic.crop = 0.3f;
		const plan p = plan_scope(eye, eye_point, rotation, sight, 2.0f, optic);
		expect(p.valid, "plan valid");
		float forward[3];
		rotate(p.camera, { 0, 0, -1 }, forward);
		expect(near(forward[0], 0) && near(forward[1], 0) && near(forward[2], -1), "camera looks along the sight");
		float up[3];
		rotate(p.camera, { 0, 1, 0 }, up);
		expect(near(up[1], 1), "camera up = gun up");
		expect(near(p.quad.position[2], -0.18f) && near(p.quad.position[0], 0.03f), "lens 8 cm ahead of the eye point");
		expect(near(p.quad_size, 0.024f), "quad = lens diameter");
		// The lens subtends atan(0.012 / 0.08) from the eye; magnified 2x it shows half that tangent.
		const float tan_lens = 0.012f / 0.08f / 2.0f;
		expect(near(std::tan(0.5f * p.lens_fov_y), tan_lens, 1e-4f), "lens FOV");
		expect(near(std::tan(0.5f * p.render_fov_y), tan_lens / 0.3f, 1e-4f), "render FOV: lens covers the crop");

		// A gun canted 30 deg about its barrel: the scope image turns with it.
		const float half = 15 * degree;
		const float roll[4] = { std::sin(half), std::cos(half), 0, 0 }; // 180 about y, then 30 about the barrel (local z)
		const plan canted = plan_scope(eye, eye_point, roll, sight, 2.0f, optic);
		float canted_up[3];
		rotate(canted.camera, { 0, 1, 0 }, canted_up);
		expect(near(std::fabs(canted_up[0]), std::sin(30 * degree), 2e-3f) && near(canted_up[1], std::cos(30 * degree), 2e-3f), "camera rolls with the gun");

		expect(!plan_scope(eye, eye_point, rotation, sight, 0.5f, optic).valid, "magnification below 1 refused");
	}

	void relative_and_rows()
	{
		pose from;
		from.position[0] = 1;
		from.position[1] = 2;
		from.position[2] = 3;
		const float s = std::sin(45 * degree), c = std::cos(45 * degree);
		from.orientation[1] = s; // 90 deg about y
		from.orientation[3] = c;
		pose to = from;
		to.position[2] -= 0.5f; // world -z: half a metre
		const pose r = relative_pose(from, to);
		// From's frame is turned 90 deg left: world -z is its +x... rotate back: local = R^-1 (0,0,-0.5) = (0.5, 0, 0).
		expect(near(r.position[0], 0.5f) && near(r.position[1], 0) && near(r.position[2], 0), "relative position in from's frame");
		expect(near(std::fabs(r.orientation[3]), 1), "same orientation: identity");

		// Camera rows of an identity camera at (10, 0, 0): a relative offset maps straight through.
		const float camera[16] = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 10, 0, 0, 1 };
		pose moved;
		moved.position[0] = 0.1f;
		moved.position[2] = -0.2f;
		moved.orientation[1] = s;
		moved.orientation[3] = c;
		float out[16];
		compose_rows(camera, moved, out);
		expect(near(out[12], 10.1f) && near(out[14], -0.2f), "composed position");
		// 90 deg about y: right becomes (0, 0, -1), back becomes (1, 0, 0).
		expect(near(out[0], 0) && near(out[2], -1) && near(out[8], 1) && near(out[10], 0), "composed axes");
		expect(out[15] == 1.0f && out[3] == 0.0f, "fourth column kept");

		// A turned camera: offsets are along its own axes.
		const float turned[16] = { 0, 0, -1, 0, 0, 1, 0, 0, 1, 0, 0, 0, 0, 0, 0, 1 };
		pose ahead;
		ahead.position[2] = -1; // one metre ahead of the camera
		compose_rows(turned, ahead, out);
		expect(near(out[12], -1) && near(out[14], 0), "ahead of a turned camera");
	}
}

// #39: the zoom learned while the game aims answers at the hip, per weapon and level.
void learned_magnification()
{
	magnification_memory memory;
	expect(near(memory.lookup("U_AK12", 2, 1.73f), 1.73f), "unknown: the fallback");
	memory.remember("U_AK12", 2, 1.7f);
	memory.remember("U_AK12", 1, 1.14f);
	expect(near(memory.lookup("U_AK12", 2, 1.0f), 1.7f) && near(memory.lookup("U_AK12", 1, 1.0f), 1.14f), "per level");
	expect(near(memory.lookup("U_SCAR-H", 2, 1.0f), 1.0f), "per weapon");
	memory.remember("U_AK12", 2, 1.75f);
	expect(near(memory.lookup("U_AK12", 2, 1.0f), 1.75f) && memory.size() == 2, "a new reading replaces the old one");
	memory.remember("U_AK12", 3, std::nanf(""));
	memory.remember("U_AK12", 3, 0.5f);
	memory.remember("", 3, 2.0f);
	expect(memory.size() == 2, "nonsense is not kept");
	for (int i = 0; i < 40; ++i)
	{
		memory.remember("U_GUN", i, 2.0f);
	}
	expect(memory.size() == 32 && near(memory.lookup("U_GUN", 39, 1.0f), 2.0f) && near(memory.lookup("U_AK12", 2, 1.0f), 1.0f), "full: the oldest goes");
}

int main()
{
	game_magnification();
	scope_along_the_sight();
	relative_and_rows();
	learned_magnification();
	if (failures)
	{
		std::printf("%d failure(s)\n", failures);
		return 1;
	}
	std::printf("scope tests passed\n");
	return 0;
}
