#pragma once

#include "weapon/reload_hands.hpp"

// Free hands (#35, setting free_hands, default on): the gun is held only while the gun hand squeezes
// its grip. Let go for release_seconds and it drops to the floor below the hand, lying there (drawn by
// body/arm_pose) while the game keeps the weapon; the trigger does nothing. Reach down to it and grip
// to pick it up again. Drawing another weapon (holsters) or the game switching weapons ends the drop.
// Empty hands (#38): letting go with the gun hand while the off hand holds the handguard hands the gun to
// the off hand instead (held there, the trigger does nothing); gripping again at the pistol grip takes it
// back; the off hand letting go drops it.
// Engine-free; OpenXR LOCAL metres. Tested by tests/free_hands_test.cpp.
namespace bfvr::weapon
{
	struct free_hands_frame
	{
		float seconds{};      // since the last update
		bool enabled{};
		bool gun_in_hand{};   // a primary or secondary is drawn (the only items that can drop)
		bool weapon_changed{}; // a different weapon is now in hand
		bool holster_take{};  // #49: the gun hand gripped at this gun's own holster: on the sling, it comes to the hand
		bool hand_valid{};
		v3 gun_hand;          // the gun hand's palm (grip point)
		bool grip{};          // gun-hand grip held
		bool busy{};          // the grip belongs to something else now (holster draw, grenade pin, panel)
		float floor_y{};      // the floor's height, same space
		float hand_yaw{};     // the gun hand's yaw (atan2(-forward.x, -forward.z)): the dropped gun lies along it
		// Empty hands (#38): the off hand on the handguard with its grip closed (two-handed hold), its grip,
		// palm and yaw, and where the gun's pistol grip is now (the gun hand regrips there).
		bool off_holds{};
		bool off_grip{};
		bool off_valid{};
		bool off_busy{};      // the off-hand grip belongs to something else (grenade, reload, holster)
		v3 off_hand;
		float off_yaw{};
		bool has_grip_point{};
		v3 grip_point;
		// The sling (#38, setting sling): a gun that leaves both hands hangs at sling_at (follows the body)
		// instead of lying on the floor; the gun hand grabs it back there.
		bool sling{};
		bool sling_valid{};
		v3 sling_at;
		float sling_yaw{};
	};

	enum class free_hands_event
	{
		none,
		dropped,
		picked_up,
		handed_to_off, // the off hand holds the gun now
		taken_back,    // the gun hand holds it again
		picked_up_off, // the off hand picked it up by the pistol grip (it fires with the off-hand trigger)
	};

	struct free_hands_result
	{
		free_hands_event event{ free_hands_event::none };
		bool dropped{};
		v3 position;    // where the dropped gun lies (its Wep_Root), when dropped
		float yaw{};    // and which way its barrel points
		bool block_fire{};
		bool in_reach{}; // dropped and the hand is close enough to pick it up (a ring, a tick)
		bool in_off_hand{}; // the off hand holds the gun by the handguard (the gun hand is free)
		bool grip_in_reach{}; // in_off_hand and the gun hand is at the pistol grip (a ring, a tick)
		bool slung{};         // dropped onto the sling: position and yaw follow the body
		bool in_off_grip{};   // the off hand holds the gun by the pistol grip (picked up with the off hand)
		bool off_in_reach{};  // dropped and the off hand is close enough to pick it up
	};

	struct free_hands_config
	{
		// Grip open this long before the gun falls or goes to the off hand (a regrip within it is free). Owner
		// 2026-11-15: no wait at all, the hand is empty the moment it opens (0.35, then 0.15 s felt like a delay).
		float release_seconds{ 0.0f };
		float pickup_reach{ 0.30f };    // the palm this close to the dropped gun picks it up
		float lie_height{ 0.04f };      // the gun's root above the floor when lying on its side
		float regrip_reach{ 0.12f };    // the gun hand's palm this close to the pistol grip takes the gun back
		float sling_reach{ 0.15f };     // and this close to the slung gun takes it (the grenade pouch is ~23 cm away)
	};

	class free_hands
	{
	public:
		explicit free_hands(const free_hands_config& config = {}) : config_(config) {}
		free_hands_result update(const free_hands_frame& frame);
		void reset();
		const free_hands_config& config() const { return config_; }
		// Live setting free_hands_release (seconds, clamped 0..0.5).
		void set_release_seconds(float seconds);

	private:
		free_hands_config config_;
		bool dropped_{};
		bool slung_{};
		bool in_off_hand_{};
		bool in_off_grip_{};
		bool last_off_grip_{ true };
		float off_open_for_{};
		v3 position_;
		float yaw_{};
		float open_for_{};
		bool last_grip_{ true };
	};
}
