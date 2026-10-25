#include "stereo/eye_math.hpp"

#include <cmath>

namespace bfvr::stereo::eye_math
{
	void compose_pose(float* camera, const pose& eye)
	{
		const float qx = eye.orientation[0], qy = eye.orientation[1], qz = eye.orientation[2], qw = eye.orientation[3];
		// Column-vector rotation matrix of the quaternion.
		const float m[3][3] = {
			{ 1 - 2 * (qy * qy + qz * qz), 2 * (qx * qy - qz * qw), 2 * (qx * qz + qy * qw) },
			{ 2 * (qx * qy + qz * qw), 1 - 2 * (qx * qx + qz * qz), 2 * (qy * qz - qx * qw) },
			{ 2 * (qx * qz - qy * qw), 2 * (qy * qz + qx * qw), 1 - 2 * (qx * qx + qy * qy) },
		};

		float result[4][3]{};
		// Rotation rows: row i of the eye transform (column i of m) times the camera rotation.
		for (int i = 0; i < 3; ++i)
		{
			for (int k = 0; k < 3; ++k)
			{
				for (int axis = 0; axis < 3; ++axis)
				{
					result[i][axis] += m[k][i] * camera[k * 4 + axis];
				}
			}
		}
		// Position: eye offset expressed in camera axes, plus the camera position.
		for (int axis = 0; axis < 3; ++axis)
		{
			result[3][axis] = camera[12 + axis];
			for (int k = 0; k < 3; ++k)
			{
				result[3][axis] += eye.position[k] * camera[k * 4 + axis];
			}
		}
		for (int row = 0; row < 4; ++row)
		{
			for (int axis = 0; axis < 3; ++axis)
			{
				camera[row * 4 + axis] = result[row][axis];
			}
		}
	}

	render_fov symmetric_fov(const fov (&fovs)[2], float crop_h, float crop_v, bool headset_aspect, float game_aspect)
	{
		render_fov out;
		for (const fov& f : fovs)
		{
			out.half_vertical = std::fmax(out.half_vertical, std::fmax(f.up, -f.down));
			out.half_horizontal = std::fmax(out.half_horizontal, std::fmax(f.right, -f.left));
		}
		out.half_horizontal = std::atan(std::tan(out.half_horizontal) * crop_h);
		out.half_vertical = std::atan(std::tan(out.half_vertical) * crop_v);
		if (headset_aspect)
		{
			out.aspect = std::tan(out.half_horizontal) / std::tan(out.half_vertical);
		}
		else
		{
			out.half_horizontal = std::atan(std::tan(out.half_vertical) * game_aspect);
		}
		return out;
	}

	pose centre_pose(const head_sample& head)
	{
		pose centre = head.eyes[0];
		for (int axis = 0; axis < 3; ++axis)
		{
			centre.position[axis] = 0.5f * (head.eyes[0].position[axis] + head.eyes[1].position[axis]);
		}
		return centre;
	}

	pose moved_along_right(const pose& eye, float metres)
	{
		const float qx = eye.orientation[0], qy = eye.orientation[1], qz = eye.orientation[2], qw = eye.orientation[3];
		const float right[3] = { 1 - 2 * (qy * qy + qz * qz), 2 * (qx * qy + qz * qw), 2 * (qx * qz - qy * qw) };
		pose moved = eye;
		for (int axis = 0; axis < 3; ++axis)
		{
			moved.position[axis] += right[axis] * metres;
		}
		return moved;
	}

	float eye_distance(const head_sample& head)
	{
		const float dx = head.eyes[1].position[0] - head.eyes[0].position[0];
		const float dy = head.eyes[1].position[1] - head.eyes[0].position[1];
		const float dz = head.eyes[1].position[2] - head.eyes[0].position[2];
		return std::sqrt(dx * dx + dy * dy + dz * dz);
	}

	float eye_angle(const head_sample& head)
	{
		const float* a = head.eyes[0].orientation;
		const float* b = head.eyes[1].orientation;
		const float dot = std::fabs(a[0] * b[0] + a[1] * b[1] + a[2] * b[2] + a[3] * b[3]);
		const float la = std::sqrt(a[0] * a[0] + a[1] * a[1] + a[2] * a[2] + a[3] * a[3]);
		const float lb = std::sqrt(b[0] * b[0] + b[1] * b[1] + b[2] * b[2] + b[3] * b[3]);
		if (la == 0.0f || lb == 0.0f)
		{
			return 3.14159265f;
		}
		return 2.0f * std::acos(std::fmin(1.0f, dot / (la * lb)));
	}

	void eye_pair::begin(std::uint64_t frame)
	{
		if (active_ && !(done_[0] && done_[1]))
		{
			++abandoned_;
		}
		frame_ = frame;
		active_ = true;
		done_[0] = done_[1] = false;
	}

	bool eye_pair::rendered(int eye, std::uint64_t frame)
	{
		if (!active_ || eye < 0 || eye > 1 || frame != frame_ || done_[eye])
		{
			++rejected_;
			return false;
		}
		done_[eye] = true;
		return true;
	}
}
