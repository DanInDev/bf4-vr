// Checks the stream view's maths (src/stereo/stream_math): look angles round-trip, head shake is
// taken out while a turn is followed, a cut is taken at once, roll is removed, the picture stays on
// the eye's picture, and the letterbox keeps 16:9.
#include <cmath>
#include <cstdio>

#include "stereo/stream_math.hpp"

namespace stream = bfvr::stereo::stream;

namespace
{
	int failures = 0;
	constexpr float pi = 3.14159265f;
	constexpr float degree = pi / 180.0f;

	void expect(bool condition, const char* what)
	{
		if (!condition)
		{
			std::printf("FAIL: %s\n", what);
			++failures;
		}
	}

	bool near(float a, float b, float tolerance)
	{
		return std::fabs(a - b) <= tolerance;
	}

	// Yaw, pitch, then roll about the view axis (positive: the head tilts to its right), as pipe fakehead builds it.
	void head(float yaw, float pitch, float roll, float (&q)[4])
	{
		float yp[4];
		stream::level_orientation(yaw, pitch, yp);
		const float r[4]{ 0.0f, 0.0f, std::sin(-0.5f * roll), std::cos(-0.5f * roll) };
		// q = yp * r
		q[0] = yp[3] * r[0] + r[3] * yp[0] + (yp[1] * r[2] - yp[2] * r[1]);
		q[1] = yp[3] * r[1] + r[3] * yp[1] + (yp[2] * r[0] - yp[0] * r[2]);
		q[2] = yp[3] * r[2] + r[3] * yp[2] + (yp[0] * r[1] - yp[1] * r[0]);
		q[3] = yp[3] * r[3] - (yp[0] * r[0] + yp[1] * r[1] + yp[2] * r[2]);
	}

	// Where the stream camera's centre ray lands in the eye's picture, as tangents.
	void centre_in_eye(const float (&eye)[4], const float (&cam)[4], float& x, float& y)
	{
		stream::matrix m;
		stream::between(eye, cam, m);
		x = m[0][2] * -1.0f / -(m[2][2] * -1.0f);
		y = m[1][2] * -1.0f / -(m[2][2] * -1.0f);
	}
}

