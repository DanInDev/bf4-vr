#pragma once

// grab_debug = 1: every grab zone drawn as a ring at its centre, as large as its reach, so the hand
// can find it in the headset and a monitor screenshot shows whether the drawn gun sits where the
// gestures think it is. The weapon modules publish their zones each frame (OpenXR LOCAL, metres);
// the compositor draws the fresh ones.
namespace bfvr::weapon
{
	enum class grab_slot
	{
		magazine,
		handle,
		pouch,
		handguard,
		primary,
		secondary,
		knife,
		gadget1,
		gadget2,
		count,
	};

	struct grab_zone
	{
		bool valid{};
		bool hot{};         // a hand is inside it
		float centre[3]{};
		float radius{};
	};

	constexpr int grab_zone_count = static_cast<int>(grab_slot::count);

	void set_grab_debug(bool enabled);
	bool grab_debug_enabled();
	// From the module that owns the zone, once per update; a zone not refreshed for 250 ms is dropped.
	void publish_grab_zone(grab_slot slot, const float (&centre)[3], float radius, bool hot);
	void clear_grab_zone(grab_slot slot);
	// The fresh zones; returns how many are valid.
	int grab_zones(grab_zone (&zones)[grab_zone_count]);
}
