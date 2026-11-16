// Checks the bone palette copy for extra meshes (src/stereo/palette_copy): tagged entries are moved by d,
// all others collapse to zero, and a point skinned to a tagged bone lands where d takes it.
#include <cmath>
#include <cstdio>
#include <random>

#include "stereo/palette_copy.hpp"

using namespace bfvr::stereo;

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

	// A rotation (from a unit quaternion) times scale, translation t, as 3x4 rows.
	void make(float* m, float qx, float qy, float qz, float qw, float scale, float tx, float ty, float tz)
	{
		const float n = std::sqrt(qx * qx + qy * qy + qz * qz + qw * qw);
		qx /= n, qy /= n, qz /= n, qw /= n;
		const float r[9] = { 1 - 2 * (qy * qy + qz * qz), 2 * (qx * qy - qz * qw), 2 * (qx * qz + qy * qw), 2 * (qx * qy + qz * qw), 1 - 2 * (qx * qx + qz * qz),
			2 * (qy * qz - qx * qw), 2 * (qx * qz - qy * qw), 2 * (qy * qz + qx * qw), 1 - 2 * (qx * qx + qy * qy) };
		const float t[3] = { tx, ty, tz };
		for (int row = 0; row < 3; ++row)
		{
			for (int k = 0; k < 3; ++k)
			{
				m[row * 4 + k] = r[row * 3 + k] * scale;
			}
			m[row * 4 + 3] = t[row];
		}
	}

	void apply(const float* m, const float (&p)[3], float (&out)[3])
	{
		for (int row = 0; row < 3; ++row)
		{
			out[row] = m[row * 4] * p[0] + m[row * 4 + 1] * p[1] + m[row * 4 + 2] * p[2] + m[row * 4 + 3];
		}
	}
}

