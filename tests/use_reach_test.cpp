// Checks use by reaching (src/weapon/use_reach): an off-hand grip reaching out ahead holds the
// interact key for as long as the grip is held; near the gun, raised (squad signal), close to the
// body, behind, busy or untracked it does not; the pose follows the head's yaw.
#include <cmath>
#include <cstdio>

#include "weapon/use_reach.hpp"

using namespace bfvr;
using namespace bfvr::weapon;

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

	use_frame reach(v3 off_hand, bool grip)
	{
		use_frame frame;
		frame.head_valid = true;
		frame.head = { 0, 1.6f, 0 };
		frame.hand_valid = true;
		frame.off_hand = off_hand;
		frame.gun_hand_valid = true;
		frame.gun_hand = { 0.2f, 1.3f, -0.3f };
		frame.off_grip = grip;
		return frame;
	}

	const v3 out_ahead{ -0.1f, 1.35f, -0.55f };

	void test_use_held_with_the_grip()
	{
		use_reach use;
		expect(!use.update(reach(out_ahead, false)).use, "reaching without a grip does not use");
		expect(use.update(reach(out_ahead, false)).reaching, "the reach pose claims the grip");
		expect(use.update(reach(out_ahead, true)).use, "grip squeezed out ahead: use");
		expect(use.update(reach({ -0.1f, 1.35f, -0.2f }, true)).use, "pulling back while gripping keeps using (a door)");
		expect(!use.update(reach({ -0.1f, 1.35f, -0.2f }, false)).use, "releasing the grip stops");
		expect(!use.update(reach({ -0.1f, 1.35f, -0.2f }, true)).use, "squeezing close to the body does not use");
		expect(!use.update(reach(out_ahead, true)).use, "a grip carried into the pose is not a press");
		use.update(reach(out_ahead, false));
		expect(use.update(reach(out_ahead, true)).use, "a fresh squeeze uses again");
	}

	void test_refusals()
	{
		const auto press = [](use_frame frame)
		{
			use_reach use;
			const bool grip = frame.off_grip;
			frame.off_grip = false;
			use.update(frame);
			frame.off_grip = grip;
			return use.update(frame).use;
		};
		expect(!press(reach({ 0.1f, 1.3f, -0.55f }, true)), "near the gun hand (the handguard): two-handed, not use");
		expect(!press(reach({ -0.1f, 1.75f, -0.55f }, true)), "raised above the eyes: the squad signal, not use");
		expect(!press(reach({ -0.1f, 1.3f, -0.2f }, true)), "close to the body (pouch, belt): not use");
		expect(!press(reach({ -0.1f, 1.35f, 0.5f }, true)), "behind the head: not use");
		expect(!press(reach({ -0.1f, 0.7f, -0.5f }, true)), "far below (picking at the floor): not use");
		use_frame busy = reach(out_ahead, true);
		busy.busy = true;
		expect(!press(busy), "a busy off hand (magazine, grenade, panel) does not use");
		use_frame lost = reach(out_ahead, true);
		lost.hand_valid = false;
		expect(!press(lost), "untracked hand does not use");
		use_frame no_gun = reach({ 0.15f, 1.3f, -0.5f }, true);
		no_gun.gun_hand_valid = false;
		expect(press(no_gun), "without a tracked gun hand only the reach counts");
	}

	void test_follows_the_head()
	{
		// Facing left (+90 degrees about y): ahead is -x.
		use_reach use;
		use_frame frame = reach({ -0.55f, 1.35f, 0.05f }, false);
		frame.head_yaw = 1.5707963f;
		frame.gun_hand = { -0.3f, 1.3f, -0.2f };
		use.update(frame);
		frame.off_grip = true;
		expect(use.update(frame).use, "head turned left: reaching left uses");

		use_reach old_forward;
		use_frame ahead = reach(out_ahead, false);
		ahead.head_yaw = 1.5707963f;
		ahead.gun_hand = { -0.3f, 1.3f, -0.2f };
		old_forward.update(ahead);
		ahead.off_grip = true;
		expect(!old_forward.update(ahead).use, "head turned left: the old forward is beside you");
	}
}

int main()
{
	test_use_held_with_the_grip();
	test_refusals();
	test_follows_the_head();
	if (failures)
	{
		std::printf("%d failure(s)\n", failures);
		return 1;
	}
	std::printf("use_reach: all checks passed\n");
	return 0;
}
