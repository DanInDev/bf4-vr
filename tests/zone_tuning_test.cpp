// Checks the per-gun grab-zone tuning (src/weapon/zone_tuning, bfvr_zones.ini): parsing, writing and clamping the file,
// the panel's right/up/forward in the gun frame, and that reload_hands moves and sizes the magazine, handle and lid
// zones by the frame's shifts.
#include <cmath>
#include <cstdio>

#include "weapon/reload.hpp"
#include "weapon/reload_hands.hpp"
#include "weapon/zone_tuning.hpp"

using namespace bfvr::weapon;
namespace tuning = bfvr::weapon::zone_tuning;

namespace
{
	int failures = 0;

	void expect(bool condition, const char* what)
	{
		if (!condition)
		{
			std::printf("FAIL: %s\n", what);
			++failures;
		}
	}

	bool close(float a, float b)
	{
		return std::fabs(a - b) < 1e-4f;
	}

	bool close(v3 a, v3 b)
	{
		return close(a.x, b.x) && close(a.y, b.y) && close(a.z, b.z);
	}

	void test_file()
	{
		tuning::book book;
		const auto warnings = tuning::parse(
			"# comment\n"
			"U_AK12 handle 1.5 -2 0.5 1.2\n"
			"\n"
			"U_AK12 magazine 0 0 0 0.8   # smaller\n"
			"U_M249 lid 40 0 0 9\n"
			"U_M249 trigger 0 0 0 1\n"
			"U_M249 lid 1 2\n",
			book);
		expect(warnings.size() == 2, "an unknown zone and a short line are warned about, the rest read");
		const tuning::tune handle = book["U_AK12"].zones[static_cast<int>(tuning::zone::handle)];
		expect(close(handle.right, 1.5f) && close(handle.up, -2.0f) && close(handle.forward, 0.5f) && close(handle.size, 1.2f), "the AK-12's handle read");
		expect(close(book["U_AK12"].zones[static_cast<int>(tuning::zone::magazine)].size, 0.8f), "a comment after the values is ignored");
		const tuning::tune lid = book["U_M249"].zones[static_cast<int>(tuning::zone::lid)];
		expect(close(lid.right, tuning::max_shift_cm) && close(lid.size, tuning::max_size), "out-of-range values are clamped");
		expect(tuning::is_default(book["U_AK12"].zones[static_cast<int>(tuning::zone::lid)]), "zones not in the file stay as built");

		tuning::book again;
		expect(tuning::parse(tuning::format(book), again).empty(), "the written file reads back without warnings");
		const tuning::tune back = again["U_AK12"].zones[static_cast<int>(tuning::zone::handle)];
		expect(close(back.right, 1.5f) && close(back.up, -2.0f) && close(back.forward, 0.5f) && close(back.size, 1.2f), "the values survive a save and load");
		expect(tuning::format(tuning::book{ { "U_P90", {} } }).find("U_P90") == std::string::npos, "an untouched gun writes no line");
		expect(tuning::zone_named("magazine") == tuning::zone::magazine && tuning::zone_named("off") == tuning::zone::count, "zone names");
	}

	void test_gun_frame()
	{
		const tuning::shift s = tuning::in_gun_frame({ 2.0f, 3.0f, -4.0f, 1.0f });
		// Wep_Root: +x is the gun's left, so right is -x; up +y; forward along the barrel +z; centimetres to metres.
		expect(close(s.x, -0.02f) && close(s.y, 0.03f) && close(s.z, -0.04f), "right/up/forward in the gun frame, metres");
	}

	constexpr v3 magwell{ 0.0f, -0.2f, -0.4f };

	// The gun frame at the magwell, turned like the game's (Wep_Root +z along the barrel, here world -z).
	hand_frame at(v3 hand)
	{
		hand_frame frame;
		frame.seconds = 1.0f / 90.0f;
		frame.hands_valid = true;
		frame.off_hand = hand;
		frame.gun_hand = { 0.1f, -0.2f, -0.3f };
		frame.magwell = magwell;
		frame.has_root = true;
		frame.root = magwell;
		frame.root_rotation = { 0.0f, 1.0f, 0.0f, 0.0f };
		frame.chest = { 0.0f, -0.35f, -0.1f };
		return frame;
	}

