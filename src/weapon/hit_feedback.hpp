#pragma once

#include <string>

// Damage felt in the hands (#32): the local soldier's health (BF4 179547: soldier +0x140 health
// component, +0x20 health, +0x24 max health) dropping gives a pulse on both controllers
// (input::hit_pulse), so hits and the blasts that hurt are felt even when the game's own rumble is
// gated off (no window focus, re/notes/haptics.md). x64 only.
namespace bfvr::weapon
{
	// Present thread, once per frame.
	void update_hit_feedback();
	// For the pipe's "haptics" report.
	std::string hit_feedback_report();
}
