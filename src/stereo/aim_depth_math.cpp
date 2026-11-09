#include "stereo/aim_depth_math.hpp"

#include <cmath>

namespace bfvr::stereo::aim_depth_math
{
	namespace
	{
		// v rotated by the conjugate of q (into the eye's axes).
		void rotate_into(const float (&q)[4], const float (&v)[3], float (&out)[3])
		{
			const float x = -q[0], y = -q[1], z = -q[2], w = q[3];
			// t = 2 * cross(q.xyz, v); out = v + w * t + cross(q.xyz, t)
			const float tx = 2.0f * (y * v[2] - z * v[1]);
			const float ty = 2.0f * (z * v[0] - x * v[2]);
			const float tz = 2.0f * (x * v[1] - y * v[0]);
			out[0] = v[0] + w * tx + (y * tz - z * ty);
			out[1] = v[1] + w * ty + (z * tx - x * tz);
			out[2] = v[2] + w * tz + (x * ty - y * tx);
		}
	}

	sample_set make_samples(const float (&start)[3], const float (&direction)[3], const float (&eye_position)[3], const float (&eye_orientation)[4],
		const projection& p)
	{
		sample_set set;
		const float ratio = last_distance / first_distance;
		for (int i = 0; i < samples; ++i)
		{
			const float t = first_distance * std::pow(ratio, static_cast<float>(i) / static_cast<float>(samples - 1));
			set.along[i] = t;
			const float world[3]{ start[0] + direction[0] * t - eye_position[0], start[1] + direction[1] * t - eye_position[1],
				start[2] + direction[2] * t - eye_position[2] };
			float eye[3];
			rotate_into(eye_orientation, world, eye);
			const float ahead = -eye[2];
			set.view_depth[i] = ahead;
			if (!(ahead > 0.05f) || !std::isfinite(ahead))
			{
				continue;
			}
			const float x = p.sx * eye[0] / ahead;
			const float y = p.sy * eye[1] / ahead;
			if (x < -1.0f || x > 1.0f || y < -1.0f || y > 1.0f)
			{
				continue;
			}
			set.texel[i][0] = std::fmin((x * 0.5f + 0.5f) * p.width, p.width - 1.0f);
			set.texel[i][1] = std::fmin((0.5f - y * 0.5f) * p.height, p.height - 1.0f);
			set.on_screen[i] = true;
		}
		return set;
	}

	float scene_distance(float depth, const projection& p)
	{
		const float d = p.b / (depth + p.a);
		return std::isfinite(d) && d > 0.0f ? d : -1.0f;
	}

	float hit_distance(const sample_set& set, const float (&depths)[samples], const projection& p)
	{
		bool have_previous = false;
		float previous_gap = 0.0f;
		float previous_along = 0.0f;
		for (int i = 0; i < samples; ++i)
		{
			if (!set.on_screen[i])
			{
				have_previous = false;
				continue;
			}
			const float scene = scene_distance(depths[i], p);
			if (scene < min_scene_distance)
			{
				have_previous = false; // the gun or an arm covers this pixel: no information
				continue;
			}
			// Positive: the point is behind the visible surface. Far behind it, something nearer hides the line
			// (a pillar between the eye and the line): no hit there, and no bracket across it.
			const float gap = set.view_depth[i] - scene;
			if (gap > 0.3f + 0.1f * scene)
			{
				have_previous = false;
				continue;
			}
			if (gap >= 0.0f)
			{
				if (!have_previous)
				{
					return set.along[i];
				}
				const float f = previous_gap / (previous_gap - gap); // previous_gap < 0 <= gap
				return previous_along + (set.along[i] - previous_along) * f;
			}
			have_previous = true;
			previous_gap = gap;
			previous_along = set.along[i];
		}
		return -1.0f;
	}
}
