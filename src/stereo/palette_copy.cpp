#include "stereo/palette_copy.hpp"

#include <cmath>

namespace bfvr::stereo::palette
{
	bool tagged(const float* entry, float tag, float tolerance)
	{
		const float length = std::sqrt(entry[0] * entry[0] + entry[1] * entry[1] + entry[2] * entry[2]);
		return std::isfinite(length) && std::fabs(length - tag) <= tolerance;
	}

	int move_tagged(float* palette, const float (&d)[12], float tag, float tolerance)
	{
		int moved = 0;
		for (int e = 0; e < entries; ++e)
		{
			float* m = palette + e * floats_per_entry;
			if (!tagged(m, tag, tolerance))
			{
				for (int i = 0; i < floats_per_entry; ++i)
				{
					m[i] = 0.0f;
				}
				continue;
			}
			float out[12];
			multiply(d, m, out);
			for (int i = 0; i < floats_per_entry; ++i)
			{
				m[i] = out[i];
			}
			++moved;
		}
		return moved;
	}

	void multiply(const float (&d)[12], const float* entry, float* out)
	{
		// [R' | t'] = [Rd | td] * [R | t]: R' = Rd R, t' = Rd t + td.
		for (int row = 0; row < 3; ++row)
		{
			for (int column = 0; column < 4; ++column)
			{
				float sum = column == 3 ? d[row * 4 + 3] : 0.0f;
				for (int k = 0; k < 3; ++k)
				{
					sum += d[row * 4 + k] * entry[k * 4 + column];
				}
				out[row * 4 + column] = sum;
			}
		}
	}

	void relative_to_root(float* entry, float tag, const float (&root)[3])
	{
		const float inverse = tag != 0.0f ? 1.0f / tag : 1.0f;
		for (int row = 0; row < 3; ++row)
		{
			float* r = entry + row * 4;
			r[0] *= inverse;
			r[1] *= inverse;
			r[2] *= inverse;
			r[3] = (r[3] - root[row]) * inverse;
		}
	}

	std::vector<bool> keep_knife_draws(const std::vector<bool>& keep, const std::vector<bool>& seen_with_gun, const std::vector<const void*>& buffers)
	{
		const std::size_t count = keep.size();
		std::vector<bool> kept(count, false);
		if (seen_with_gun.size() != count || buffers.size() != count)
		{
			return kept;
		}
		for (std::size_t i = 0; i < count; ++i)
		{
			bool own_buffer = false;
			for (std::size_t j = 0; j < count; ++j)
			{
				own_buffer = own_buffer || (j != i && keep[j] && !seen_with_gun[j] && buffers[j] == buffers[i]);
			}
			kept[i] = keep[i] && (!seen_with_gun[i] || own_buffer);
		}
		return kept;
	}
}