	void test_zones_move()
	{
		reload gun({ variant::ar, 30, true });
		gun.load(30, 60);
		reload_hands hands;
		const hand_config config;
		// As built: the magazine zone is straight below the magwell, the handle at the AR's point.
		hand_result built = hands.update(gun, at({ 0.5f, 0.5f, 0.5f }));
		const v3 magazine = built.zone_centre[0];
		const v3 handle = built.zone_centre[1];
		expect(close(built.zone_reach[0], config.reach_magazine), "as built: the magazine zone's reach");

		hand_frame tuned = at({ 0.5f, 0.5f, 0.5f });
		const tuning::shift right = tuning::in_gun_frame({ 5.0f, 0.0f, 0.0f, 1.0f });
		tuned.magazine_shift = { right.x, right.y, right.z };
		tuned.magazine_size = 1.5f;
		const tuning::shift up = tuning::in_gun_frame({ 0.0f, 2.0f, 3.0f, 1.0f });
		tuned.handle_shift = { up.x, up.y, up.z };
		tuned.handle_size = 2.0f;
		reload_hands moved;
		const hand_result r = moved.update(gun, tuned);
		// The gun frame is turned 180 degrees about y: its -x (the gun's right) is world +x, its +z world -z.
		expect(close(r.zone_centre[0], { magazine.x + 0.05f, magazine.y, magazine.z }), "magazine 5 cm right: the zone moves 5 cm to the gun's right");
		expect(close(r.zone_reach[0], config.reach_magazine * 1.5f), "magazine size 1.5: the reach 1.5 times");
		expect(close(r.zone_centre[1], { handle.x, handle.y + 0.02f, handle.z - 0.03f }), "handle 2 up, 3 forward: the zone moves with it");
		expect(close(r.zone_reach[1], built.zone_reach[1] * 2.0f), "handle size 2: twice the reach");

		// A grab at the moved zone takes the magazine; the same place as built is outside it.
		const v3 grab_at{ magazine.x + 0.05f + 0.11f, magazine.y, magazine.z };
		reload_hands plain;
		expect(plain.update(gun, at(grab_at)).in_zone != hand_result::zone::magazine, "as built: 16 cm to the right is outside the magazine zone");
		hand_frame there = at(grab_at);
		there.magazine_shift = tuned.magazine_shift;
		there.magazine_size = 1.5f;
		reload_hands tuned_hands;
		expect(tuned_hands.update(gun, there).in_zone == hand_result::zone::magazine, "tuned 5 cm right and 1.5x: the hand there is in it");
	}

	void test_lid_zone()
	{
		reload_profile belt{ variant::ar, 100, true };
		belt.belt = true;
		reload gun(belt);
		gun.load(100, 200);
		hand_frame frame = at({ 0.5f, 0.5f, 0.5f });
		frame.belt = true;
		frame.has_belt_points = true;
		frame.lid_hinge = { 0.0f, -0.1f, -0.5f };
		frame.lid_grab = { 0.0f, -0.08f, -0.35f };
		frame.feed_tray = { 0.0f, -0.12f, -0.4f };
		frame.belt_end = { 0.05f, -0.18f, -0.4f };
		reload_hands hands;
		const hand_result built = hands.update(gun, frame);
		expect(built.has_lid_zone && close(built.lid_centre, frame.lid_grab) && close(built.lid_reach, hand_config{}.lid_reach), "the lid zone reported as built");
		const tuning::shift back = tuning::in_gun_frame({ 0.0f, 0.0f, -4.0f, 1.0f });
		frame.lid_shift = { back.x, back.y, back.z };
		frame.lid_size = 0.5f;
		reload_hands tuned;
		const hand_result r = tuned.update(gun, frame);
		expect(close(r.lid_centre, { frame.lid_grab.x, frame.lid_grab.y, frame.lid_grab.z + 0.04f }) && close(r.lid_reach, hand_config{}.lid_reach * 0.5f),
			"lid 4 cm back and half size: moved towards the stock, smaller");
	}
}

int main()
{
	test_file();
	test_gun_frame();
	test_zones_move();
	test_lid_zone();
	if (failures)
	{
		std::printf("%d failures\n", failures);
		return 1;
	}
	std::printf("zone tuning: all passed\n");
	return 0;
}
