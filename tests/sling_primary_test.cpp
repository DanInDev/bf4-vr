// Checks when the primary is captured for its sling, when its copy is shown and when a capture is
// dropped (src/weapon/sling_primary, #58).
#include <cstdio>

#include "weapon/sling_primary.hpp"

using namespace bfvr::weapon;

namespace
{
	int failures = 0;
	constexpr float frame = 1.0f / 90.0f;

	void expect(bool condition, const char* what)
	{
		if (!condition)
		{
			std::printf("FAIL: %s\n", what);
			++failures;
		}
	}

	sling_primary_frame at_slot(int slot, const char* name, bool captured = false, unsigned attempts = 0)
	{
		sling_primary_frame f;
		f.enabled = true;
		f.slot_valid = true;
		f.slot = slot;
		f.name = name;
		f.seconds = frame;
		f.captured = captured;
		f.attempts = attempts;
		f.sling_valid = true;
		return f;
	}

	// Frames until the first capture request with the primary in hand, or -1.
	int frames_to_capture(sling_primary& s, const sling_primary_frame& f, int limit = 300)
	{
		for (int i = 1; i <= limit; ++i)
		{
			if (s.update(f).capture)
			{
				return i;
			}
		}
		return -1;
	}

	void test_capture_and_show()
	{
		sling_primary s;
		// Level start: the rifle in hand. No capture during the draw animation, then one.
		const int first = frames_to_capture(s, at_slot(0, "U_SCAR-H"));
		expect(first > 0 && first >= static_cast<int>(1.0f / frame) - 1 && first <= static_cast<int>(1.0f / frame) + 2, "capture after a second in hand");
		expect(!s.update(at_slot(0, "U_SCAR-H", true)).capture, "captured: no more captures");
		expect(!s.update(at_slot(0, "U_SCAR-H", true)).show, "the primary in hand: no copy (the game draws it)");
		// The pistol out: the copy hangs at the sling.
		const sling_primary_result pistol = s.update(at_slot(1, "U_M9", true));
		expect(pistol.show && !pistol.capture && !pistol.forget, "pistol out: the rifle shown on the sling");
		sling_primary_frame no_body = at_slot(1, "U_M9", true);
		no_body.sling_valid = false;
		expect(!s.update(no_body).show, "no body frame: nothing shown");
		sling_primary_frame off = at_slot(1, "U_M9", true);
		off.enabled = false;
		expect(!s.update(off).show, "setting off: nothing shown");
		// Back to the rifle: hidden again, no new capture.
		const sling_primary_result back = s.update(at_slot(0, "U_SCAR-H", true));
		expect(!back.show && !back.capture && !back.forget, "the rifle back in hand: no copy, no capture");
		// Research (pipe holster slingtest): the copy beside the rifle in hand, for a monitor check without a switch.
		sling_primary_frame test = at_slot(0, "U_SCAR-H", true);
		test.test_show = true;
		expect(s.update(test).show, "slingtest: the copy also with the rifle in hand");
		// Knife or grenade out: the copy too (it is drawn where the game's gun magazine is, see the brief).
		expect(s.update(at_slot(7, "U_Knife", true)).show, "knife out: shown (if anything draws it)");
	}

	void test_no_capture_yet()
	{
		sling_primary s;
		// The pistol drawn before the rifle was ever in hand for a second: nothing to show.
		expect(!s.update(at_slot(1, "U_M9")).show, "never captured: nothing on the sling");
		s.update(at_slot(0, "U_SCAR-H"));
		expect(!s.update(at_slot(1, "U_M9")).show, "a short glimpse of the rifle: still nothing");
	}

	void test_new_primary()
	{
		sling_primary s;
		frames_to_capture(s, at_slot(0, "U_SCAR-H"));
		s.update(at_slot(1, "U_M9", true));
		// A different rifle picked up into slot 0: the capture is dropped and taken again after a second.
		const sling_primary_result swapped = s.update(at_slot(0, "U_AK12", true));
		expect(swapped.forget && !swapped.capture, "another primary: forget the old capture");
		expect(s.primary_name() == "U_AK12", "remembers the new primary");
		const int again = frames_to_capture(s, at_slot(0, "U_AK12", false, 0));
		expect(again > 0, "the new primary is captured after it settles");
	}

	void test_attempts_limited()
	{
		sling_primary s;
		frames_to_capture(s, at_slot(0, "U_SCAR-H"));
		// Three rejected or empty captures: stop hiding the gun.
		expect(s.update(at_slot(0, "U_SCAR-H", false, 2)).capture, "a third attempt is allowed");
		expect(!s.update(at_slot(0, "U_SCAR-H", false, 3)).capture, "after three attempts: no more (the gun would flicker)");
		// A new primary starts over.
		s.update(at_slot(0, "U_AK12", false, 3));
		expect(frames_to_capture(s, at_slot(0, "U_AK12", false, 0)) > 0, "a new primary gets its own attempts");
		sling_primary_frame invalid = at_slot(0, "U_AK12");
		invalid.slot_valid = false;
		expect(!s.update(invalid).capture && !s.update(invalid).show, "no slot read: nothing");
		sling_primary_frame disabled = at_slot(0, "U_AK12");
		disabled.enabled = false;
		expect(frames_to_capture(s, disabled) == -1, "setting off: never hides the gun for a capture");
	}
}

int main()
{
	test_capture_and_show();
	test_no_capture_yet();
	test_new_primary();
	test_attempts_limited();
	if (failures)
	{
		std::printf("%d failure(s)\n", failures);
		return 1;
	}
	std::printf("sling_primary: all checks passed\n");
	return 0;
}
