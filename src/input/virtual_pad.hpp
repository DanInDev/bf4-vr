#pragma once

#include <windows.h>
#include <unknwn.h>

#include <cstdint>

namespace bfvr::input
{
	// BF4 enumerates DirectInput game controllers once at start-up and only creates an XInput pad
	// object for devices its IsXInputDevice check accepts; without a physical pad it never polls
	// XInput. The DirectInput8Create proxy calls this with the game's result: for the game's own
	// IDirectInput8A it replaces *output with a wrapper whose EnumDevices also offers a fake
	// "bf-vr virtual pad", and IsXInputDevice is hooked to accept it (re/notes/gamepad.md).
	// Does nothing unless virtual_pad, gamepad and theater are enabled.
	void wrap_direct_input(REFIID interface_id, LPUNKNOWN outer, const void* caller, void** output);

	struct virtual_pad_stats
	{
		std::uint64_t offered{};       // fake devices passed to the game's EnumDevices callback
		std::uint64_t xinput_answers{}; // IsXInputDevice calls answered TRUE for the fake device
	};
	virtual_pad_stats virtual_pad_counts();
}
