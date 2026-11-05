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
			// [R' | t'] = [Rd | td] * [R | t]: R' = Rd R, t' = Rd t + td.
			float out[12];
			for (int row = 0; row < 3; ++row)
			{
				for (int column = 0; column < 4; ++column)
				{
					float sum = column == 3 ? d[row * 4 + 3] : 0.0f;
					for (int k = 0; k < 3; ++k)
					{
						sum += d[row * 4 + k] * m[k * 4 + column];
					}
					out[row * 4 + column] = sum;
				}
			}
			for (int i = 0; i < floats_per_entry; ++i)
			{
				m[i] = out[i];
			}
			++moved;
		}
		return moved;
	}
}
