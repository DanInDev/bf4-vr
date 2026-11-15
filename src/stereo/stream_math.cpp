#include "stereo/stream_math.hpp"

#include <algorithm>
#include <cmath>

namespace bfvr::stereo::stream
{
	namespace
	{
		constexpr float pi = 3.14159265f;
		constexpr float max_pitch = 1.45f; // keep the level camera off the poles, where yaw is undefined
		constexpr float speed_cutoff_hz = 1.0f; // low-pass on the measured turning speed

		float wrap(float angle)
		{
			return std::remainder(angle, 2.0f * pi);
		}

		float blend(float cutoff_hz, float seconds)
		{
			return 1.0f - std::exp(-2.0f * pi * cutoff_hz * seconds);
		}
	}

	look look_of(const float (&q)[4])
	{
		matrix m;
		to_matrix(q, m);
		// Forward is -z turned: minus the third column.
		const float fx = -m[0][2], fy = -m[1][2], fz = -m[2][2];
		return { std::atan2(-fx, -fz), std::asin(std::clamp(fy, -1.0f, 1.0f)) };
	}

	void level_orientation(float yaw, float pitch, float (&out)[4])
	{
		// Yaw about +y, then pitch about the turned x axis.
		const float sy = std::sin(0.5f * yaw), cy = std::cos(0.5f * yaw);
		const float sp = std::sin(0.5f * pitch), cp = std::cos(0.5f * pitch);
		out[0] = cy * sp;
		out[1] = sy * cp;
		out[2] = -sy * sp;
		out[3] = cy * cp;
	}

	void steadier::configure(float rest_seconds, float speed_up)
	{
		rest_seconds_ = std::max(rest_seconds, 0.0f);
		speed_up_ = std::max(speed_up, 0.0f);
	}

	void steadier::reset()
	{
		started_ = false;
	}

	float steadier::step(float& value, float target, float speed, float seconds) const
	{
		const float cutoff = 1.0f / (2.0f * pi * rest_seconds_) + speed_up_ * speed;
		value += blend(cutoff, seconds) * (target - value);
		return value;
	}

	look steadier::update(const look& eye, float seconds)
	{
		const float pitch = std::clamp(eye.pitch, -max_pitch, max_pitch);
		const float yaw = started_ ? raw_yaw_ + wrap(eye.yaw - raw_yaw_) : eye.yaw;
		const bool jump = started_ && (std::fabs(yaw - raw_yaw_) > snap || std::fabs(pitch - raw_pitch_) > snap);
		if (!started_ || jump || rest_seconds_ <= 0.0f || !(seconds > 0.0f))
		{
			if (!started_ || jump || rest_seconds_ <= 0.0f)
			{
				yaw_ = yaw;
				pitch_ = pitch;
				yaw_speed_ = pitch_speed_ = 0.0f;
			}
			raw_yaw_ = yaw;
			raw_pitch_ = pitch;
			started_ = true;
			return { wrap(yaw_), pitch_ };
		}
		seconds = std::min(seconds, 0.1f);
		const float a = blend(speed_cutoff_hz, seconds);
		yaw_speed_ += a * ((yaw - raw_yaw_) / seconds - yaw_speed_);
		pitch_speed_ += a * ((pitch - raw_pitch_) / seconds - pitch_speed_);
		raw_yaw_ = yaw;
		raw_pitch_ = pitch;
		// One cut-off for both axes: a diagonal look is followed along a straight line.
		const float speed = std::hypot(yaw_speed_, pitch_speed_);
		step(yaw_, yaw, speed, seconds);
		step(pitch_, pitch, speed, seconds);
		// Keep the unwrapped values near zero turns so float precision holds over a long session.
		const float turns = std::round(raw_yaw_ / (2.0f * pi)) * 2.0f * pi;
		raw_yaw_ -= turns;
		yaw_ -= turns;
		return { wrap(yaw_), pitch_ };
	}

	edges edges_of_fov(float left, float right, float up, float down)
	{
		return { std::tan(left), std::tan(right), std::tan(up), std::tan(down) };
	}

	extent extent_of(float horizontal_degrees, float aspect)
	{
		const float x = std::tan(0.5f * std::clamp(horizontal_degrees, 10.0f, 170.0f) * pi / 180.0f);
		return { x, aspect > 0.0f ? x / aspect : x };
	}

