#pragma once

#include "weapon/reload_hands.hpp"

// Physical grenades (docs/tasks/grenades.md): take one from the belt with the off hand, pull the pin
// with the gun hand, throw by letting go while the hand moves. Engine-free; positions in OpenXR
// LOCAL relative to the first-person camera, metres. Tested by tests/reload_test.cpp.
namespace bfvr::weapon
{
	struct grenade_frame
	{
		float seconds{};    // since the last update
		bool hands_valid{};
		v3 off_hand;        // holds and throws the grenade
		v3 gun_hand;        // pulls the pin
		v3 belt;            // grenade pouch
		bool off_grip{};
		bool gun_grip{};
		bool have_grenade{ true }; // the game still has one to throw
	};

	enum class grenade_event
	{
		none,
		taken,
		pin_pulled,
		thrown,    // with velocity and position
		dropped,   // pin out, let go without a throw: falls at the hand
		put_back,  // pin in: back to the pouch
	};

	struct grenade_result
	{
		grenade_event event{ grenade_event::none };
		v3 velocity;      // thrown / dropped: hand velocity at release
		v3 position;      // where it leaves the hand
		bool holding{};
		bool armed{};
		bool off_busy{};  // keep the off-hand grip from the game
		bool gun_busy{};  // keep the gun-hand grip from the game
	};

	struct grenade_config
	{
		float reach_belt{ 0.15f };
		float reach_pin{ 0.10f };   // gun hand this close to the grenade grabs the pin
		float pin_pull{ 0.05f };    // pulled this far away = pin out
		float throw_speed{ 1.5f };  // hand speed at release that counts as a throw (m/s)
		float velocity_window{ 0.08f }; // seconds of hand motion averaged into one velocity
		float release_lookback{ 0.15f }; // the throw is the fastest such velocity this long before release
	};

	class grenade_hands
	{
	public:
		explicit grenade_hands(const grenade_config& config = {}) : config_(config) {}
		grenade_result update(const grenade_frame& frame);
		void reset();

	private:
		static constexpr int history_size = 32;
		struct sample
		{
			float time{};
			v3 position;
		};

		v3 velocity_ending(int newest) const; // averaged over velocity_window up to history index newest
		v3 release_velocity() const;

		grenade_config config_;
		bool last_off_grip_{};
		bool last_gun_grip_{};
		bool holding_{};
		bool armed_{};
		bool pin_held_{};
		v3 pin_start_;
		float clock_{};
		sample history_[history_size];
		int history_count_{};
		int history_next_{};
	};
}
