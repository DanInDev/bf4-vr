#pragma once

#include "weapon/vec.hpp"

// Ladders by hand (#134): grip a rung, pull to climb, hand over hand, up and down. Engine-free; the game
// adapter is weapon/ladders. BF4's own climb moves the soldier (character state Climbing, the left stick's
// forward/back at up to the climbing state's speed); this decides which rung each hand holds and where the
// body has to be for the gripping hands to stay on their rungs, and turns that into the stick the game climbs
// by. Measured on SP_Shanghai's wall ladder (re/notes/ladders.md). Tested by tests/ladder_climb_test.cpp.
namespace bfvr::weapon::ladder
{
	// A ladder in the game's world: its foot (the entity's origin, the bottom of the rails), up its rails,
	// across its rungs (right, seen by the climber) and out of the wall towards the climber.
	struct frame
	{
		v3 foot;
		v3 right{ 1, 0, 0 };
		v3 up{ 0, 1, 0 };
		v3 out{ 0, 0, 1 };
		float length{}; // rails, metres (the game's climb length); 0 = not known
	};

	// Where the rungs are in the ladder's own frame (the wall ladder model: 35 rungs, 0.298 m apart).
	struct rung_model
	{
		float first{ 0.166f };     // the lowest rung's centre above the foot
		float spacing{ 0.2978f };  // centre to centre
		float plane{ 0.025f };     // the rungs' centres out from the foot's plane
		float half_width{ 0.27f }; // a hand counts on a rung this far either side of the centre line (rails at ~0.30)
		float default_length{ 10.39f }; // for a ladder whose length the game has not told yet
	};

	v3 to_local(const frame& ladder, const v3& world);
	v3 to_world(const frame& ladder, const v3& local);

	// The rung nearest to a world point within reach metres (along its bar, clamped to the rails). False when no
	// rung of the ladder is that close.
	bool nearest_rung(const frame& ladder, const rung_model& rungs, const v3& point, float reach, v3& on_rung, int& index);

	// The game's climb speed for a stick deflection: BF4 answers v = speed * min(1, gain * (|s| - dead)) (monitor
	// 2026-11-25: 0.3 nothing, 0.5 0.6 m/s, 0.7 1.25, 0.9 1.9, 1.0 2.0 at the default 2 m/s; the same downwards).
	struct stick_curve
	{
		float dead{ 0.3f };
		float gain{ 1.6f };
	};
	float climb_speed(const stick_curve& curve, float speed, float stick);
	// The deflection that asks for velocity v (m/s, + up) at the state's speed; 0 below a millimetre a second.
	float stick_for(const stick_curve& curve, float speed, float v);

	struct hand_in
	{
		bool tracked{};
		bool grip{};  // squeezed
		bool busy{};  // something else has this hand (a gun, a magazine, the panel): no rung for it
		v3 palm;      // OpenXR LOCAL (physical: unaffected by the game moving the soldier)
		v3 palm_world; // the same palm in the game's world now
	};

	struct climb_in
	{
		float dt{};
		bool on_ladder{};  // the character's state is Climbing
		bool have_ladder{};
		frame ladder;      // the ladder the soldier is on, or the nearest known one while on the ground
		float body_y{};    // the character's world height now
		float speed{ 2.0f }; // the climbing state's speed in use (m/s)
		hand_in hands[2];  // 0 = left (off hand), 1 = right
	};

	enum class event { none, grabbed, released };

	struct climb_out
	{
		bool holding[2]{};
		v3 rung[2];          // the held point on the rung, world (where the drawn palm stays)
		event events[2]{};
		bool drive{};        // the hands climb: the stick below goes to the game
		float stick_y{};     // left stick, + forward (up the ladder)
		float target_y{};    // where the hands want the body
		bool mount{};        // on the ground, a hand took a rung: walk onto the ladder
		v3 mount_direction;  // world, horizontal unit: towards the ladder
	};

	struct climb_config
	{
		rung_model rungs;
		stick_curve curve;
		float grab_reach{ 0.12f };  // a palm this close to a rung takes it
		float follow{ 12.0f };      // 1/s: how hard the body chases the hands
		float lead_limit{ 0.40f };  // the hands' target never runs further than this from the body (blocked, top, bottom)
		float mount_seconds{ 0.8f }; // walking onto the ladder gives up after this
		// Getting on, the game moves the body itself for a moment (monitor: up 0.9 m and back): the hands drive
		// nothing until it has stood still this long, or this long at most.
		float settle_still{ 0.25f };
		float settle_limit{ 1.5f };
		// Metres the body has moved other than the stick asked (summed with a 0.15 s memory) before that counts as the
		// game's own move (climb-off) or a block, taken along rather than pushed against.
		float own_move{ 0.06f };
	};

	// One per player. Grabs only on a fresh squeeze (one held from elsewhere never takes a rung); the hands'
	// target moves by the gripping hands' vertical travel (both averaged), rebased whenever a hand takes or leaves
	// a rung so the body never jumps; leaving the ladder (the game's climb-off at the top, the ground at the
	// bottom) lets go of everything.
	class climber
	{
	public:
		explicit climber(const climb_config& config = {}) : config_(config) {}
		climb_out update(const climb_in& in);
		void reset();
		const climb_config& config() const { return config_; }
		bool holding(int hand) const { return held_[hand]; }

	private:
		void rebase(const climb_in& in);

		climb_config config_;
		bool held_[2]{};
		v3 rung_[2];
		float palm_anchor_[2]{};
		float body_anchor_{};
		float target_{};
		float last_body_{};
		float last_v_{};     // m/s the stick sent last frame asks of the game
		float last_v2_{};    // ...and the frame before (the game follows a frame late)
		float own_move_{};   // the body's move other than asked, recent sum
		bool last_grip_[2]{};
		bool was_on_{};
		bool settling_{};    // just on: the game is still placing the body
		float settle_seconds_{};
		float still_seconds_{};
		float settle_y_{};
		float mounting_{};   // seconds left walking onto the ladder
		int mount_hand_{ -1 };
		v3 mount_rung_;
	};
}
