#include "stereo/lens_math.hpp"

#include <cmath>

namespace bfvr::stereo
{
	float reticle_span(float size_fraction, float ads_fov_degrees)
	{
		return size_fraction * 2.0f * std::tan(0.5f * ads_fov_degrees * 3.14159265f / 180.0f);
	}

	float reticle_fov(float override_degrees, float level_fov_degrees)
	{
		if (override_degrees > 0.0f && std::isfinite(override_degrees))
		{
			return override_degrees;
		}
		if (level_fov_degrees > 0.5f && level_fov_degrees < 170.0f)
		{
			return level_fov_degrees;
		}
		return 40.0f;
	}

	float glass_reticle_span(float span, float power)
	{
		return power >= 1.0f && std::isfinite(power) ? span * power : 0.0f;
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

	bool glass_picture_uv(const float (&along)[3], const float (&right)[3], const float (&up)[3], const float (&slope)[2], float power, float lens_tan,
		float (&uv)[2])
	{
		if (!(power >= 1.0f) || !(lens_tan > 1e-4f) || !std::isfinite(power) || !std::isfinite(lens_tan))
		{
			return false;
		}
		float way[3]{};
		for (int i = 0; i < 3; ++i)
		{
			way[i] = along[i] + (right[i] * slope[0] + up[i] * slope[1]) / power;
		}
		if (way[2] > -1e-4f)
		{
			return false;
		}
		uv[0] = 0.5f + 0.5f * (way[0] / -way[2]) / lens_tan;
		uv[1] = 0.5f - 0.5f * (way[1] / -way[2]) / lens_tan;
		const float du = uv[0] - 0.5f, dv = uv[1] - 0.5f;
		return du * du + dv * dv <= 0.25f;
	}

	float glass_picture_reach(const float (&along)[3], float power)
	{
		if (along[2] > -0.05f || !(power >= 1.0f))
		{
			return 0.0f;
		}
		const float line = std::sqrt(along[0] * along[0] + along[1] * along[1]) / -along[2];
		return 1.15f * (line + glass_field_stop_on_line / power);
	}

	unsigned choose_glass(glass_choice& kept, const std::uint64_t* area, const std::uint64_t* on_line, int draws)
	{
		const int n = draws < 0 ? 0 : (draws > glass_max_draws ? glass_max_draws : draws);
		int smallest = -1;
		unsigned bits = 0;
		std::uint64_t total = 0;
		for (int k = 0; k < n; ++k)
		{
			if (on_line[k] > 0 && area[k] > 0)
			{
				bits |= 1u << k;
				total += area[k];
				if (smallest < 0 || area[k] < area[smallest])
				{
					smallest = k;
				}
			}
		}
		if (smallest >= 0)
		{
			kept = { smallest, total, bits };
			return bits;
		}
		// A kept draw that is not drawn now (live: the ACOG's glass disc, hidden off the line) counts as no pixels.
		std::uint64_t now = 0;
		for (int k = 0; k < n; ++k)
		{
			if (kept.bits & (1u << k))
			{
				now += area[k];
			}
		}
		const bool same_optic = kept.bits != 0 && n > 0 && now > 0 && now < 2 * kept.area && 2 * now > kept.area;
		if (same_optic)
		{
			return kept.bits;
		}
		kept = {};
		return 0;
	}

	float glass_field_stop(float eye_off_line_metres)
	{
		const float off = std::isfinite(eye_off_line_metres) ? std::fabs(eye_off_line_metres) : glass_eye_box;
		return glass_field_stop_on_line * std::fmax(0.0f, 1.0f - off / glass_eye_box);
	}
}
