#include "stereo/light_rebase.hpp"

#include <algorithm>
#include <cmath>

namespace bfvr::stereo::light_rebase
{
	void rebase(const float (&light)[16], const float (&from)[16], const float (&to)[16], float (&out)[16])
	{
		// inverse(from): the transposed rotation over the first row's squared length, the position taken back.
		const float length = from[0] * from[0] + from[1] * from[1] + from[2] * from[2];
		const float scale = length > 0.0f ? 1.0f / length : 0.0f;
		float inverse[16]{};
		for (int row = 0; row < 3; ++row)
		{
			for (int column = 0; column < 3; ++column)
			{
				inverse[row * 4 + column] = from[column * 4 + row] * scale;
			}
		}
		for (int column = 0; column < 3; ++column)
		{
			const float* axis = &from[column * 4];
			inverse[12 + column] = -(from[12] * axis[0] + from[13] * axis[1] + from[14] * axis[2]) * scale;
		}
		inverse[15] = 1.0f;
		// The light's rows in from's frame: directions (w 0) and the position (w 1).
		float local[16]{};
		for (int row = 0; row < 4; ++row)
		{
			const float w = row == 3 ? 1.0f : 0.0f;
			for (int column = 0; column < 4; ++column)
			{
				float sum = w * inverse[12 + column];
				for (int k = 0; k < 3; ++k)
				{
					sum += light[row * 4 + k] * inverse[k * 4 + column];
				}
				local[row * 4 + column] = sum;
			}
			local[row * 4 + 3] = w;
		}
		for (int row = 0; row < 4; ++row)
		{
			for (int column = 0; column < 4; ++column)
			{
				float sum = 0.0f;
				for (int k = 0; k < 4; ++k)
				{
					sum += local[row * 4 + k] * to[k * 4 + column];
				}
				out[row * 4 + column] = sum;
			}
		}
	}

	float rotation_difference(const float (&a)[16], const float (&b)[16])
	{
		float largest = 0.0f;
		for (int i = 0; i < 12; ++i)
		{
			if (i % 4 != 3)
			{
				largest = std::max(largest, std::fabs(a[i] - b[i]));
			}
		}
		return largest;
	}

	float position_difference(const float (&a)[16], const float (&b)[16])
	{
		const float x = a[12] - b[12], y = a[13] - b[13], z = a[14] - b[14];
		return std::sqrt(x * x + y * y + z * z);
	}

	float forward_angle_degrees(const float (&a)[16], const float (&b)[16])
	{
		const float dot = a[8] * b[8] + a[9] * b[9] + a[10] * b[10];
		const float lengths = std::sqrt((a[8] * a[8] + a[9] * a[9] + a[10] * a[10]) * (b[8] * b[8] + b[9] * b[9] + b[10] * b[10]));
		if (lengths <= 0.0f)
		{
			return 0.0f;
		}
		return std::acos(std::clamp(dot / lengths, -1.0f, 1.0f)) * 57.2957795f;
	}
}
