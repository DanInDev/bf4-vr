// Checks the release gate (src/input/release_gate, #72): while a system menu has the controllers the game gets
// a neutral pad, and afterwards a trigger, grip or button that still reads pressed stays closed until released.
#include <cstdio>

#include "input/release_gate.hpp"

using namespace bfvr::input;

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

	constexpr std::uint16_t right_grip = 0x0200; // XINPUT_GAMEPAD_RIGHT_SHOULDER
	constexpr std::uint16_t left_grip = 0x0100;
	constexpr std::uint16_t a_button = 0x1000;

	gamepad_state pad(std::uint16_t buttons, std::uint8_t lt, std::uint8_t rt, std::int16_t lx = 0, std::int16_t ry = 0)
	{
		gamepad_state p{};
		p.connected = true;
		p.buttons = buttons;
		p.left_trigger = lt;
		p.right_trigger = rt;
		p.left_x = lx;
		p.right_y = ry;
		return p;
	}
}

int main()
{
	{
		release_gate gate;
		const gamepad_state firing = pad(right_grip, 0, 255);
		const gamepad_state out = gate.apply(firing, true);
		expect(out.right_trigger == 255 && out.buttons == right_grip, "with the controllers ours, everything passes");
		expect(!gate.holding() && gate.times_closed() == 0, "nothing closed before a focus loss");
	}
	{
		// The trigger and the right grip (an armed grenade, the gun) are held when the dashboard opens; SteamVR swallows
		// the trigger's release and reports it held after; the left grip is squeezed in the menu and still held on return.
		release_gate gate;
		gate.apply(pad(right_grip, 0, 255), true);
		const gamepad_state away = gate.apply(pad(right_grip | left_grip | a_button, 255, 255, 20000, -30000), false);
		expect(away.connected && away.left_trigger == 0 && away.right_trigger == 0 && away.left_x == 0 && away.right_y == 0,
			"while the runtime has the controllers: no trigger, no stick, still connected");
		expect(away.buttons == right_grip, "the grip held when the menu opened stays held (no grenade dropped live)");
		expect(gate.times_closed() == 1, "one closure counted");
		gate.apply(pad(0, 0, 0), false);
		expect(gate.times_closed() == 1, "a long focus loss is one closure");
		expect(gate.apply(pad(0, 0, 0), false).buttons == right_grip, "the kept grip stays held while the runtime reads neutral");
		const gamepad_state back = gate.apply(pad(right_grip | left_grip, 200, 255, 20000, 0), true);
		expect(back.right_trigger == 0 && back.left_trigger == 0, "a trigger still reading pressed after the menu stays closed (no endless fire)");
		expect(back.buttons == right_grip, "the kept grip, still squeezed, holds on; the grip squeezed in the menu stays closed");
		expect(back.left_x == 0, "a stick still pushed stays closed");
		expect(gate.holding(), "the gate reports what it holds");
		const gamepad_state still = gate.apply(pad(right_grip | left_grip, 200, 255, 20000, 0), true);
		expect(still.right_trigger == 0 && still.buttons == right_grip, "and stays closed while the input reads pressed");
		const gamepad_state released = gate.apply(pad(right_grip | left_grip, 200, 10, 20000, 0), true);
		expect(released.right_trigger == 10 && !gate.closed_right_trigger(), "the right trigger let out opens it");
		expect(released.buttons == right_grip && released.left_trigger == 0 && released.left_x == 0, "the others stay closed until their own release");
		const gamepad_state pulled = gate.apply(pad(right_grip | left_grip, 200, 255, 20000, 0), true);
		expect(pulled.right_trigger == 255, "a fresh pull fires again");
		gate.apply(pad(0, 0, 255, 0, 0), true);
		const gamepad_state open = gate.apply(pad(right_grip | left_grip, 255, 255, 20000, 0), true);
		expect(open.buttons == (right_grip | left_grip) && open.left_trigger == 255 && open.left_x == 20000, "all open after each was released once");
		expect(!gate.holding(), "nothing held any more");
	}
	{
		// The kept grip let go after the return lets go in the game too, and a new squeeze takes again.
		release_gate gate;
		gate.apply(pad(right_grip, 0, 0), true);
		gate.apply(pad(0, 0, 0), false);
		expect(gate.apply(pad(0, 0, 0), true).buttons == 0, "kept grip released after the return: released");
		expect(gate.apply(pad(right_grip, 0, 0), true).buttons == right_grip, "squeezed again: held");
		gate.apply(pad(0, 0, 0), false);
		gate.apply(pad(0, 0, 0), true);
		expect(gate.apply(pad(0, 0, 0), false).buttons == 0, "a later menu keeps only what was held at its own start");
	}
	{
		// A button released in the menu but another still held when coming back.
		release_gate gate;
		gate.apply(pad(0, 0, 0), false);
		const gamepad_state back = gate.apply(pad(a_button, 0, 0), true);
		expect(back.buttons == 0, "A held across the menu stays closed");
		const gamepad_state grip = gate.apply(pad(a_button | right_grip, 0, 0), true);
		expect(grip.buttons == right_grip, "a grip released before the return and pressed after passes; A stays closed");
		gate.apply(pad(right_grip, 0, 0), true);
		expect(gate.apply(pad(a_button | right_grip, 0, 0), true).buttons == (a_button | right_grip), "A opens after its release");
	}
	{
		freeze_watch watch(0.5f);
		expect(!watch.update(true, true, 0.011f), "a moving controller is alive");
		bool frozen = false;
		for (int i = 0; i < 40; ++i)
		{
			frozen = watch.update(true, false, 0.011f);
		}
		expect(!frozen, "0.44 s of identical poses is not yet a freeze");
		for (int i = 0; i < 10; ++i)
		{
			frozen = watch.update(true, false, 0.011f);
		}
		expect(frozen && watch.frozen(), "0.5 s of bit-identical poses is a frozen stream");
		expect(!watch.update(true, true, 0.011f), "one new pose ends it");
		for (int i = 0; i < 100; ++i)
		{
			frozen = watch.update(false, false, 0.011f);
		}
		expect(!frozen, "untracked controllers are not a frozen stream");
	}
	if (failures == 0)
	{
		std::printf("release_gate: all checks passed\n");
	}
	return failures == 0 ? 0 : 1;
}
