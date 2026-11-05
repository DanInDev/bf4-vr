#include "weapon/shot_frame.hpp"

#include <cmath>

namespace bfvr::weapon
{
	namespace
	{
		struct v3
		{
			float x, y, z;
		};
		v3 operator+(v3 a, v3 b) { return { a.x + b.x, a.y + b.y, a.z + b.z }; }
		v3 operator*(v3 a, float s) { return { a.x * s, a.y * s, a.z * s }; }
		float dot(v3 a, v3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
		v3 cross(v3 a, v3 b) { return { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x }; }
		float length(v3 a) { return std::sqrt(dot(a, a)); }
		v3 normalize(v3 a)
		{
			const float l = length(a);
			return l > 1e-8f ? a * (1.0f / l) : v3{ 0, 0, 0 };
		}

		// LOCAL axes in the world for a level camera of yaw c (camera_hook level_rows: right (-k, 0, s),
		// up (0, 1, 0), back (-s, 0, -k)).
		v3 to_world(float c, const float (&local)[3])
		{
			const float s = std::sin(c), k = std::cos(c);
			return v3{ -k, 0, s } * local[0] + v3{ 0, 1, 0 } * local[1] + v3{ -s, 0, -k } * local[2];
		}

		// Rodrigues: v turned by the shortest rotation that takes unit a to unit b.
		v3 turn(v3 v, v3 a, v3 b)
		{
			const v3 axis = cross(a, b);
			const float s = length(axis);
			const float c = dot(a, b);
			if (s < 1e-6f)
			{
				if (c > 0.0f)
				{
					return v;
				}
				// Opposite: half a turn about any axis across a.
				const v3 any = normalize(std::fabs(a.y) < 0.9f ? cross(a, v3{ 0, 1, 0 }) : cross(a, v3{ 1, 0, 0 }));
				return any * (2.0f * dot(any, v)) + v * -1.0f;
			}
			const v3 k = axis * (1.0f / s);
			return v * c + cross(k, v) * s + k * (dot(k, v) * (1.0f - c));
		}
	}

	void shot_frame_at_gun(const float (&game)[16], float body_yaw, float lift, const shot_line_local& line, float max_reach, float (&out)[16])
	{
		for (int i = 0; i < 16; ++i)
		{
			out[i] = game[i];
		}
		const v3 forward = normalize({ game[8], game[9], game[10] });
		const v3 wanted = normalize(to_world(body_yaw, line.direction));
		if (length(forward) < 0.5f || length(wanted) < 0.5f || !std::isfinite(body_yaw))
		{
			return;
		}
		for (int row = 0; row < 3; ++row)
		{
			const v3 r = turn({ game[row * 4], game[row * 4 + 1], game[row * 4 + 2] }, forward, wanted);
			out[row * 4] = r.x;
			out[row * 4 + 1] = r.y;
			out[row * 4 + 2] = r.z;
		}
		v3 offset = to_world(body_yaw, line.start) + v3{ 0, std::isfinite(lift) ? lift : 0.0f, 0 };
		if (const float reach = length(offset); max_reach > 0.0f && reach > max_reach)
		{
			offset = offset * (max_reach / reach);
		}
		out[12] = game[12] + offset.x;
		out[13] = game[13] + offset.y;
		out[14] = game[14] + offset.z;
	}
}
