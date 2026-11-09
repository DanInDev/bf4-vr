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
	std::printf(failures ? "palette_copy_test: %d failures\n" : "palette_copy_test: all passed\n", failures);
	return failures ? 1 : 0;
}
