#pragma once

#include "body/arm_ik.hpp"

// Finger poses for the first-person hands: open, curled around a magazine, hooked on a charging
// handle, following the controller's squeeze and trigger, or as the game animated them (the gun's
// grips). Engine-free; tested by tests/finger_pose_test.cpp.
//
// Rig facts (re/notes/skeleton.md "Fingers"): every finger is <Hand><Finger>0..4 under the hand bone
// (Thumb1..4). Joints 1-3 of index..pinky and joints 2-3 of the thumb bend about their local +z on
// both hands; joint 0 (metacarpal) and Thumb1 (the thumb's swing across the palm) are left as animated.
namespace bfvr::body::fingers
{
	enum finger
	{
		thumb,
		index,
		middle,
		ring,
		pinky,
		count,
	};

	// The bent joints of one finger, from the knuckle out.
	constexpr int joints = 3;

	// Per finger: how much of the game's animated pose is kept (1 = as animated), and the curl the
	// rest is drawn with (0 = open, 1 = fist).
	struct hand_shape
	{
		float animated[count]{ 1, 1, 1, 1, 1 };
		float curl[count]{};
	};

	// The controller's finger sensors for one hand.
	struct finger_input
	{
		bool valid{};          // read from a focused session (or a test value)
		float squeeze{};       // grip, 0..1
		float trigger{};       // 0..1
		bool trigger_touch{};
		bool thumb_touch{};    // thumbstick, face buttons or thumb rest
	};

	// What a hand is doing; picks its shape.
	enum class task
	{
		free,       // nothing in it: the controller's squeeze and trigger curl the fingers
		item,       // holds what the game animated (gun grip, grenade, knife, gadget)
		gun_grip,   // the gun hand on a gun: animated, but the index lies along the frame off the trigger
		support,    // the off hand on the handguard: as animated
		magazine,   // wrapped around a held magazine
		handle,     // hooked on the charging handle
		reach,      // open, about to grab something (inside a grab zone)
	};

	hand_shape shape_for(task what, const finger_input& input);

	// Bend angle (radians) about local z of a joint rotation (its twist about z).
	float bend_of(ik::quat rotation);
	// The rotation with its bend about z replaced by angle, the rest (spread) kept.
	ik::quat with_bend(ik::quat rotation, float angle);
	// Bend of joint (0..2: knuckle, middle, tip) of a finger at a curl (0 open .. 1 fist), radians.
	float bend_at(finger which, int joint, float curl);
	// The drawn local rotation of a joint for a shape, from the animated one.
	ik::quat shaped(ik::quat animated, finger which, int joint, const hand_shape& shape);

	// The left hand's rotation that mirrors a right-hand rotation across the x = 0 plane (OpenXR x right).
	// Hand bones: fingers along local +x, palm towards +y, the index at +z on the right hand and -z on
	// the left, so the bone axes mirror in z. Turns the gun hand's grip into the same grip on the left.
	ik::quat mirrored_hand(ik::quat right);

	// Rotation whose columns are the given orthonormal axes.
	ik::quat from_basis(ik::vec3 x, ik::vec3 y, ik::vec3 z);

	// Where an object held in the fist sits, from the drawn joints (any one space): the centre between
	// the curled middle finger and the thumb, and a frame with +y from the little finger towards the
	// index (an object's top comes out there) and +z out through the curled fingers.
	struct grasp
	{
		bool valid{};
		ik::vec3 centre;
		ik::quat frame;
	};
	grasp grasp_of(ik::vec3 index_knuckle, ik::vec3 pinky_knuckle, ik::vec3 middle_knuckle, ik::vec3 middle_tip, ik::vec3 thumb_tip);

	// Eases a hand from one shape to the next, so the fingers move instead of popping.
	class shape_smoother
	{
	public:
		// seconds: time since the last call; time_constant: about how long a change takes to settle.
		const hand_shape& update(const hand_shape& target, float seconds, float time_constant = 0.07f);
		const hand_shape& current() const { return current_; }
		void reset(const hand_shape& to) { current_ = to; started_ = true; }

	private:
		hand_shape current_;
		bool started_{};
	};
}
