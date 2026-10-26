// Checks the engine-free per-eye maths (src/stereo/eye_math): pose composition, the rendered
// frustum, the eye midpoint, parallel-eye detection and the complete-pairs-only rule.
// Proves the maths only; whether the engine draws what these numbers say needs the game.
#include <cmath>
#include <cstdio>
#include <random>

#include "stereo/constant_patch.hpp"
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

		// Pair rendering: the right eye is the left eye moved by the IPD along its own right axis.
		head.eyes[0] = yaw_pose(0.5f, 0.1f, 1.6f, -0.2f);
		head.eyes[1] = moved_along_right(head.eyes[0], 0.063f);
		expect(near(eye_distance(head), 0.063f), "eye distance wrong");
		const float x_after_yaw = std::cos(0.5f), z_after_yaw = -std::sin(0.5f); // +x turned 0.5 rad about +y
		expect(near(head.eyes[1].position[0] - head.eyes[0].position[0], 0.063f * x_after_yaw)
			&& near(head.eyes[1].position[2] - head.eyes[0].position[2], 0.063f * z_after_yaw), "right axis wrong");
		expect(eye_angle(head) < 1e-4f, "moving changed the orientation");
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

	void test_constant_patch()
	{
		// A 32-float "RenderView": camera position at 4..6, a view-projection at 16..31; the eye copy
		// moves both.
		float centre[32]{};
		float eye[32]{};
		centre[4] = 10, centre[5] = 1.6f, centre[6] = -3;
		for (int i = 0; i < 16; ++i)
		{
			centre[16 + i] = 0.25f * static_cast<float>(i) - 1.5f;
		}
		for (int i = 0; i < 32; ++i)
		{
			eye[i] = centre[i];
		}
		eye[4] += 0.064f;
		eye[28] += 0.087f; // translation row of the view-projection
		constant_patch patch;
		patch.add_view(centre, eye, 32);
		expect(patch.matrices() >= 1 && patch.positions() >= 1, "changed windows not collected");

		// Block: a world matrix (must stay), the centre view-projection 16-byte aligned, the camera position.
		float block[64]{};
		for (int i = 0; i < 12; ++i)
		{
			block[i] = 100.0f + static_cast<float>(i);
		}
		for (int i = 0; i < 16; ++i)
		{
			block[16 + i] = centre[16 + i];
		}
		block[40] = 10, block[41] = 1.6f, block[42] = -3;
		const int replaced = patch.apply(block, 64);
		expect(replaced == 2, "wrong number of replacements");
		expect(near(block[28], centre[28] + 0.087f) && near(block[16], centre[16]), "view-projection not replaced by the eye's");
		expect(near(block[40], 10.064f), "camera position not replaced");
		expect(block[0] == 100.0f && block[11] == 111.0f, "world matrix changed");

		// Unaligned copies are not constant-buffer fields and stay.
		float unaligned[40]{};
		for (int i = 0; i < 16; ++i)
		{
			unaligned[2 + i] = centre[16 + i];
		}
		expect(patch.apply(unaligned, 40) == 0, "unaligned window replaced");

		// Rounding differences of the engine's own copy still match.
		for (int i = 0; i < 16; ++i)
		{
			block[16 + i] = centre[16 + i];
		}
		block[19] *= 1.0f + 2e-7f;
		expect(patch.apply(block, 64) == 1, "rounded copy not matched");
	}
}

namespace
{
	// Row-vector convention like the engine: clip = [x y z 1] * V * P.
	void multiply(const float* a, const float* b, float* out)
	{
		for (int r = 0; r < 4; ++r)
		{
			for (int c = 0; c < 4; ++c)
			{
				out[r * 4 + c] = a[r * 4 + 0] * b[0 * 4 + c] + a[r * 4 + 1] * b[1 * 4 + c] + a[r * 4 + 2] * b[2 * 4 + c] + a[r * 4 + 3] * b[3 * 4 + c];
			}
		}
	}

