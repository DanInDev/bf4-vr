#pragma once

#include "stereo/tracking.hpp"

// Two-handed weapon (issue #29): the off hand grabs the handguard and the barrel then points along
// the line between the hands; shots go where the drawn barrel points. Engine-free, OpenXR LOCAL
// metres; tested by tests/two_hand_test.cpp.
namespace bfvr::weapon
{
	struct two_hand_config
	{
		float grab_reach{ 0.12f };  // off hand this close to the barrel line grabs
		float grab_near{ 0.08f };   // the handguard starts this far ahead of the gun hand
		float grab_far{ 0.65f };    // and ends here
		float min_separation{ 0.08f }; // hands closer than this: the line between them says nothing
		float blend_time{ 0.12f };  // seconds to blend between one- and two-handed
		float trigger_press{ 0.6f };   // off-hand trigger: a press past this toggles the accessory
		float trigger_release{ 0.3f }; // and must fall below this before the next
	};

	struct two_hand_input
	{
		bool tracked{};         // both controllers tracked this frame
		stereo::pose gun;       // gun hand: position = grip, orientation = aim rotation (-z down the barrel)
		float off_hand[3]{};    // off-hand grip position
		bool grip{};            // off-hand grip held
		float trigger{};        // off-hand trigger, 0..1
		bool busy{};            // reload, grenade, accessory or holster owns the off hand
		float seconds{};
	};

	struct two_hand_output
	{
		bool held{};            // the off hand holds the gun
		float weight{};         // 0 one-handed .. 1 two-handed (blends after grab and release)
		float rotation[4]{ 0, 0, 0, 1 }; // gun-hand aim rotation to draw and fire with (x, y, z, w)
		bool claim_grip{};      // keep the off-hand grip from the game (pad LB is the grenade); also while
		                        // the off hand is on the handguard before the grip closes, so the
		                        // press that grabs never reaches the game
		// Off-hand trigger pressed while holding the gun: the game's accessory toggle (laser, light,
		// magnifier). A trigger already held at the grab does not count.
		bool toggle_accessory{};
		bool claim_trigger{};   // keep the off-hand trigger (pad LT, zoom) from the game while holding
	};

	class two_hand_grip
	{
	public:
		explicit two_hand_grip(const two_hand_config& config = {}) : config_(config) {}
		void configure(const two_hand_config& config) { config_ = config; }
		const two_hand_config& config() const { return config_; }
		two_hand_output update(const two_hand_input& input);
		void reset();

	private:
		two_hand_config config_;
		bool held_{};
		bool last_grip_{};
		bool trigger_down_{};
		float weight_{};
		float line_in_gun_[3]{};          // hand line at the grab, gun-hand axes
		float two_handed_[4]{ 0, 0, 0, 1 }; // last two-handed rotation (kept for the release blend)
	};

	// Whether the off hand is on the handguard: within reach of the barrel line, between grab_near
	// and grab_far ahead of the gun hand.
	bool on_handguard(const stereo::pose& gun, const float (&off_hand)[3], const two_hand_config& config);

	// Bullets leave the game camera (OpenXR LOCAL origin), not the gun. To hit what the barrel points
	// at, aim from origin at the point convergence metres down the barrel line. Unit vector.
	void converged_aim(const float (&origin)[3], const float (&grip)[3], const float (&rotation)[4], float convergence, float (&direction)[3]);
}
