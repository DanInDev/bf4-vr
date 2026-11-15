#pragma once

#include <cstdint>

// #70 (owner 2026-11-15): the game picks the target of some actions along its own aim, which stays with the body
// in VR (#37): the squad order (Q) found "no targets" unless the body faced them, a melee strike went where the
// body faced, and the C4 flew that way. For such an action the game's aim is turned first (to where the head
// looks, or along the throwing hand), and the button follows a moment later so the game reads the new aim; the
// aim stays a little after the button lets go. Engine-free; times in milliseconds. Tested by
// tests/action_aim_test.cpp.
namespace bfvr::weapon
{
	struct action_aim_config
	{
		std::uint64_t lead_ms{ 100 }; // aim turned this long before the button goes down
		std::uint64_t tail_ms{ 150 }; // and kept this long after it lets go
	};

	class action_aim
	{
	public:
		explicit action_aim(const action_aim_config& config = {}) : config_(config) {}
		// An action starts now: its button goes down after the lead and is held hold_ms. false while one runs.
		bool start(std::uint64_t now, std::uint64_t hold_ms);
		// The action's button is down at this time.
		bool button(std::uint64_t now) const;
		// The game's aim belongs to the action at this time (lead, button, tail).
		bool aiming(std::uint64_t now) const;
		// Ended (the tail is over): free for the next one.
		bool done(std::uint64_t now) const { return !active_ || now >= until_; }

	private:
		action_aim_config config_;
		bool active_{};
		std::uint64_t down_at_{};
		std::uint64_t up_at_{};
		std::uint64_t until_{};
	};
}
