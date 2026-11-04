// Checks the fixed-foveation shading-rate map (src/stereo/foveation_pattern): tile counts, full rate in the
// middle, coarser rates outward, symmetry, the levels and their coverage.
#include <cstdio>

#include "stereo/foveation_pattern.hpp"

using namespace bfvr::stereo::foveation_pattern;

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
}

int main()
{
	expect(!level("off").enabled && !level("nonsense").enabled, "off and unknown: no foveation");
	expect(level("light").inner > level("medium").inner && level("medium").inner > level("strong").inner, "levels get stronger");

	std::uint32_t tx{}, ty{};
	const std::vector<std::uint8_t> off = make(2160, 2160, level("off"), tx, ty);
	expect(tx == 135 && ty == 135 && off.size() == 135u * 135u, "2160 px = 135 tiles of 16");
	bool all_full = true;
	for (const std::uint8_t r : off)
	{
		all_full = all_full && r == 0;
	}
	expect(all_full, "off: every tile full rate");

	const rings medium = level("medium");
	const std::vector<std::uint8_t> map = make(2160, 2160, medium, tx, ty);
	const auto at = [&](std::uint32_t x, std::uint32_t y) { return map[static_cast<std::size_t>(y) * tx + x]; };
	expect(at(67, 67) == 0 && at(60, 70) == 0, "middle at full rate");
	expect(at(0, 0) == 2 && at(134, 134) == 2, "corners 4x4");
	expect(at(67, 0) >= 1 && at(0, 67) >= 1, "edges coarser than the middle");
	bool symmetric = true;
	bool monotonic = true;
	for (std::uint32_t y = 0; y < ty; ++y)
	{
		for (std::uint32_t x = 0; x < tx; ++x)
		{
			symmetric = symmetric && at(x, y) == at(tx - 1 - x, y) && at(x, y) == at(x, ty - 1 - y);
			if (x + 1 < tx / 2 + 1 && x < tx / 2)
			{
				monotonic = monotonic && at(x, y) >= at(x + 1, y);
			}
		}
	}
	expect(symmetric, "map symmetric about the middle");
	expect(monotonic, "rate never finer further out");

	// Partial tiles at the edge: 1080 px = 67.5 -> 68 tiles.
	make(1080, 1080, medium, tx, ty);
	expect(tx == 68 && ty == 68, "half-size target: partial edge tiles");

	float light[3], strong[3];
	coverage(level("light"), light);
	coverage(level("strong"), strong);
	expect(light[0] > 0.45f && light[0] < 0.6f, "light keeps about half the image at full rate");
	expect(strong[0] < light[0] && strong[2] > light[2], "strong shades less at full rate");
	expect(light[0] + light[1] + light[2] > 0.999f && light[0] + light[1] + light[2] < 1.001f, "coverage sums to 1");

	if (failures)
	{
		std::printf("%d failure(s)\n", failures);
		return 1;
	}
	std::printf("foveation tests passed\n");
	return 0;
}
