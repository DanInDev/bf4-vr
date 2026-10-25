// Checks the engine-free per-eye maths (src/stereo/eye_math): pose composition, the rendered
// frustum, the eye midpoint, parallel-eye detection and the complete-pairs-only rule.
// Proves the maths only; whether the engine draws what these numbers say needs the game.
#include <cmath>
#include <cstdio>
#include <random>

#include "stereo/eye_math.hpp"

using namespace bfvr::stereo;
using namespace bfvr::stereo::eye_math;

namespace
{
	int failures = 0;

	void expect(bool condition, const char* what, int round = -1)
	{
		if (!condition)
		{
			std::printf("FAIL round %d: %s\n", round, what);
			++failures;
		}
	}

	bool near(float a, float b, float tolerance = 1e-4f)
	{
		return std::fabs(a - b) <= tolerance;
	}

	pose yaw_pose(float radians, float x, float y, float z)
	{
		pose p;
		p.orientation[1] = std::sin(radians / 2);
		p.orientation[3] = std::cos(radians / 2);
		p.position[0] = x;
		p.position[1] = y;
		p.position[2] = z;
		return p;
	}

	void identity_camera(float (&camera)[16], float x, float y, float z)
	{
		const float rows[16] = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, x, y, z, 1 };
		for (int i = 0; i < 16; ++i)
		{
			camera[i] = rows[i];
		}
	}

	pose random_pose(std::mt19937& random)
	{
		std::uniform_real_distribution<float> unit(-1.0f, 1.0f);
		pose p;
		float length = 0;
		for (float& q : p.orientation)
		{
			q = unit(random);
			length += q * q;
		}
		length = std::sqrt(length);
		for (float& q : p.orientation)
		{
			q /= length;
		}
		for (float& axis : p.position)
		{
			axis = 0.5f * unit(random);
		}
		return p;
	}

	void test_compose()
	{
		// Identity pose leaves the camera unchanged.
		float camera[16];
		identity_camera(camera, 3, 4, 5);
		compose_pose(camera, pose{});
		expect(near(camera[0], 1) && near(camera[5], 1) && near(camera[10], 1) && near(camera[12], 3) && near(camera[14], 5), "identity pose moved the camera");

		// A pure eye offset moves the position along the camera's own axes.
		float rotated[16] = { 0, 0, -1, 0, 0, 1, 0, 0, 1, 0, 0, 0, 10, 0, 0, 1 }; // camera turned 90 degrees
		compose_pose(rotated, yaw_pose(0, 0.032f, 0, 0));
		expect(near(rotated[12], 10) && near(rotated[13], 0) && near(rotated[14], -0.032f), "eye offset not along the camera's right row");

		// A 90-degree head yaw to the left: the camera's forward row (+z local) becomes the old right row's negative.
		identity_camera(camera, 0, 0, 0);
		compose_pose(camera, yaw_pose(1.5707963f, 0, 0, 0));
		expect(near(camera[8], 1) && near(camera[10], 0, 1e-3f), "yaw did not turn the forward row");
		expect(near(camera[0], 0, 1e-3f) && near(camera[2], -1), "yaw did not turn the right row");

		// Rows stay orthonormal and the two eyes stay one IPD apart for random head poses.
		std::mt19937 random(7);
		for (int round = 0; round < 200; ++round)
		{
			pose left = random_pose(random);
			pose right = left;
			const float ipd = 0.063f;
			// Right eye: the left eye moved along its own +x axis.
			const float qx = left.orientation[0], qy = left.orientation[1], qz = left.orientation[2], qw = left.orientation[3];
			const float x_axis[3] = { 1 - 2 * (qy * qy + qz * qz), 2 * (qx * qy + qz * qw), 2 * (qx * qz - qy * qw) };
			for (int axis = 0; axis < 3; ++axis)
			{
				right.position[axis] += ipd * x_axis[axis];
			}

			float base[16];
			identity_camera(base, 1, 2, 3);
			const float yaw = 0.7f;
			base[0] = std::cos(yaw), base[2] = -std::sin(yaw), base[8] = std::sin(yaw), base[10] = std::cos(yaw);
			float a[16], b[16];
			for (int i = 0; i < 16; ++i)
			{
				a[i] = b[i] = base[i];
			}
			compose_pose(a, left);
			compose_pose(b, right);
			for (int r = 0; r < 3; ++r)
			{
				const float* row = a + r * 4;
				expect(near(row[0] * row[0] + row[1] * row[1] + row[2] * row[2], 1, 1e-3f), "row not unit length", round);
				const float* next = a + ((r + 1) % 3) * 4;
				expect(near(row[0] * next[0] + row[1] * next[1] + row[2] * next[2], 0, 1e-3f), "rows not orthogonal", round);
			}
			const float dx = b[12] - a[12], dy = b[13] - a[13], dz = b[14] - a[14];
			expect(near(std::sqrt(dx * dx + dy * dy + dz * dz), ipd, 1e-4f), "eyes not one IPD apart", round);
			// The right eye lies along the composed camera's right row.
			expect(near((dx * a[0] + dy * a[1] + dz * a[2]) / ipd, 1, 1e-3f), "eye offset not along the right row", round);
		}
	}

	void test_fov()
	{
		const fov quest[2] = { { -0.942f, 0.698f, 0.768f, -0.890f }, { -0.698f, 0.942f, 0.768f, -0.890f } };
		const render_fov full = symmetric_fov(quest, 1, 1, true, 16.0f / 9.0f);
		expect(near(full.half_horizontal, 0.942f) && near(full.half_vertical, 0.890f), "symmetric FOV does not cover both eyes");
		expect(near(full.aspect, std::tan(0.942f) / std::tan(0.890f)), "headset aspect wrong");

		const render_fov cropped = symmetric_fov(quest, 0.8f, 0.9f, true, 16.0f / 9.0f);
		expect(near(std::tan(cropped.half_horizontal), 0.8f * std::tan(0.942f)), "horizontal crop not in tangent space");
		expect(near(std::tan(cropped.half_vertical), 0.9f * std::tan(0.890f)), "vertical crop not in tangent space");

		const render_fov game = symmetric_fov(quest, 1, 1, false, 16.0f / 9.0f);
		expect(game.aspect == 0.0f, "game aspect mode must not override the aspect");
		expect(near(std::tan(game.half_horizontal), std::tan(0.890f) * 16.0f / 9.0f), "game aspect horizontal FOV wrong");
	}

	void test_family()
	{
		head_sample head;
		head.valid = true;
		head.eyes[0] = yaw_pose(0.3f, -0.032f, 1.6f, 0.1f);
		head.eyes[1] = yaw_pose(0.3f, 0.032f, 1.6f, 0.1f);
		const pose centre = centre_pose(head);
		expect(near(centre.position[0], 0) && near(centre.position[1], 1.6f) && near(centre.position[2], 0.1f), "centre is not the eye midpoint");
		expect(near(centre.orientation[1], head.eyes[0].orientation[1]), "centre orientation changed");
		expect(eye_angle(head) < 1e-3f, "parallel eyes reported as canted");
		head.eyes[1] = yaw_pose(0.3f + 0.1f, 0.032f, 1.6f, 0.1f);
		expect(near(eye_angle(head), 0.1f, 1e-3f), "eye angle wrong");
		// q and -q are the same rotation.
		for (float& q : head.eyes[1].orientation)
		{
			q = -q;
		}
		expect(near(eye_angle(head), 0.1f, 1e-3f), "eye angle depends on the quaternion sign");
	}

	void test_pair()
	{
		eye_pair pair;
		expect(!pair.rendered(0, 1), "eye accepted before any frame began");
		expect(pair.rejected() == 1, "rejection not counted");

		pair.begin(5);
		expect(!pair.complete(), "new frame already complete");
		expect(pair.rendered(0, 5), "left eye rejected");
		expect(!pair.complete(), "one eye completes the pair");
		expect(!pair.rendered(0, 5), "left eye accepted twice");
		expect(!pair.rendered(1, 4), "stale eye accepted");
		expect(!pair.rendered(2, 5), "eye index 2 accepted");
		expect(pair.rendered(1, 5), "right eye rejected");
		expect(pair.complete(), "both eyes do not complete the pair");
		expect(pair.rejected() == 4, "rejections miscounted");

		pair.begin(6);
		expect(!pair.complete() && pair.abandoned() == 0, "completed frame counted as abandoned");
		pair.rendered(0, 6);
		pair.begin(7);
		expect(pair.abandoned() == 1, "half frame not counted as abandoned");
		expect(!pair.rendered(1, 6), "eye of an abandoned frame accepted");
	}
}

int main()
{
	test_compose();
	test_fov();
	test_family();
	test_pair();
	std::printf(failures ? "eye_math_test: %d failures\n" : "eye_math_test: all passed\n", failures);
	return failures ? 1 : 0;
}
