#include "stereo/hud_layout.hpp"

#include <cmath>

namespace bfvr::stereo::hud_layout
{
	void rotate(const float (&q)[4], const float (&v)[3], float (&out)[3])
	{
		// v' = v + 2w (q x v) + 2 q x (q x v)
		const float qx = q[0], qy = q[1], qz = q[2], qw = q[3];
		const float tx = 2 * (qy * v[2] - qz * v[1]);
		const float ty = 2 * (qz * v[0] - qx * v[2]);
		const float tz = 2 * (qx * v[1] - qy * v[0]);
		out[0] = v[0] + qw * tx + (qy * tz - qz * ty);
		out[1] = v[1] + qw * ty + (qz * tx - qx * tz);
		out[2] = v[2] + qw * tz + (qx * ty - qy * tx);
	}

	quad projected(const pose& view, const fov& rendered, float distance, const uv_rect& rect)
	{
		const float left = distance * std::tan(rendered.left);
		const float right = distance * std::tan(rendered.right);
		const float up = distance * std::tan(rendered.up);
		const float down = distance * std::tan(rendered.down);
		const float x = left + 0.5f * (rect.u0 + rect.u1) * (right - left);
		const float y = up - 0.5f * (rect.v0 + rect.v1) * (up - down);
		const float local[3] = { x, y, -distance };
		float offset[3];
		rotate(view.orientation, local, offset);
		quad result;
		result.placement = view;
		for (int axis = 0; axis < 3; ++axis)
		{
			result.placement.position[axis] += offset[axis];
		}
		result.width = (rect.u1 - rect.u0) * (right - left);
		result.height = (rect.v1 - rect.v0) * (up - down);
		result.rect = rect;
		return result;
	}

	pose facing(const float (&position)[3], const float (&target)[3])
	{
		float z[3] = { target[0] - position[0], target[1] - position[1], target[2] - position[2] };
		float length = std::sqrt(z[0] * z[0] + z[1] * z[1] + z[2] * z[2]);
		pose result;
		for (int axis = 0; axis < 3; ++axis)
		{
			result.position[axis] = position[axis];
		}
		if (length < 1e-6f)
		{
			return result;
		}
		for (float& c : z)
		{
			c /= length;
		}
		// x = up x z (horizontal); straight up/down falls back to world x.
		float x[3] = { z[2], 0.0f, -z[0] };
		length = std::sqrt(x[0] * x[0] + x[2] * x[2]);
		if (length < 1e-4f)
		{
			x[0] = 1.0f;
			x[2] = 0.0f;
			length = 1.0f;
		}
		x[0] /= length;
		x[2] /= length;
		const float y[3] = { z[1] * x[2] - z[2] * x[1], z[2] * x[0] - z[0] * x[2], z[0] * x[1] - z[1] * x[0] };
		// Rotation matrix with columns x, y, z to quaternion.
		const float m00 = x[0], m11 = y[1], m22 = z[2];
		const float trace = m00 + m11 + m22;
		float qx, qy, qz, qw;
		if (trace > 0.0f)
		{
			const float s = 2.0f * std::sqrt(trace + 1.0f);
			qw = 0.25f * s;
			qx = (y[2] - z[1]) / s;
			qy = (z[0] - x[2]) / s;
			qz = (x[1] - y[0]) / s;
		}
		else if (m00 > m11 && m00 > m22)
		{
			const float s = 2.0f * std::sqrt(1.0f + m00 - m11 - m22);
			qw = (y[2] - z[1]) / s;
			qx = 0.25f * s;
			qy = (y[0] + x[1]) / s;
			qz = (z[0] + x[2]) / s;
		}
		else if (m11 > m22)
		{
			const float s = 2.0f * std::sqrt(1.0f + m11 - m00 - m22);
			qw = (z[0] - x[2]) / s;
			qx = (y[0] + x[1]) / s;
			qy = 0.25f * s;
			qz = (z[1] + y[2]) / s;
		}
		else
		{
			const float s = 2.0f * std::sqrt(1.0f + m22 - m00 - m11);
			qw = (x[1] - y[0]) / s;
			qx = (z[0] + x[2]) / s;
			qy = (z[1] + y[2]) / s;
			qz = 0.25f * s;
		}
		result.orientation[0] = qx;
		result.orientation[1] = qy;
		result.orientation[2] = qz;
		result.orientation[3] = qw;
		return result;
	}

	quad on_body(const pose& anchor, const float (&offset)[3], const float (&eye)[3], float width, const uv_rect& rect, float texture_aspect)
	{
		float moved[3];
		rotate(anchor.orientation, offset, moved);
		const float position[3] = { anchor.position[0] + moved[0], anchor.position[1] + moved[1], anchor.position[2] + moved[2] };
		quad result;
		result.placement = facing(position, eye);
		result.width = width;
		const float pixels_wide = (rect.u1 - rect.u0) * texture_aspect;
		const float pixels_high = rect.v1 - rect.v0;
		result.height = pixels_wide > 0.0f ? width * pixels_high / pixels_wide : 0.0f;
		result.rect = rect;
		return result;
	}
}