	// View matrix of a camera whose rows are right, up, forward and whose position is p.
	void view_of(const float (&rot)[9], const float (&p)[3], float* v)
	{
		for (int r = 0; r < 3; ++r)
		{
			for (int c = 0; c < 3; ++c)
			{
				v[r * 4 + c] = rot[c * 3 + r]; // transpose: world -> camera axes
			}
			v[r * 4 + 3] = 0;
		}
		for (int c = 0; c < 3; ++c)
		{
			v[12 + c] = -(p[0] * rot[c * 3 + 0] + p[1] * rot[c * 3 + 1] + p[2] * rot[c * 3 + 2]);
		}
		v[15] = 1;
	}

	void test_view_projection_shift()
	{
		const float sx = 1.358f, sy = 2.414f, near_plane = 0.06f;
		// Reverse-z infinite projection as in the RenderView (+0x320).
		const float projection[16] = { sx, 0, 0, 0, 0, sy, 0, 0, 0, 0, -1, -1, 0, 0, -near_plane, 0 };
		std::mt19937 random(11);
		for (int round = 0; round < 100; ++round)
		{
			const pose head = random_pose(random);
			const float qx = head.orientation[0], qy = head.orientation[1], qz = head.orientation[2], qw = head.orientation[3];
			const float rot[9] = { 1 - 2 * (qy * qy + qz * qz), 2 * (qx * qy + qz * qw), 2 * (qx * qz - qy * qw),
				2 * (qx * qy - qz * qw), 1 - 2 * (qx * qx + qz * qz), 2 * (qy * qz + qx * qw),
				2 * (qx * qz + qy * qw), 2 * (qy * qz - qx * qw), 1 - 2 * (qx * qx + qy * qy) };
			const float p[3] = { 100.0f * head.position[0], 50.0f + head.position[1], -300.0f + head.position[2] };
			const float d = 0.064f;
			const float p_eye[3] = { p[0] + d * rot[0], p[1] + d * rot[1], p[2] + d * rot[2] };
			float v[16], v_eye[16], vp[16], vp_eye[16];
			view_of(rot, p, v);
			view_of(rot, p_eye, v_eye);
			multiply(v, projection, vp);
			multiply(v_eye, projection, vp_eye);

			float block[48]{};
			for (int i = 0; i < 16; ++i)
			{
				block[4 + i] = vp[i];            // row-vector storage at +0x10
				block[24 + i] = vp[(i % 4) * 4 + i / 4]; // transposed storage at +0x60
			}
			const int shifted = constant_patch::shift_view_projections(block, 48, sx, sy, d);
			expect(shifted == 2, "view-projections not found in both storage orders", round);
			bool ok = true;
			for (int i = 0; i < 16; ++i)
			{
				ok = ok && near(block[4 + i], vp_eye[i], 2e-3f) && near(block[24 + i], vp_eye[(i % 4) * 4 + i / 4], 2e-3f);
			}
			expect(ok, "shifted view-projection differs from the moved camera's", round);
		}

		// A pure projection (camera-space) stays.
		float block[16];
		for (int i = 0; i < 16; ++i)
		{
			block[i] = projection[i];
		}
		expect(constant_patch::shift_view_projections(block, 16, sx, sy, 0.064f) == 0, "pure projection shifted");
		// A view-projection with another projection (shadow cascade) stays.
		float other[16] = { 0.1f, 0, 0, 0, 0, 0.1f, 0, 0, 0, 0, 0.01f, 0, 5, 3, 0.5f, 1 };
		expect(constant_patch::shift_view_projections(other, 16, sx, sy, 0.064f) == 0, "foreign view-projection shifted");
	}
}

