// Checks the shot frame at the gun (src/weapon/shot_frame): it starts at the gun's sight line, points
// along the barrel in the world, stays a rotation of the game's frame, and never reaches far from the
// camera.
#include <cmath>
#include <cstdio>
#include <random>

#include "weapon/shot_frame.hpp"

using namespace bfvr::weapon;

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

	bool close(float a, float b, float tolerance = 1e-4f)
	{
		return std::fabs(a - b) <= tolerance;
	}

	float row_dot(const float (&m)[16], int a, int b)
	{
		return m[a * 4] * m[b * 4] + m[a * 4 + 1] * m[b * 4 + 1] + m[a * 4 + 2] * m[b * 4 + 2];
	}

	// The game's frame as seen live (2026-11-05): level-ish rows, forward = row 2, position = row 3.
	void game_frame(float yaw, float pitch, float (&m)[16])
	{
		const float cy = std::cos(yaw), sy = std::sin(yaw), cp = std::cos(pitch), sp = std::sin(pitch);
		const float forward[3]{ sy * cp, sp, cy * cp };
		const float right[3]{ cy, 0.0f, -sy };
		const float up[3]{ forward[1] * right[2] - forward[2] * right[1], forward[2] * right[0] - forward[0] * right[2], forward[0] * right[1] - forward[1] * right[0] };
		const float position[3]{ -435.3f, 399.1f, -645.6f };
		for (int i = 0; i < 3; ++i)
		{
			m[i] = right[i];
			m[4 + i] = up[i];
			m[8 + i] = forward[i];
			m[12 + i] = position[i];
		}
		m[3] = m[7] = m[11] = m[15] = 0.0f;
	}
}

int main()
{
	std::mt19937 random(3);
	std::uniform_real_distribution<float> unit(-1.0f, 1.0f);
	for (int round = 0; round < 300; ++round)
	{
		const float body = unit(random) * 3.1f;
		float game[16];
		game_frame(body + unit(random) * 0.5f, unit(random) * 0.4f, game);
		shot_line_local line;
		line.start[0] = unit(random) * 0.3f;
		line.start[1] = -0.2f + unit(random) * 0.1f;
		line.start[2] = -0.3f + unit(random) * 0.1f;
		const float yaw = unit(random), pitch = unit(random) * 0.6f;
		// LOCAL forward is -z: a yaw left turns it towards -x.
		line.direction[0] = -std::sin(yaw) * std::cos(pitch);
		line.direction[1] = std::sin(pitch);
		line.direction[2] = -std::cos(yaw) * std::cos(pitch);
		const float lift = round % 3 == 0 ? 0.4f : 0.0f;
		float out[16];
		shot_frame_at_gun(game, body, lift, line, 0.0f, out);

		// Expected, by hand: LOCAL axes in the world for camera yaw `body` (camera_hook level_rows).
		const float s = std::sin(body), k = std::cos(body);
		const auto world = [&](const float (&v)[3], float (&w)[3]) {
			w[0] = -k * v[0] - s * v[2];
			w[1] = v[1];
			w[2] = s * v[0] - k * v[2];
		};
		float start[3], direction[3];
		world(line.start, start);
		world(line.direction, direction);
		expect(close(out[12], game[12] + start[0]) && close(out[13], game[13] + start[1] + lift) && close(out[14], game[14] + start[2]),
			"it starts at the gun's sight line (plus the stance lift), from the camera");
		expect(close(out[8], direction[0]) && close(out[9], direction[1]) && close(out[10], direction[2]), "it points along the barrel in the world");
		bool rotation = true;
		for (int a = 0; a < 3; ++a)
		{
			for (int b = 0; b < 3; ++b)
			{
				rotation = rotation && close(row_dot(out, a, b), a == b ? 1.0f : 0.0f, 1e-4f);
			}
		}
		expect(rotation, "the rows stay orthonormal");
		const float handed_game = game[0] * (game[5] * game[10] - game[6] * game[9]) - game[1] * (game[4] * game[10] - game[6] * game[8]) + game[2] * (game[4] * game[9] - game[5] * game[8]);
		const float handed_out = out[0] * (out[5] * out[10] - out[6] * out[9]) - out[1] * (out[4] * out[10] - out[6] * out[8]) + out[2] * (out[4] * out[9] - out[5] * out[8]);
		expect(close(handed_game, handed_out, 1e-3f), "the game's handedness is kept");
		expect(out[3] == game[3] && out[7] == game[7] && out[11] == game[11] && out[15] == game[15], "the fourth column is left as it was");
	}
	// Looking straight back from the game's forward still gives a proper frame.
	float game[16];
	game_frame(0.0f, 0.0f, game);
	shot_line_local back;
	back.direction[2] = 1.0f; // LOCAL +z: behind, world -z at body yaw 0... i.e. the game's forward reversed
	float out[16];
	shot_frame_at_gun(game, 0.0f, 0.0f, back, 0.0f, out);
	expect(close(out[10], -1.0f) && close(row_dot(out, 0, 0), 1.0f) && close(row_dot(out, 0, 2), 0.0f), "half a turn round works");
	// A start far out is pulled in to the reach limit.
	shot_line_local far;
	far.start[0] = 2.0f;
	far.direction[2] = -1.0f;
	shot_frame_at_gun(game, 0.0f, 0.0f, far, 0.8f, out);
	const float dx = out[12] - game[12], dy = out[13] - game[13], dz = out[14] - game[14];
	expect(close(std::sqrt(dx * dx + dy * dy + dz * dz), 0.8f), "never further than max_reach from the camera");
	// Bad input leaves the game's frame alone.
	shot_line_local none;
	shot_frame_at_gun(game, 0.0f, 0.0f, none, 0.8f, out);
	bool same = true;
	for (int i = 0; i < 16; ++i)
	{
		same = same && out[i] == game[i];
	}
	expect(same, "no direction: the game's frame unchanged");
	std::printf(failures ? "shot_frame_test: %d failures\n" : "shot_frame_test: all passed\n", failures);
	return failures ? 1 : 0;
}
