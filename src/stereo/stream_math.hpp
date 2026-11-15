#pragma once

// Stream view (stream_view): a steadied, level, 16:9 picture of what the left eye sees, for recording
// the game window with OBS. The engine-free half: the steadying filter, the camera it gives, the turn
// from that camera into the eye's picture, and where the picture goes in the window. Tested by
// tests/stream_math_test.cpp; stereo/stream_view.cpp draws it.
namespace bfvr::stereo::stream
{
	// Yaw (left positive) and pitch (up positive) of where an orientation looks (its -z), radians.
	struct look
	{
		float yaw{};
		float pitch{};
	};
	look look_of(const float (&orientation)[4]);

	// A level camera (no roll) looking along yaw and pitch, as a quaternion x, y, z, w.
	void level_orientation(float yaw, float pitch, float (&out)[4]);

	// The steadying: a low-pass on yaw and pitch whose cut-off rises with the turning speed, so the
	// head's small shakes are taken out while a real turn is followed closely. A jump larger than
	// snap (a recenter, a cut, a teleport) is taken at once.
	class steadier
	{
	public:
		// rest_seconds: time constant while the head is held still (0: no steadying).
		// speed_up: how much higher the cut-off gets per radian a second of turning (Hz per rad/s).
		void configure(float rest_seconds, float speed_up);
		look update(const look& eye, float seconds);
		void reset();
		bool started() const { return started_; }

		static constexpr float snap = 0.35f; // radians between two frames

	private:
		float step(float& value, float target, float speed, float seconds) const;

		bool started_{};
		float rest_seconds_{ 0.3f };
		float speed_up_{ 3.0f };
		float yaw_{}, pitch_{};          // steadied, yaw unwrapped
		float raw_yaw_{}, raw_pitch_{};  // last input, yaw unwrapped
		float yaw_speed_{}, pitch_speed_{};
	};

	// Tangents of a picture's edges from its camera (x right, y up, looking along -z).
	struct edges
	{
		float left{}, right{}, up{}, down{}; // left and down negative
	};
	edges edges_of_fov(float left, float right, float up, float down); // radians

	// Half width and height of the stream camera's picture as tangents: horizontal_degrees across, aspect = w / h.
	struct extent
	{
		float x{}, y{};
	};
	extent extent_of(float horizontal_degrees, float aspect);

	// 3x3 rotation, m[row][column]; its columns are the turned x, y and z axes.
	using matrix = float[3][3];
	void to_matrix(const float (&q)[4], matrix& m);
	// The turn that takes a ray in the stream camera to the same ray in the eye's camera: eye^T * stream.
	void between(const float (&eye)[4], const float (&stream)[4], matrix& out);
	// Whether all four corners of the stream picture, turned by to_eye, land inside the eye's picture.
	bool fits(const matrix& to_eye, const extent& stream, const edges& eye);
	// Normalised spherical blend from a (t = 0) to b (t = 1).
	void slerp(const float (&a)[4], const float (&b)[4], float t, float (&out)[4]);

	// The stream camera for this frame: the steadied level camera, or as far towards the eye's own
	// orientation as needed to keep the picture's corners on the eye's picture (a fast turn the
	// steadying trails, a head rolled far). Returns how much of the steadying was kept (1 all, 0 none).
	float aim(const float (&eye)[4], const look& steadied, const extent& stream, const edges& eye_edges, float (&out)[4]);

	// Where a picture of aspect picture_aspect goes in a window of window_aspect, as fractions of the
	// window: the largest centred rectangle of that shape (black bars on the other sides).
	struct rect
	{
		float x{}, y{}, width{ 1.0f }, height{ 1.0f };
	};
	rect letterbox(float window_aspect, float picture_aspect);
}
