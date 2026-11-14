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
		float max_yaw{ 4.0f };
		float pitch_recovery{ 0.22f };  // seconds (time constant); Overlord settles its climb in 0.22 s
		float back_recovery{ 0.06f };   // the push back returns faster than the muzzle comes down
		float two_handed_scale{ 0.4f }; // a support hand on the handguard steadies the gun
		float rest_degrees{ 0.25f };    // below this the gun is at rest: the next shot is a first shot
		// #60: a long gun (not a pistol) fired without the support hand, at the one_hand setting's full
		// strength: the climb per shot this many times the one-handed kick above (with two_handed_scale 0.4:
		// ~5x a two-handed shot, as the BF3 VR mod tuned it), the push back this many times (~2.5x), a
		// random throw of up to one_hand_scatter degrees per shot up/down and sideways, the muzzle settling
		// one_hand_recovery times slower, and higher caps.
		float one_hand_climb{ 2.0f };
		float one_hand_back{ 1.0f };
		float one_hand_scatter{ 1.5f };
		float one_hand_recovery{ 1.8f };
		float one_hand_max_degrees{ 35.0f };
		float one_hand_max_back{ 0.05f };
		float one_hand_max_yaw{ 12.0f };
	};

	// The gun's own recoil for the current stance (BF4: GunSwayData, hip fire), relative to the
	// reference gun the config is tuned on. The default kicks exactly as the config says.
	struct recoil_profile
	{
		float vertical{ 1.0f };       // scales degrees_per_shot and back_per_shot
		float first_shot{ 1.0f };     // extra for a shot from rest
		float yaw_min{};              // degrees per shot, drawn uniformly between min and max
		float yaw_max{};
		float recovery{ 1.0f };       // scales the settle times (> 1: settles slower)
		bool pistol{};                // a handgun (the game's pistol animation set): no one-hand penalty
	};

	struct recoil_offset
	{
		float degrees{};
		float back{};
		float yaw{}; // degrees, positive = muzzle to the right
	};

	class gun_recoil
	{
	public:
		explicit gun_recoil(const recoil_config& config = {}) : config_(config) {}
		// shots fired at `now` (seconds, any steady clock); strength: the live setting (0 = off, 1 = as
		// tuned); two_handed: the support hand holds the gun; one_hand: the one-hand penalty's strength
		// for a long gun fired without it (0 = none, 1 = as tuned, #60).
		void shot(double now, int shots, float strength, bool two_handed, const recoil_profile& gun = {}, float one_hand = 0.0f);
		// The kick at `now`; exactly zero two seconds after the last shot.
		recoil_offset at(double now) const;
		void reset() { state_ = {}; last_ = -1.0; }

	private:
		recoil_offset decayed(double now) const;
		float random(); // [0, 1)

		recoil_config config_;
		recoil_offset state_;
		double last_{ -1.0 };
		float recovery_{ 1.0f };
		unsigned seed_{ 0x9E3779B9u };
	};

	// The game's values for one gun and stance (BF4 GunSwayRecoilData and GunSwayData).
	struct game_recoil
	{
		float vertical{};        // RecoilAmplitudeIncPerShot
		float horizontal_min{};  // HorizontalRecoilAmplitudeIncPerShotMin / Max
		float horizontal_max{};
		float decrease{};        // RecoilAmplitudeDecreaseFactor (higher: recovers faster)
		float first_shot{ 1.0f }; // FirstShotRecoilMultiplier
	};

	// Profile relative to the reference gun the config is tuned on (the AK-12: 0.3 up, decrease 18):
	// sideways at half the vertical gain, settle times by the decrease factors' ratio.
	recoil_profile profile_from_game(const game_recoil& gun, const game_recoil& reference, const recoil_config& config = {});
}
