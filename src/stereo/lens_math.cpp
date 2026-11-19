#include "stereo/lens_math.hpp"

#include <cmath>

namespace bfvr::stereo
{
	float reticle_span(float size_fraction, float ads_fov_degrees)
	{
		return size_fraction * 2.0f * std::tan(0.5f * ads_fov_degrees * 3.14159265f / 180.0f);
	}

	bool lens_axes(const float (&along)[3], const float (&up)[3], float (&right_out)[3], float (&up_out)[3])
	{
		const float right[3]{ along[1] * up[2] - along[2] * up[1], along[2] * up[0] - along[0] * up[2], along[0] * up[1] - along[1] * up[0] };
		const float length = std::sqrt(right[0] * right[0] + right[1] * right[1] + right[2] * right[2]);
		if (length < 1e-4f)
		{
			return false;
		}
		for (int i = 0; i < 3; ++i)
		{
			right_out[i] = right[i] / length;
		}
		up_out[0] = right_out[1] * along[2] - right_out[2] * along[1];
		up_out[1] = right_out[2] * along[0] - right_out[0] * along[2];
		up_out[2] = right_out[0] * along[1] - right_out[1] * along[0];
		return true;
	}

	float glass_picture_scale(float power, float lens_tan)
	{
		// The disc edge (uv 0.5 from the centre) is lens_tan off the line; the glass shows the world at slope / power.
		if (!(power >= 1.0f) || !(lens_tan > 1e-4f) || !std::isfinite(lens_tan) || !std::isfinite(power))
		{
			return 0.0f;
		}
		return 0.5f / (power * lens_tan);
	}

	float glass_field_stop(float eye_off_line_metres)
	{
		const float off = std::isfinite(eye_off_line_metres) ? std::fabs(eye_off_line_metres) : glass_eye_box;
		return glass_field_stop_on_line * std::fmax(0.0f, 1.0f - off / glass_eye_box);
	}
}
