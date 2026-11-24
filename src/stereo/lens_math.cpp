#include "stereo/lens_math.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace bfvr::stereo
{
	float reticle_span(float size_fraction, float ads_fov_degrees)
	{
		return size_fraction * 2.0f * std::tan(0.5f * ads_fov_degrees * 3.14159265f / 180.0f);
	}

	float reticle_fov(float override_degrees, float level_fov_degrees)
	{
		if (override_degrees >= 10.0f && override_degrees < 170.0f)
		{
			return override_degrees;
		}
		if (level_fov_degrees > 0.5f && level_fov_degrees < 170.0f)
		{
			return level_fov_degrees;
		}
		return 40.0f;
	}

	float reticle_level_fov(const float* level_fov, int levels, int capacity, int selected, bool magnifier)
	{
		const int level = magnifier ? selected - 1 : selected;
		if (!level_fov || level < 1 || selected >= levels || selected >= capacity)
		{
			return 0.0f;
		}
		const float fov = level_fov[level];
		return fov > 0.5f && fov < 170.0f ? fov : 0.0f;
	}

	float glass_reticle_span(float span, float power)
	{
		return power >= 1.0f && std::isfinite(power) ? span * power : 0.0f;
	}

	reticle_material read_reticle_material(const float (&block)[20])
	{
		reticle_material art;
		const float size = block[8];
		const float aspect = block[12];
		const float u = block[16], v = block[17];
		const auto finite = [](float x) { return std::isfinite(x); };
		// The sight line crosses the art (the ACOG also draws a full-height quad anchored at its corner, 1 1).
		art.known = finite(size) && finite(aspect) && finite(u) && finite(v) && size > 0.001f && size <= 4.0f && aspect >= 0.1f && aspect <= 10.0f && u > 0.01f
			&& u < 0.99f && v > 0.01f && v < 0.99f;
		if (art.known)
		{
			art.size = size;
			art.aspect = aspect;
			art.anchor[0] = u;
			art.anchor[1] = v;
		}
		else if (finite(size) && size > 0.001f && size < 0.5f)
		{
			art.size = size;
		}
		return art;
	}

	float half_to_float(std::uint16_t half)
	{
		const int sign = (half >> 15) & 1;
		const int exponent = (half >> 10) & 0x1f;
		const int mantissa = half & 0x3ff;
		float value = 0.0f;
		if (exponent == 0)
		{
			value = std::ldexp(static_cast<float>(mantissa), -24);
		}
		else if (exponent == 31)
		{
			value = mantissa ? std::numeric_limits<float>::quiet_NaN() : std::numeric_limits<float>::infinity();
		}
		else
		{
			value = std::ldexp(1.0f + static_cast<float>(mantissa) / 1024.0f, exponent - 15);
		}
		return sign ? -value : value;
	}

	bool scope_art_from_quad(const float (&position)[4][2], const float (&uv)[4][2], reticle_material& art)
	{
		float low[2]{ position[0][0], position[0][1] }, high[2]{ low[0], low[1] };
		for (const auto& p : position)
		{
			for (int axis = 0; axis < 2; ++axis)
			{
				if (!std::isfinite(p[axis]))
				{
					return false;
				}
				low[axis] = (std::min)(low[axis], p[axis]);
				high[axis] = (std::max)(high[axis], p[axis]);
			}
		}
		const float width = high[0] - low[0], height = high[1] - low[1];
		if (width < 0.002f || height < 0.002f || width > 0.2f || height > 0.2f || low[0] > 0.0f || high[0] < 0.0f || low[1] > 0.0f || high[1] < 0.0f)
		{
			return false;
		}
		// uv at the origin, from how uv runs along x and along y over the corners (an axis-aligned quad).
		float origin_uv[2]{};
		for (int axis = 0; axis < 2; ++axis)
		{
			int at_low = -1, at_high = -1;
			for (int k = 0; k < 4; ++k)
			{
				if (std::fabs(position[k][axis] - low[axis]) < 1e-4f && at_low < 0)
				{
					at_low = k;
				}
				if (std::fabs(position[k][axis] - high[axis]) < 1e-4f && at_high < 0)
				{
					at_high = k;
				}
			}
			if (at_low < 0 || at_high < 0)
			{
				return false;
			}
			// The texture coordinate that changes along this axis (u along x, v along y).
			const float from = uv[at_low][axis], to = uv[at_high][axis];
			if (std::fabs(to - from) < 0.25f)
			{
				return false;
			}
			const float t = (0.0f - low[axis]) / (high[axis] - low[axis]);
			origin_uv[axis] = from + t * (to - from);
		}
		art.size = 0.0f; // no material size: scope_art_span
		art.aspect = width / height;
		art.anchor[0] = origin_uv[0];
		art.anchor[1] = origin_uv[1];
		art.known = false;
		// The line crosses the art, not its corner or edge (the ACOG also draws a quad with its origin at a corner).
		constexpr float margin = 0.01f;
		return origin_uv[0] > margin && origin_uv[0] < 1.0f - margin && origin_uv[1] > margin && origin_uv[1] < 1.0f - margin;
	}

	float scope_art_span(float aspect, float level_fov_degrees)
	{
		if (!(aspect > 0.0f) || !(level_fov_degrees > 0.5f) || level_fov_degrees >= 170.0f)
		{
			return 0.0f;
		}
		return scope_art_width_of_screen * 2.0f * std::tan(0.5f * level_fov_degrees * 3.14159265f / 180.0f) / aspect;
	}

	bool reticle_art_uv(const reticle_material& art, float span, const float (&slope)[2], float (&uv)[2])
	{
		if (!(span > 0.0f) || !(art.aspect > 0.0f))
		{
			return false;
		}
		uv[0] = art.anchor[0] + slope[0] / (span * art.aspect);
		uv[1] = art.anchor[1] - slope[1] / span;
		return uv[0] >= 0.0f && uv[0] <= 1.0f && uv[1] >= 0.0f && uv[1] <= 1.0f;
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
		const float identity[4]{ 0.0f, 0.0f, 0.0f, 1.0f };
		return glass_picture_uv(along, right, up, slope, power, lens_tan, identity, uv);
	}

	bool glass_picture_uv(const float (&along)[3], const float (&right)[3], const float (&up)[3], const float (&slope)[2], float power, float lens_tan,
		const float (&picture)[4], float (&uv)[2])
	{
		if (!(power >= 1.0f) || !(lens_tan > 1e-4f) || !std::isfinite(power) || !std::isfinite(lens_tan))
		{
			return false;
		}
		float eye_way[3]{};
		for (int i = 0; i < 3; ++i)
		{
			eye_way[i] = along[i] + (right[i] * slope[0] + up[i] * slope[1]) / power;
		}
		// Into the picture's axes: rotate by the conjugate of its orientation (v' = v + 2w (u x v) + 2 u x (u x v), u = -xyz).
		const float u[3]{ -picture[0], -picture[1], -picture[2] }, w = picture[3];
		const float t[3]{ u[1] * eye_way[2] - u[2] * eye_way[1], u[2] * eye_way[0] - u[0] * eye_way[2], u[0] * eye_way[1] - u[1] * eye_way[0] };
		const float tt[3]{ u[1] * t[2] - u[2] * t[1], u[2] * t[0] - u[0] * t[2], u[0] * t[1] - u[1] * t[0] };
		float way[3]{};
		for (int i = 0; i < 3; ++i)
		{
			way[i] = eye_way[i] + 2.0f * w * t[i] + 2.0f * tt[i];
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

	float glass_field_stop(float eye_off_line_metres, float eye_box)
	{
		const float box = std::isfinite(eye_box) && eye_box > 0.001f ? eye_box : glass_eye_box;
		const float fade = box * glass_eye_box_fade;
		const float off = std::isfinite(eye_off_line_metres) ? std::fabs(eye_off_line_metres) : box + fade;
		if (off <= box)
		{
			return glass_field_stop_on_line;
		}
		const float t = std::fmin(1.0f, (off - box) / fade);
		return glass_field_stop_on_line * (1.0f - t * t * (3.0f - 2.0f * t));
	}
}
