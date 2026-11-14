#pragma once

#include "weapon/vec.hpp"

// The drawn gun stops at walls (#63): a ray along the real gun's barrel (where the controller holds it, never
// the drawn one) says how far the barrel would be inside the level; the drawn gun is pulled back along the
// barrel by that much plus a margin, so the muzzle stays on the near side. Never stuck (the BF3 VR mod's
// user's rule): the push is worked out from the real gun every frame, keeps no memory but a short ease, and
// lets go when it would take more than max_push (the barrel then goes into the wall rather than the drawn
// hand leaving the real one). Engine-free; tested by tests/wall_stop_test.cpp.
namespace bfvr::weapon
{
	struct wall_stop_config
	{
		float margin{ 0.03f };   // the muzzle stays this far short of the wall
		float max_push{ 0.40f }; // more than this: let go (a rifle pushed back to the chest)
		float rejoin{ 0.05f };   // let go, it takes hold again only once the push needed is this much under max_push
		float ease_in{ 0.04f };  // seconds (time constants); headset 2026-11-14: 20 / 60 ms jittered
		float ease_out{ 0.08f };
	};

	class wall_stop
	{
	public:
		explicit wall_stop(const wall_stop_config& config = {}) : config_(config) {}
		// inside: metres of the real barrel beyond the first hit along it (0 or less: clear); known: the ray
		// answered for this frame's gun. Returns the push back along the barrel, metres.
		float update(float seconds, bool known, float inside);
		float push() const { return push_; }
		bool let_go() const { return let_go_; }
		void reset() { push_ = 0.0f; let_go_ = false; }

	private:
		wall_stop_config config_;
		float push_{};
		bool let_go_{};
	};
}
