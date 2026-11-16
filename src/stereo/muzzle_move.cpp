#include "stereo/muzzle_move.hpp"

#include <algorithm>
#include <cmath>

namespace bfvr::stereo::muzzle_move
{
	namespace
	{
		// Rotation matrix rows of a unit quaternion (x y z w).
		void matrix(const float (&q)[4], float (&m)[9])
		{
			const float x = q[0], y = q[1], z = q[2], w = q[3];
			m[0] = 1 - 2 * (y * y + z * z);
			m[1] = 2 * (x * y - z * w);
			m[2] = 2 * (x * z + y * w);
			m[3] = 2 * (x * y + z * w);
			m[4] = 1 - 2 * (x * x + z * z);
			m[5] = 2 * (y * z - x * w);
			m[6] = 2 * (x * z - y * w);
			m[7] = 2 * (y * z + x * w);
			m[8] = 1 - 2 * (x * x + y * y);
		}

		bool unit(const float (&q)[4], float (&out)[4])
		{
			const float length = std::sqrt(q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3]);
			if (!(length > 1e-6f) || !std::isfinite(length))
			{
				return false;
			}
			for (int i = 0; i < 4; ++i)
			{
				out[i] = q[i] / length;
			}
			return true;
		}

		float saturate(float value)
		{
			return std::clamp(value, 0.0f, 1.0f);
		}
	}

	bool make(const float (&game_position)[3], const float (&game_rotation)[4], const float (&drawn_position)[3], const float (&drawn_rotation)[4],
		const float (&aim_position)[3], const float (&aim_rotation)[4], float flash_along, move& out)
	{
		float g[4], d[4], a[4];
		if (!unit(game_rotation, g) || !unit(drawn_rotation, d) || !unit(aim_rotation, a))
		{
			return false;
		}
		float mg[9], md[9];
		matrix(g, mg);
		matrix(d, md);
		// R = Md * Mg^T; t = pd - R * pg.
		float r[9];
		for (int row = 0; row < 3; ++row)
		{
			for (int col = 0; col < 3; ++col)
			{
				r[row * 3 + col] = md[row * 3 + 0] * mg[col * 3 + 0] + md[row * 3 + 1] * mg[col * 3 + 1] + md[row * 3 + 2] * mg[col * 3 + 2];
			}
		}
		for (int row = 0; row < 3; ++row)
		{
			out.rows[row * 4 + 0] = r[row * 3 + 0];
			out.rows[row * 4 + 1] = r[row * 3 + 1];
			out.rows[row * 4 + 2] = r[row * 3 + 2];
			out.rows[row * 4 + 3] = drawn_position[row] - (r[row * 3 + 0] * game_position[0] + r[row * 3 + 1] * game_position[1] + r[row * 3 + 2] * game_position[2]);
		}
		float ma[9];
		matrix(a, ma);
		for (int row = 0; row < 3; ++row)
		{
			out.axis[row] = ma[row * 3 + 2];
			out.muzzle[row] = aim_position[row] + ma[row * 3 + 1] * 0.019f + ma[row * 3 + 2] * flash_along;
		}
		return true;
	}

	float weight(const move& m, const float (&point)[3], const shape& s, float fade)
	{
		const float rel[3] = { point[0] - m.muzzle[0], point[1] - m.muzzle[1], point[2] - m.muzzle[2] };
		const float along = rel[0] * m.axis[0] + rel[1] * m.axis[1] + rel[2] * m.axis[2];
		const float off[3] = { rel[0] - along * m.axis[0], rel[1] - along * m.axis[1], rel[2] - along * m.axis[2] };
		const float across = std::sqrt(off[0] * off[0] + off[1] * off[1] + off[2] * off[2]);
		const float across_span = std::max(s.across_none - s.across_full, 1e-3f);
		const float along_span = std::max(s.along_none - s.along_full, 1e-3f);
		return saturate(fade) * saturate((s.across_none - across) / across_span) * saturate((s.along_none - along) / along_span)
			* saturate((along + s.behind + 0.4f) / 0.4f);
	}

	void apply(const move& m, const float (&point)[3], float (&out)[3])
	{
		for (int row = 0; row < 3; ++row)
		{
			out[row] = m.rows[row * 4 + 0] * point[0] + m.rows[row * 4 + 1] * point[1] + m.rows[row * 4 + 2] * point[2] + m.rows[row * 4 + 3];
		}
	}
}
