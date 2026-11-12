// Checks the hip_accuracy blend (src/weapon/hip_accuracy_math): the game's hip dispersion moves towards
// the zoom values with the strength, clamped, and a broken strength leaves the game's values.
#include <cmath>
#include <cstdio>
#include <limits>

#include "weapon/hip_accuracy.hpp"
#include "weapon/zoom_mesh.hpp"

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

	bool near(float a, float b)
	{
		return std::fabs(a - b) <= 1e-5f;
	}

	// AK-12, standing still (live read 2026-11-10): hip 3..7 deg, zoom 0.2..1.7 deg.
	constexpr float hip[dispersion_floats]{ 3.0f, 7.0f, 0.11f, 15.0f, 15.0f, 0.2f };
	constexpr float zoom[dispersion_floats]{ 0.2f, 1.7f, 0.11f, 9.2f, 15.0f, 0.2f };
}

int main()
{
	float out[dispersion_floats]{};
	blend_dispersion(hip, zoom, 1.0f, out);
	expect(near(out[0], 0.2f) && near(out[1], 1.7f) && near(out[3], 9.2f), "strength 1: the zoom values");
	blend_dispersion(hip, zoom, 0.0f, out);
	expect(near(out[0], 3.0f) && near(out[1], 7.0f) && near(out[3], 15.0f), "strength 0: the game's hip values");
	blend_dispersion(hip, zoom, 0.5f, out);
	expect(near(out[0], 1.6f) && near(out[1], 4.35f) && near(out[2], 0.11f), "half way, unchanged fields stay");
	blend_dispersion(hip, zoom, 3.0f, out);
	expect(near(out[0], 0.2f), "clamped above 1");
	blend_dispersion(hip, zoom, -1.0f, out);
	expect(near(out[0], 3.0f), "clamped below 0");
	blend_dispersion(hip, zoom, std::numeric_limits<float>::quiet_NaN(), out);
	expect(near(out[0], 3.0f) && near(out[1], 7.0f), "NaN: the game's values");
	// sight_zoom_mesh (#39): the zoom mesh shows from the start of the zoom while a sight is at an eye.
	expect(bfvr::weapon::zoom_mesh_factor(1.0f, true) == 0.0f, "sight at the eye: the zoom mesh at once");
	expect(bfvr::weapon::zoom_mesh_factor(0.7f, false) == 0.7f, "otherwise the data's own factor");
	if (failures)
	{
		std::printf("%d failure(s)\n", failures);
		return 1;
	}
	std::printf("hip_accuracy: all checks passed\n");
	return 0;
}
