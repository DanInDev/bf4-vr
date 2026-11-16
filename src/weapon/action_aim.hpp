#pragma once

#include <cstdint>

// #70 (owner 2026-11-15): the game picks the target of some actions along its own aim, which stays with the body
// in VR (#37): the squad order (Q) found "no targets" unless the body faced them, a melee strike went where the
// body faced, and the C4 flew that way. For such an action the game's aim is turned first (to where the head
// looks, or along the throwing hand), and the button follows a moment later so the game reads the new aim; the
// aim stays a little after the button lets go. The button is driven by press and release events, so a slow
// frame (or a stall) delays them but never skips them. Engine-free; times in milliseconds. Tested by
// tests/action_aim_test.cpp.
namespace bfvr::weapon
{
	struct action_aim_config
	{
		std::uint64_t lead_ms{ 100 }; // aim turned this long before the button goes down
		std::uint64_t tail_ms{ 150 }; // and kept this long after it lets go
	};

	enum class action_button
	{
		none,
		press,   // the button goes down now
		release, // the button lets go now
	};

	class action_aim
	{
	public:
		explicit action_aim(const action_aim_config& config = {}) : config_(config) {}
		// An action starts now: the button goes down after the lead and is held at least hold_ms. false while one runs.
		bool start(std::uint64_t now, std::uint64_t hold_ms);
		// Each frame: what the button does now (at most one event per call). A press is never skipped; the release
		// comes no sooner than hold_ms after the press.
		action_button update(std::uint64_t now);
		// The button is down (between the press and the release events).
		bool down() const { return pressed_ && !released_; }
		// The game's aim belongs to the action at this time (from the start, through the button, to the tail).
		bool aiming(std::uint64_t now) const;
		// Ended (released and the tail over): free for the next one.
		bool done(std::uint64_t now) const { return !active_ || (released_ && now >= released_at_ + config_.tail_ms); }

	private:
		action_aim_config config_;
		bool active_{};
		bool pressed_{};
		bool released_{};
		std::uint64_t down_at_{};
		std::uint64_t hold_ms_{};
		std::uint64_t pressed_at_{};
		std::uint64_t released_at_{};
	};
}