	void to_matrix(const float (&q)[4], matrix& m)
	{
		const float x = q[0], y = q[1], z = q[2], w = q[3];
		m[0][0] = 1 - 2 * (y * y + z * z);
		m[0][1] = 2 * (x * y - w * z);
		m[0][2] = 2 * (x * z + w * y);
		m[1][0] = 2 * (x * y + w * z);
		m[1][1] = 1 - 2 * (x * x + z * z);
		m[1][2] = 2 * (y * z - w * x);
		m[2][0] = 2 * (x * z - w * y);
		m[2][1] = 2 * (y * z + w * x);
		m[2][2] = 1 - 2 * (x * x + y * y);
	}

	void between(const float (&eye)[4], const float (&stream)[4], matrix& out)
	{
		matrix e, s;
		to_matrix(eye, e);
		to_matrix(stream, s);
		for (int i = 0; i < 3; ++i)
		{
			for (int j = 0; j < 3; ++j)
			{
				out[i][j] = e[0][i] * s[0][j] + e[1][i] * s[1][j] + e[2][i] * s[2][j];
			}
		}
	}

	bool fits(const matrix& to_eye, const extent& stream, const edges& eye)
	{
		// The picture's edges are straight lines in the eye's picture too, and the eye's picture is
		// convex: the four corners decide.
		for (int corner = 0; corner < 4; ++corner)
		{
			const float v[3]{ (corner & 1) ? stream.x : -stream.x, (corner & 2) ? stream.y : -stream.y, -1.0f };
			float r[3];
			for (int i = 0; i < 3; ++i)
			{
				r[i] = to_eye[i][0] * v[0] + to_eye[i][1] * v[1] + to_eye[i][2] * v[2];
			}
			if (r[2] > -1e-3f)
			{
				return false;
			}
			const float x = r[0] / -r[2];
			const float y = r[1] / -r[2];
			if (x < eye.left || x > eye.right || y > eye.up || y < eye.down)
			{
				return false;
			}
		}
		return true;
	}

	void slerp(const float (&a)[4], const float (&b)[4], float t, float (&out)[4])
	{
		float d = a[0] * b[0] + a[1] * b[1] + a[2] * b[2] + a[3] * b[3];
		const float sign = d < 0.0f ? -1.0f : 1.0f;
		d = std::fabs(d);
		float wa = 1.0f - t, wb = t;
		if (d < 0.9995f)
		{
			const float angle = std::acos(std::min(d, 1.0f));
			const float s = std::sin(angle);
			wa = std::sin((1.0f - t) * angle) / s;
			wb = std::sin(t * angle) / s;
		}
		float length = 0.0f;
		for (int i = 0; i < 4; ++i)
		{
			out[i] = wa * a[i] + wb * sign * b[i];
			length += out[i] * out[i];
		}
		length = std::sqrt(length);
		for (float& value : out)
		{
			value /= length;
		}
	}

	float aim(const float (&eye)[4], const look& steadied, const extent& stream, const edges& eye_edges, float (&out)[4])
	{
		float level[4];
		level_orientation(steadied.yaw, steadied.pitch, level);
		matrix m;
		between(eye, level, m);
		if (fits(m, stream, eye_edges))
		{
			std::copy(std::begin(level), std::end(level), out);
			return 1.0f;
		}
		// The largest share of the steadied camera that still fits; the eye's own camera (t = 0) fits
		// whenever the stream picture is narrower than the eye's.
		float lo = 0.0f, hi = 1.0f;
		for (int i = 0; i < 14; ++i)
		{
			const float mid = 0.5f * (lo + hi);
			float q[4];
			slerp(eye, level, mid, q);
			between(eye, q, m);
			(fits(m, stream, eye_edges) ? lo : hi) = mid;
		}
		slerp(eye, level, lo, out);
		return lo;
	}

	rect letterbox(float window_aspect, float picture_aspect)
	{
		if (!(window_aspect > 0.0f) || !(picture_aspect > 0.0f))
		{
			return {};
		}
		if (window_aspect > picture_aspect)
		{
			const float width = picture_aspect / window_aspect;
			return { 0.5f * (1.0f - width), 0.0f, width, 1.0f };
		}
		const float height = window_aspect / picture_aspect;
		return { 0.0f, 0.5f * (1.0f - height), 1.0f, height };
	}
}
