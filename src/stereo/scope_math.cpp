#include "stereo/scope_math.hpp"

#include <cmath>

namespace bfvr::stereo::scope_math
{
	namespace
	{
		struct v3
		{
			float x{}, y{}, z{};
		};

		v3 operator+(v3 a, v3 b) { return { a.x + b.x, a.y + b.y, a.z + b.z }; }
		v3 operator-(v3 a, v3 b) { return { a.x - b.x, a.y - b.y, a.z - b.z }; }
		v3 operator*(v3 a, float s) { return { a.x * s, a.y * s, a.z * s }; }
		float dot(v3 a, v3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
		v3 cross(v3 a, v3 b) { return { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x }; }
		float length(v3 a) { return std::sqrt(dot(a, a)); }
		v3 normalized(v3 a)
		{
			const float l = length(a);
			return l > 1e-8f ? a * (1.0f / l) : v3{};
		}

		struct quat
		{
			float x{}, y{}, z{}, w{ 1 };
		};

		quat from(const float (&q)[4]) { return { q[0], q[1], q[2], q[3] }; }
		quat conjugate(quat q) { return { -q.x, -q.y, -q.z, q.w }; }
		quat operator*(quat a, quat b)
		{
			return { a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y, a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
				a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w, a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z };
		}
		v3 rotate(quat q, v3 v)
		{
			const v3 u{ q.x, q.y, q.z };
			const v3 t = cross(u, v) * 2.0f;
			return v + t * q.w + cross(u, t);
		}

		// Rotation whose x, y, z axes are the given orthonormal vectors.
		quat from_axes(v3 x, v3 y, v3 z)
		{
			const float trace = x.x + y.y + z.z;
			quat q;
			if (trace > 0.0f)
			{
				const float s = std::sqrt(trace + 1.0f) * 2.0f;
				q = { (y.z - z.y) / s, (z.x - x.z) / s, (x.y - y.x) / s, 0.25f * s };
			}
			else if (x.x > y.y && x.x > z.z)
			{
				const float s = std::sqrt(1.0f + x.x - y.y - z.z) * 2.0f;
				q = { 0.25f * s, (y.x + x.y) / s, (z.x + x.z) / s, (y.z - z.y) / s };
			}
			else if (y.y > z.z)
			{
				const float s = std::sqrt(1.0f + y.y - x.x - z.z) * 2.0f;
				q = { (y.x + x.y) / s, 0.25f * s, (z.y + y.z) / s, (z.x - x.z) / s };
			}
			else
			{
				const float s = std::sqrt(1.0f + z.z - x.x - y.y) * 2.0f;
				q = { (z.x + x.z) / s, (z.y + y.z) / s, 0.25f * s, (x.y - y.x) / s };
			}
			const float n = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
			return { q.x / n, q.y / n, q.z / n, q.w / n };
		}

		void store(const quat& q, float (&out)[4])
		{
			out[0] = q.x;
			out[1] = q.y;
			out[2] = q.z;
			out[3] = q.w;
		}

		v3 position(const pose& p) { return { p.position[0], p.position[1], p.position[2] }; }
	}

	float magnification(float hip_fov_y, float aim_fov_y)
	{
		constexpr float low = 0.01f, high = 3.1f;
		if (!(hip_fov_y > low && hip_fov_y < high && aim_fov_y > low && aim_fov_y < high) || aim_fov_y >= hip_fov_y)
		{
			return 1.0f;
		}
		return std::tan(0.5f * hip_fov_y) / std::tan(0.5f * aim_fov_y);
	}

	plan plan_scope(const pose& eye, const float (&sight_eye)[3], const float (&rotation)[4], const float (&sight)[3], float magnification, const lens& optic)
	{
		plan result;
		const quat root = from(rotation);
		const v3 forward = normalized(rotate(root, { sight[0], sight[1], sight[2] }));
		const v3 back = forward * -1.0f;
		const v3 right = normalized(cross(rotate(root, { 0, 1, 0 }), back));
		if (length(forward) < 0.5f || length(right) < 0.5f || !(magnification >= 1.0f) || optic.radius <= 0.0f || optic.distance <= 0.0f
			|| !(optic.crop > 0.0f && optic.crop <= 1.0f))
		{
			return result;
		}
		const v3 up = cross(back, right);
		const v3 origin{ sight_eye[0], sight_eye[1], sight_eye[2] };
		quat orientation = from_axes(right, up, back);
		const v3 centre = origin + forward * optic.distance;
		const float distance = std::fmax(0.02f, length(centre - position(eye)));
		const float tan_lens = optic.radius / distance / magnification;
		result.lens_fov_y = 2.0f * std::atan(tan_lens);
		result.render_fov_y = 2.0f * std::atan(tan_lens / optic.crop);
		result.camera.position[0] = origin.x;
		result.camera.position[1] = origin.y;
		result.camera.position[2] = origin.z;
		store(orientation, result.camera.orientation);
		result.quad.position[0] = centre.x;
		result.quad.position[1] = centre.y;
		result.quad.position[2] = centre.z;
		store(orientation, result.quad.orientation);
		result.quad_size = 2.0f * optic.radius;
		result.valid = std::isfinite(result.render_fov_y) && result.render_fov_y > 0.0f && result.render_fov_y < 3.0f;
		return result;
	}

	pose relative_pose(const pose& from_pose, const pose& to)
	{
		const quat inverse = conjugate(from(from_pose.orientation));
		const v3 offset = rotate(inverse, position(to) - position(from_pose));
		pose result;
		result.position[0] = offset.x;
		result.position[1] = offset.y;
		result.position[2] = offset.z;
		store(inverse * from(to.orientation), result.orientation);
		return result;
	}

	void compose_rows(const float (&camera)[16], const pose& relative, float (&out)[16])
	{
		const v3 rows[3] = { { camera[0], camera[1], camera[2] }, { camera[4], camera[5], camera[6] }, { camera[8], camera[9], camera[10] } };
		const auto to_world = [&](v3 local) { return rows[0] * local.x + rows[1] * local.y + rows[2] * local.z; };
		const quat q = from(relative.orientation);
		const v3 axes[3] = { to_world(rotate(q, { 1, 0, 0 })), to_world(rotate(q, { 0, 1, 0 })), to_world(rotate(q, { 0, 0, 1 })) };
		const v3 p = v3{ camera[12], camera[13], camera[14] } + to_world(position(relative));
		for (int r = 0; r < 3; ++r)
		{
			out[r * 4 + 0] = axes[r].x;
			out[r * 4 + 1] = axes[r].y;
			out[r * 4 + 2] = axes[r].z;
			out[r * 4 + 3] = camera[r * 4 + 3];
		}
		out[12] = p.x;
		out[13] = p.y;
		out[14] = p.z;
		out[15] = camera[15];
	}
}
