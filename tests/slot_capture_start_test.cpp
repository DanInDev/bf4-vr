// Checks the level-start capture of the grenade and the knife for their slots (src/weapon/slot_capture_start, #41).
#include <cstdio>

#include "weapon/slot_capture_start.hpp"

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

	slot_capture_frame in_level(int slot = 0)
	{
		slot_capture_frame f;
		f.enabled = true;
		f.slot_valid = true;
		f.slot = slot;
		f.seconds = frame;
		return f;
	}

	// Steps until the first request, or none after limit seconds; returns it and the seconds taken.
	slot_capture_request first_request(slot_capture_start& s, const slot_capture_frame& f, float limit, float& took)
	{
		for (int i = 1; static_cast<float>(i) * frame <= limit; ++i)
		{
			const slot_capture_request r = s.update(f);
			if (r != slot_capture_request::none)
			{
				took = static_cast<float>(i) * frame;
				return r;
			}
		}
		took = -1;
		return slot_capture_request::none;
	}

	void test_level_start()
	{
		slot_capture_start s;
		float took{};
		slot_capture_frame f = in_level();
		expect(first_request(s, f, 5.0f, took) == slot_capture_request::grenade && took > 2.4f && took < 2.6f, "the grenade first, 2.5 s after the gun is in hand");
		f.grenade_captured = true;
		expect(first_request(s, f, 5.0f, took) == slot_capture_request::knife && took > 2.4f, "then the knife, once the gun is back and settled");
		f.knife_captured = true;
		expect(first_request(s, f, 10.0f, took) == slot_capture_request::none && !s.armed(), "both done: nothing more this level");
	}

	void test_waits()
	{
		slot_capture_start s;
		float took{};
		slot_capture_frame f = in_level();
		f.primary_pending = true;
		expect(first_request(s, f, 5.0f, took) == slot_capture_request::none, "the primary's own capture goes first");
		f.primary_pending = false;
		expect(first_request(s, f, 0.1f, took) == slot_capture_request::grenade, "then at once (settled meanwhile)");

		slot_capture_start busy;
		slot_capture_frame b = in_level();
		b.busy = true;
		expect(first_request(busy, b, 5.0f, took) == slot_capture_request::none, "never during a switch or a capture");
		slot_capture_frame knife_out = in_level(7);
		expect(first_request(busy, knife_out, 5.0f, took) == slot_capture_request::none, "only from a gun in hand");
		slot_capture_frame off = in_level();
		off.enabled = false;
		expect(first_request(busy, off, 5.0f, took) == slot_capture_request::none, "setting off: never");
	}

	void test_once_per_level()
	{
		slot_capture_start s;
		float took{};
		slot_capture_frame f = in_level();
		// Both tried but neither taken (no grenades left, the game refused): not again in this level.
		first_request(s, f, 5.0f, took);
		first_request(s, f, 5.0f, took);
		expect(first_request(s, f, 10.0f, took) == slot_capture_request::none, "tried once each: no loop of weapon switches");
		// A load screen (no soldier for 3 s) arms it again.
		slot_capture_frame loading = in_level();
		loading.slot_valid = false;
		for (int i = 0; i < 300; ++i)
		{
			s.update(loading);
		}
		expect(s.armed() && first_request(s, f, 5.0f, took) == slot_capture_request::grenade, "the next level tries again");
		// Already captured items are skipped.
		slot_capture_start known;
		slot_capture_frame have = in_level();
		have.grenade_captured = have.knife_captured = true;
		expect(first_request(known, have, 5.0f, took) == slot_capture_request::none, "both captured already: no switch at all");
		// A short gap (a switch reading no slot for a frame) does not re-arm.
		slot_capture_start brief;
		first_request(brief, f, 5.0f, took);
		first_request(brief, f, 5.0f, took);
		slot_capture_frame gap = in_level();
		gap.slot_valid = false;
		brief.update(gap);
		expect(first_request(brief, f, 5.0f, took) == slot_capture_request::none, "a one-frame gap is no new level");
	}
}

int main()
{
	test_level_start();
	test_waits();
	test_once_per_level();
	if (failures)
	{
		std::printf("%d failure(s)\n", failures);
		return 1;
	}
	std::printf("slot_capture_start: all checks passed\n");
	return 0;
}
