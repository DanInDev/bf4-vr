#pragma once

#include <cstdint>

// #163: why a game frame was not head-tracked, and whether the theater keeps showing the last stereo pair
// through it. A menu, a loading screen or a scene on the flat screen drop to the flat screen at once; a frame
// without a head pose (tracking lost for a moment, a hitch on the waiter thread) keeps the last pair for a short
// while, as a compositor does: the flat screen flashing in for one frame is worse than one held frame.
// Engine-free; tested by tests/stereo_hold_test.cpp.
namespace bfvr::stereo
{
	enum class mono_reason : std::uint8_t
	{
		none,         // head-tracked (a stereo frame)
		off,          // stereo switched off
		menu,         // a menu camera or no campaign level loaded
		scene_screen, // a scripted scene shown on the flat screen (cutscene_view = screen)
		no_head,      // no valid head pose for this frame
	};

	const char* mono_reason_name(mono_reason reason);

	class stereo_hold
	{
	public:
		explicit stereo_hold(std::uint64_t hold_ms = 300) : hold_ms_(hold_ms) {}

		// A stereo pair (or eye) was stored.
		void on_stereo();
		// A mono frame arrived; true while the last pair stays shown, false once it has to go.
		bool keep_on_mono(mono_reason reason, std::uint64_t now_ms);

		std::uint64_t held_frames() const { return held_frames_; }
		std::uint64_t dropped(mono_reason reason) const { return dropped_[static_cast<int>(reason)]; }

	private:
		std::uint64_t hold_ms_;
		std::uint64_t since_ms_{};
		bool holding_{};
		bool dropped_now_{};
		std::uint64_t held_frames_{};
		std::uint64_t dropped_[5]{};
	};
}
