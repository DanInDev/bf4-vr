#pragma once

#include "stereo/tracking.hpp"

// Two-handed weapon (issue #29): the off hand grabs the handguard and the barrel then points along
// the line between the hands; shots go where the drawn barrel points. Engine-free, OpenXR LOCAL
// metres; tested by tests/two_hand_test.cpp.
namespace bfvr::weapon
{
	struct two_hand_config
	{
		float grab_reach{ 0.125f };  // off hand this close to the barrel line grabs
		float grab_near{ 0.05f };   // the handguard starts this far ahead of the gun hand
		float grab_far{ 0.75f };    // and ends here
		float min_separation{ 0.08f }; // hands closer than this: the line between them says nothing
		float blend_time{ 0.12f };  // seconds to blend between one- and two-handed (also eases the snap)
		float release_grace{ 0.25f }; // seconds of lost tracking before the hold lets go
		// lock: the barrel lies on the line between the hands (VR shooter); soft: the hand line at the
		// grab is remembered in gun axes and only its later turns steer the gun.
		bool lock{ true };
		float trigger_press{ 0.6f };   // off-hand trigger: a press past this toggles the accessory
		float trigger_release{ 0.3f }; // and must fall below this before the next
		// #99, a pistol or revolver (two_hand_input pistol): the off hand closing within cup_reach of the gun hand cups it;
		// the gun keeps the gun hand's aim, steadied by the off hand's own aim (pistol_share of it; as two hands on a
		// pistol, not a barrel line between hands a few cm apart).
		float cup_reach{ 0.15f };
		float pistol_share{ 0.5f };
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
		// Where the gun holds the off hand, off the barrel line through the gun hand: gun-hand aim axes
		// (x right, y up), metres. The hand slides freely along the barrel. Zero = on the line.
		float support_offset[2]{};
		// #99: a one-handed gun (the game holds it with both hands at the grip): the cup grip, see two_hand_config.
		bool pistol{};
		float off_orientation[4]{ 0, 0, 0, 1 }; // the off hand's aim rotation
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
		float untracked_{};               // seconds the hands have been lost while holding
		bool trigger_down_{};
		float weight_{};
		float line_in_gun_[3]{};          // hand line at the grab, gun-hand axes
		float two_handed_[4]{ 0, 0, 0, 1 }; // last two-handed rotation (kept for the release blend)
		float off_to_gun_[4]{ 0, 0, 0, 1 }; // #99: the gun hand's aim in the off hand's at the cup grab
		bool held_as_pistol_{};               // the running hold's kind: a change of kind lets go (never mixes the two)
		two_hand_output update_pistol(const two_hand_input& input);
	};

	// Whether the off hand is on the handguard: within reach of the barrel line, between grab_near
	// and grab_far ahead of the gun hand.
	bool on_handguard(const stereo::pose& gun, const float (&off_hand)[3], const two_hand_config& config);

	// Bullets leave the game camera (OpenXR LOCAL origin), not the gun. To hit what the barrel points
	// at, aim from origin at the point convergence metres down the barrel line. Unit vector.
	void converged_aim(const float (&origin)[3], const float (&grip)[3], const float (&rotation)[4], float convergence, float (&direction)[3]);

	// The drawn gun's barrel as a rotation for converged_aim (its -z along Wep_Root's +z barrel): shots
	// and the reticle follow the gun as it is drawn, not the controller's aim pose. With a sight
	// direction (unit, Wep_Root axes: body::weapon_points::sight_direction), the line is the sight's
	// instead of the bare barrel's (they differ by a degree or two: the barrel is zeroed upwards).
	void drawn_barrel_rotation(const float (&root_rotation)[4], float (&rotation)[4], const float* sight_direction = nullptr);

	// #39 (owner 2026-11-15: the AK-12's optic looked gutted, its top missing): the game's zoom mesh is the optic
	// as its own ADS camera sees it, open at the top. It is shown only while an eye looks into the lens from
	// behind: up to max_distance behind the lens (lens_ahead along the sight line from sight_eye) and within
	// tan_half_angle of the line counted from the lens, plus the lens radius. direction: unit.
	bool eye_in_lens_cone(const float (&eye)[3], const float (&sight_eye)[3], const float (&direction)[3], float lens_ahead, float tan_half_angle,
		float lens_radius, float max_distance);
	// An eye looks through the sight: within max_offset (metres) of the sight line, and from up to
	// max_behind behind the game's eye point (eye relief) to max_ahead in front of it (towards the optic:
	// in VR the eye can come closer than the game's camera sits). direction: unit.
	bool eye_behind_sight(const float (&eye)[3], const float (&sight_eye)[3], const float (&direction)[3], float max_offset, float max_behind,
		float max_ahead = 0.05f);
	// The eye's distance from the sight line (off) and how far along it from the sight's eye point (along:
	// + towards the muzzle, - behind), metres.
	void sight_offset(const float (&eye)[3], const float (&sight_eye)[3], const float (&direction)[3], float& off, float& along);

	// sight_dot (#39): a red dot collimated like a real reflex sight. The game draws its own dot along the
	// view's centre, which a head never holds on the barrel; this one sits at infinity along the sight
	// line, so an eye sees it only while that eye's ray parallel to the line passes through the lens:
	// within lens_radius of the line, from max_behind behind the sight's eye point up to max_ahead in
	// front of it. point: where to draw it, distance metres from the eye along the sight.
	struct sight_dot_view
	{
		bool visible{};
		float point[3]{};
	};
	sight_dot_view sight_dot_for_eye(const float (&eye)[3], const float (&sight_eye)[3], const float (&direction)[3], float lens_radius, float max_behind,
		float max_ahead, float distance);

	// sight_ads: aiming starts with an eye within the enter band and ends only after no eye has been
	// within the leave band for hold seconds, so hand shake and the drawn recoil do not flicker the
	// game's ADS (headset 2026-11-10: 82 changes in one session).
	class sight_ads_latch
	{
	public:
		bool update(bool within_enter, bool within_leave, float seconds, float hold);
		bool aiming() const { return aiming_; }
		void reset();

	private:
		bool aiming_{};
		float outside_{};
	};
}