namespace
{
	// A 512-byte per-view block (layout of constant_patch::shift_per_view_block) for a camera with
	// axes rot (rows right, up, back) at p, column-vector convention.
	void per_view_block(const float (&rot)[9], const float (&p)[3], float sx, float sy, float* block)
	{
		const float near_plane = 0.06f;
		for (int i = 0; i < 128; ++i)
		{
			block[i] = 0;
		}
		float view[16]{};
		for (int r = 0; r < 3; ++r)
		{
			for (int c = 0; c < 3; ++c)
			{
				view[r * 4 + c] = rot[r * 3 + c];
			}
			view[r * 4 + 3] = -(rot[r * 3 + 0] * p[0] + rot[r * 3 + 1] * p[1] + rot[r * 3 + 2] * p[2]);
		}
		view[15] = 1;
		const float projection[16] = { sx, 0, 0, 0, 0, sy, 0, 0, 0, 0, -1, -near_plane, 0, 0, -1, 0 };
		for (int r = 0; r < 4; ++r)
		{
			for (int c = 0; c < 4; ++c)
			{
				float sum = 0, rotation_only = 0;
				for (int k = 0; k < 4; ++k)
				{
					sum += projection[r * 4 + k] * view[k * 4 + c];
					rotation_only += projection[r * 4 + k] * (c < 3 || k == 3 ? view[k * 4 + c] : 0.0f);
				}
				block[0xb0 / 4 + r * 4 + c] = sum;
				block[0xf0 / 4 + r * 4 + c] = rotation_only;
				block[0x70 / 4 + r * 4 + c] = projection[r * 4 + c];
				block[0x30 / 4 + r * 4 + c] = view[r * 4 + c];
			}
		}
		block[0x130 / 4 + 0] = sx;
		block[0x130 / 4 + 1] = sy;
		block[0x130 / 4 + 2] = 1;
		block[0x130 / 4 + 3] = -near_plane;
		for (int axis = 0; axis < 3; ++axis)
		{
			block[0x140 / 4 + axis] = p[axis];
		}
	}

	void test_per_view_block()
	{
		std::mt19937 random(5);
		for (int round = 0; round < 100; ++round)
		{
			const pose camera = random_pose(random);
			const float qx = camera.orientation[0], qy = camera.orientation[1], qz = camera.orientation[2], qw = camera.orientation[3];
			const float rot[9] = { 1 - 2 * (qy * qy + qz * qz), 2 * (qx * qy + qz * qw), 2 * (qx * qz - qy * qw),
				2 * (qx * qy - qz * qw), 1 - 2 * (qx * qx + qz * qz), 2 * (qy * qz + qx * qw),
				2 * (qx * qz + qy * qw), 2 * (qy * qz - qx * qw), 1 - 2 * (qx * qx + qy * qy) };
			const float p[3] = { 2.0f * camera.position[0], 1.6f + camera.position[1], camera.position[2] };
			const float d = 0.064f;
			const float p_eye[3] = { p[0] + d * rot[0], p[1] + d * rot[1], p[2] + d * rot[2] };
			float block[128], expected[128];
			per_view_block(rot, p, 2.001f, 2.414f, block);
			per_view_block(rot, p_eye, 2.001f, 2.414f, expected);
			expect(constant_patch::shift_per_view_block(block, 128, d), "per-view block not recognised", round);
			bool ok = true;
			for (int i = 0; i < 128; ++i)
			{
				ok = ok && near(block[i], expected[i], 1e-3f);
			}
			expect(ok, "shifted per-view block differs from the moved camera's", round);
		}
		// Arbitrary data is not a per-view block.
		float noise[128];
		std::mt19937 random_noise(9);
		std::uniform_real_distribution<float> unit(-1.0f, 1.0f);
		for (float& value : noise)
		{
			value = unit(random_noise);
		}
		expect(!constant_patch::shift_per_view_block(noise, 128, 0.064f), "noise taken for a per-view block");
	}
}

int main()
{
	test_per_view_block();
	test_view_projection_shift();
	test_compose();
	test_fov();
	test_family();
	test_pair();
	test_constant_patch();
	std::printf(failures ? "eye_math_test: %d failures\n" : "eye_math_test: all passed\n", failures);
	return failures ? 1 : 0;
}
