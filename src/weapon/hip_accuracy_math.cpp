#include "weapon/hip_accuracy.hpp"

#include <algorithm>
#include <cmath>

namespace bfvr::weapon
{
	void blend_dispersion(const float (&hip)[dispersion_floats], const float (&zoom)[dispersion_floats], float strength,
		float (&out)[dispersion_floats])
	{
		const float t = std::isfinite(strength) ? std::clamp(strength, 0.0f, 1.0f) : 0.0f;
		for (std::size_t i = 0; i < dispersion_floats; ++i)
		{
			out[i] = hip[i] + (zoom[i] - hip[i]) * t;
		}
	}
}