int main()
{
	// Look angles of a level camera come back unchanged.
	{
		float q[4];
		stream::level_orientation(0.7f, -0.3f, q);
		const stream::look l = stream::look_of(q);
		expect(near(l.yaw, 0.7f, 1e-4f) && near(l.pitch, -0.3f, 1e-4f), "look_of(level_orientation) round-trips");
		float rolled[4];
		head(0.7f, -0.3f, 25 * degree, rolled);
		const stream::look r = stream::look_of(rolled);
		expect(near(r.yaw, 0.7f, 1e-4f) && near(r.pitch, -0.3f, 1e-4f), "roll does not change where the head looks");
	}

	// The level camera has no roll: its right axis stays horizontal.
	{
		float q[4];
		stream::level_orientation(1.2f, 0.4f, q);
		stream::matrix m;
		stream::to_matrix(q, m);
		expect(near(m[1][0], 0.0f, 1e-5f), "level camera: right axis horizontal");
	}

	constexpr float dt = 1.0f / 90.0f;

	// Head shake (+-1.5 degrees at 4 Hz) is mostly taken out.
	{
		stream::steadier s;
		s.configure(0.3f, 3.0f);
		float worst = 0.0f;
		for (int i = 0; i < 90 * 6; ++i)
		{
			const float t = i * dt;
			const stream::look eye{ 0.5f + 1.5f * degree * std::sin(2 * pi * 4.0f * t), 0.1f + 1.0f * degree * std::cos(2 * pi * 3.0f * t) };
			const stream::look out = s.update(eye, dt);
			if (t > 3.0f)
			{
				worst = std::fmax(worst, std::fmax(std::fabs(out.yaw - 0.5f), std::fabs(out.pitch - 0.1f)));
			}
		}
		std::printf("shake: worst %.3f degrees of 1.5\n", worst / degree);
		expect(worst < 0.4f * degree, "head shake: under a third left");
	}

	// A steady 60 degree/s turn is followed with a small lag; the view settles where the head stops.
	{
		stream::steadier s;
		s.configure(0.3f, 3.0f);
		float lag = 0.0f;
		stream::look out{};
		for (int i = 0; i < 90; ++i)
		{
			const stream::look eye{ 60 * degree * (i * dt), 0.0f };
			out = s.update(eye, dt);
			if (i > 45)
			{
				lag = std::fmax(lag, eye.yaw - out.yaw);
			}
		}
		std::printf("turn: lag %.2f degrees at 60 deg/s\n", lag / degree);
		expect(lag < 8 * degree, "turn: lag under 8 degrees at 60 deg/s");
		for (int i = 0; i < 180; ++i)
		{
			out = s.update({ 60 * degree * (89 * dt), 0.0f }, dt);
		}
		expect(near(out.yaw, 60 * degree * (89 * dt), 0.2f * degree), "turn: settles where the head stops");
	}

	// A step of 10 degrees is eased in, not jumped to.
	{
		stream::steadier s;
		s.configure(0.3f, 3.0f);
		s.update({ 0.0f, 0.0f }, dt);
		const stream::look first = s.update({ 10 * degree, 0.0f }, dt);
		expect(first.yaw > 0.0f && first.yaw < 5 * degree, "10 degree step: eased in");
	}

	// A cut (more than the snap angle) is taken at once; so is a yaw across +-180 without a spin.
	{
		stream::steadier s;
		s.configure(0.3f, 3.0f);
		s.update({ 0.0f, 0.0f }, dt);
		const stream::look cut = s.update({ 1.5f, 0.0f }, dt);
		expect(near(cut.yaw, 1.5f, 1e-5f), "cut: taken at once");
		stream::steadier w;
		w.configure(0.3f, 3.0f);
		w.update({ pi - 0.01f, 0.0f }, dt);
		const stream::look across = w.update({ -pi + 0.01f, 0.0f }, dt);
		expect(std::fabs(std::remainder(across.yaw - pi, 2 * pi)) < 0.02f, "yaw across 180: no spin");
	}

	// No steadying: the eye's angles as they are.
	{
		stream::steadier s;
		s.configure(0.0f, 3.0f);
		s.update({ 0.0f, 0.0f }, dt);
		const stream::look out = s.update({ 0.2f, -0.1f }, dt);
		expect(near(out.yaw, 0.2f, 1e-6f) && near(out.pitch, -0.1f, 1e-6f), "smoothing 0: follows exactly");
	}

	// The fake head's FOV (pipe fakehead), rendered symmetric: 54 degrees each side, 51 up and down.
	const stream::edges eye_edges = stream::edges_of_fov(-0.942f, 0.942f, 0.890f, -0.890f);
	const stream::extent picture = stream::extent_of(85.0f, 16.0f / 9.0f);
	expect(near(picture.x, std::tan(42.5f * degree), 1e-5f) && near(picture.y, picture.x * 9 / 16, 1e-5f), "extent: 85 degrees across, 16:9");

	// Steadied a few degrees off the eye: the level camera is kept, its centre where it looks.
	{
		float eye[4];
		head(0.3f, 0.05f, 0.0f, eye);
		float cam[4];
		const float kept = stream::aim(eye, { 0.3f + 4 * degree, 0.05f }, picture, eye_edges, cam);
		expect(kept == 1.0f, "4 degrees off: steadied camera kept");
		float x = 0, y = 0;
		centre_in_eye(eye, cam, x, y);
		expect(near(std::atan(x), -4 * degree, 0.1f * degree), "4 degrees off: centre 4 degrees left in the eye's picture");
	}

	// Rolled head: the stream stays level (the horizon in the picture is the world's).
	{
		float eye[4];
		head(0.0f, 0.0f, 10 * degree, eye);
		float cam[4];
		const float kept = stream::aim(eye, stream::look_of(eye), picture, eye_edges, cam);
		stream::matrix m;
		stream::to_matrix(cam, m);
		expect(kept == 1.0f && near(m[1][0], 0.0f, 1e-5f), "10 degree roll: stream level");
	}

	// Far behind a fast turn: pulled towards the eye until the corners fit, never off the picture.
	{
		float eye[4];
		head(0.0f, 0.0f, 0.0f, eye);
		float cam[4];
		const float kept = stream::aim(eye, { 30 * degree, 0.0f }, picture, eye_edges, cam);
		stream::matrix m;
		stream::between(eye, cam, m);
		expect(kept > 0.0f && kept < 1.0f, "30 degrees behind: partly steadied");
		expect(stream::fits(m, picture, eye_edges), "30 degrees behind: corners on the eye's picture");
		std::printf("30 degrees behind: kept %.2f\n", kept);
	}

	// A head rolled far in a cropped view (45 degrees each side, 40 up and down; the fake head's wide
	// picture holds a level 85 degree stream at any roll): level does not fit, so some roll comes back,
	// still on the picture.
	{
		const stream::edges cropped = stream::edges_of_fov(-45 * degree, 45 * degree, 40 * degree, -40 * degree);
		float eye[4];
		head(0.0f, 0.0f, 45 * degree, eye);
		float cam[4];
		const float kept = stream::aim(eye, stream::look_of(eye), picture, cropped, cam);
		stream::matrix m;
		stream::between(eye, cam, m);
		expect(kept < 1.0f && stream::fits(m, picture, cropped), "45 degree roll, cropped view: partly level, on the picture");
		std::printf("45 degree roll, cropped view: kept %.2f\n", kept);
	}

	// Letterbox: 16:9 fills a 16:9 window, has bars on an ultrawide, and top/bottom bars on a square.
	{
		const stream::rect same = stream::letterbox(16.0f / 9.0f, 16.0f / 9.0f);
		expect(near(same.width, 1.0f, 1e-6f) && near(same.height, 1.0f, 1e-6f), "letterbox: 16:9 window filled");
		const stream::rect wide = stream::letterbox(21.0f / 9.0f, 16.0f / 9.0f);
		expect(near(wide.width, 16.0f / 21.0f, 1e-5f) && near(wide.x, 0.5f * (1 - 16.0f / 21.0f), 1e-5f), "letterbox: ultrawide, bars at the sides");
		const stream::rect square = stream::letterbox(1.0f, 16.0f / 9.0f);
		expect(near(square.height, 9.0f / 16.0f, 1e-5f) && near(square.y, 0.5f * (1 - 9.0f / 16.0f), 1e-5f), "letterbox: square window, bars top and bottom");
	}

	if (failures)
	{
		std::printf("%d failure(s)\n", failures);
		return 1;
	}
	std::printf("stream_math: all passed\n");
	return 0;
}
