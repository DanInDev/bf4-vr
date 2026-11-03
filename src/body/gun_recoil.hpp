#pragma once

// The drawn gun's kick (#14, after Overlord's VR recoil): each shot pitches the muzzle up about the
// wrist and pushes the gun back along the barrel; full auto stacks up to a cap, and both settle back
// to the controller. Only the drawn gun and hands move: the controller, the head and where the shots
// go are unchanged. Engine-free; tested by tests/gun_recoil_test.cpp.
namespace bfvr::body
{
	struct recoil_config
	{
		float degrees_per_shot{ 2.5f };
		float back_per_shot{ 0.012f };   // metres along the barrel
		float max_degrees{ 15.0f };
		float max_back{ 0.035f };
		float pitch_recovery{ 0.22f };  // seconds (time constant); Overlord settles its climb in 0.22 s
		float back_recovery{ 0.06f };   // the push back returns faster than the muzzle comes down
		float two_handed_scale{ 0.4f }; // a support hand on the handguard steadies the gun
	};

	struct recoil_offset
	{
		float degrees{};
		float back{};
	};

	class gun_recoil
	{
	public:
		explicit gun_recoil(const recoil_config& config = {}) : config_(config) {}
		// shots fired at `now` (seconds, any steady clock); strength: the live setting (0 = off, 1 = as
		// tuned); two_handed: the support hand holds the gun.
		void shot(double now, int shots, float strength, bool two_handed);
		// The kick at `now`; exactly zero two seconds after the last shot.
		recoil_offset at(double now) const;
		void reset() { state_ = {}; last_ = -1.0; }

	private:
		recoil_offset decayed(double now) const;

		recoil_config config_;
		recoil_offset state_;
		double last_{ -1.0 };
	};
}