int main()
{
	constexpr float tag = 1.002f;
	std::mt19937 random(5);
	std::uniform_real_distribution<float> unit(-1.0f, 1.0f);
	for (int round = 0; round < 200; ++round)
	{
		float pal[palette::entries * palette::floats_per_entry];
		bool is_tagged[palette::entries];
		for (int e = 0; e < palette::entries; ++e)
		{
			is_tagged[e] = e == 0 || e == 1 || (e == 7 && round % 2 == 0);
			make(pal + e * 12, unit(random), unit(random), unit(random), unit(random), is_tagged[e] ? tag : 1.0f, unit(random), 1.9f + unit(random), unit(random));
		}
		float original[palette::entries * palette::floats_per_entry];
		for (int i = 0; i < palette::entries * 12; ++i)
		{
			original[i] = pal[i];
		}
		float d[12];
		make(d, unit(random), unit(random), unit(random), unit(random), 1.0f, unit(random), unit(random), unit(random));
		const int moved = palette::move_tagged(pal, d, tag);
		expect(moved == (round % 2 == 0 ? 3 : 2), "the tagged entries are found, no others");
		const float point[3] = { unit(random) * 0.2f, unit(random) * 0.2f, unit(random) * 0.2f };
		for (int e = 0; e < palette::entries; ++e)
		{
			float* m = pal + e * 12;
			if (!is_tagged[e])
			{
				bool zero = true;
				for (int i = 0; i < 12; ++i)
				{
					zero = zero && m[i] == 0.0f;
				}
				expect(zero, "an untagged entry collapses to zero");
				continue;
			}
			// A vertex skinned to this bone: d applied after the bone's own matrix.
			float skinned[3], expected[3], copied[3];
			apply(original + e * 12, point, skinned);
			apply(d, skinned, expected);
			apply(m, point, copied);
			const float error = std::sqrt((copied[0] - expected[0]) * (copied[0] - expected[0]) + (copied[1] - expected[1]) * (copied[1] - expected[1])
				+ (copied[2] - expected[2]) * (copied[2] - expected[2]));
			expect(error < 1e-4f, "a vertex of a tagged bone lands where d takes it");
			expect(palette::tagged(m, tag), "the moved entry keeps its scale (d is a rotation)");
		}
	}
	// The held grenade: captured while its root sat at root, unrotated, scale 0.01; relative_to_root then
	// d * entry draws each vertex where it would be with the root at d.
	for (int round = 0; round < 100; ++round)
	{
		constexpr float grenade_tag = 0.01f;
		const float root[3] = { 0.0f, 1.0f, 0.0f };
		// The bone below the root (its local offset and bind combined): any rigid transform.
		float local[12];
		make(local, unit(random), unit(random), unit(random), unit(random), 1.0f, unit(random) * 0.1f, unit(random) * 0.1f, unit(random) * 0.1f);
		float hidden_root[12];
		make(hidden_root, 0, 0, 0, 1, grenade_tag, root[0], root[1], root[2]);
		float entry[12];
		palette::multiply(hidden_root, local, entry);
		expect(palette::tagged(entry, grenade_tag), "a grenade entry carries the grenade tag");
		expect(!palette::tagged(entry, tag), "the grenade tag is not the magazine's");
		// The gun's magazine during a capture: 0.01 * 1.5, told apart from the grenade's 0.01.
		float marked[12];
		make(marked, 0.1f, 0.2f, 0.3f, 0.9f, grenade_tag * 1.5f, root[0], root[1], root[2]);
		expect(!palette::tagged(marked, grenade_tag) && palette::tagged(marked, grenade_tag * 1.5f), "the marked magazine is not a grenade entry");
		palette::relative_to_root(entry, grenade_tag, root);
		float d[12];
		make(d, unit(random), unit(random), unit(random), unit(random), 1.0f, unit(random), 1.5f + unit(random), unit(random));
		float placed[12];
		palette::multiply(d, entry, placed);
		const float point[3] = { unit(random) * 0.05f, unit(random) * 0.05f, unit(random) * 0.05f };
		float at_local[3], expected[3], copied[3];
		apply(local, point, at_local);
		apply(d, at_local, expected);
		apply(placed, point, copied);
		const float error = std::sqrt((copied[0] - expected[0]) * (copied[0] - expected[0]) + (copied[1] - expected[1]) * (copied[1] - expected[1])
			+ (copied[2] - expected[2]) * (copied[2] - expected[2]));
		expect(error < 1e-4f, "a captured grenade vertex lands where its root at d puts it");
		expect(palette::tagged(placed, 1.0f), "the placed grenade has scale 1");
	}
	float plain[12];
	make(plain, 0.1f, 0.2f, 0.3f, 0.9f, 1.0f, 0, 2, 0);
	expect(!palette::tagged(plain, tag), "scale 1 is not the tag");
	make(plain, 0.1f, 0.2f, 0.3f, 0.9f, 0.001f, 0, 2, 0);
	expect(!palette::tagged(plain, tag), "the hidden legs' 0.001 is not the tag");
	{
		// #41 (monitor 2026-11-16): the knife's pre-pass and depth draws were listed among the gun's (level start with the
		// knife out); its G-buffer draw from the same index buffer was not. All three stay; a gun-only buffer goes.
		int knife_buffer = 0, glove_buffer = 0;
		const std::vector<const void*> buffers = { &knife_buffer, &knife_buffer, &knife_buffer, &glove_buffer };
		const std::vector<bool> keep = { true, true, true, true };
		const std::vector<bool> seen = { true, false, true, true };
		const std::vector<bool> kept = palette::keep_knife_draws(keep, seen, buffers);
		expect(kept[0] && kept[1] && kept[2], "the knife's own draws seen with the gun stay");
		expect(!kept[3], "a draw seen with the gun from another buffer is left out");
		const std::vector<bool> all_seen = palette::keep_knife_draws(keep, { true, true, true, true }, buffers);
		expect(!all_seen[0] && !all_seen[1] && !all_seen[2] && !all_seen[3], "draws all seen with the gun are left out");
		const std::vector<bool> one_pass = palette::keep_knife_draws({ false, true, true, true }, seen, buffers);
		expect(!one_pass[0], "a draw already left out stays out");
	}
	std::printf(failures ? "palette_copy_test: %d failures\n" : "palette_copy_test: all passed\n", failures);
	return failures ? 1 : 0;
}
