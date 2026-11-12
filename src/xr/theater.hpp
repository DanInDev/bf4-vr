#pragma once

#include <dxgi.h>

#include <cstdint>
#include <memory>

namespace bfvr::xr
{
	struct theater_status
	{
		const char* phase{};
		std::uint64_t presents_seen{};
		std::uint64_t frames_submitted{};
		int session_state{};
		// Worst single call during the last full second, in milliseconds.
		double max_sync_ms{};
		double max_frame_ms{};
		bool stereo{};
		std::uint64_t eye_images_stored{};
		// Step the game's frame is in inside the VR code, and for how long (diagnoses freezes).
		const char* stage{};
		double stage_ms{};
		bool menu_over_level{}; // a menu is up over the level: the flat screen is shown
		const char* session_state_name{};
		bool input_focus{};          // the session reads the controllers (FOCUSED)
		std::uint32_t focus_losses{}; // FOCUSED left for another state
		double unfocused_seconds{};  // since focus was last lost, while it is still lost
		std::uint64_t held_grenade_frames{}; // frames that showed the grenade in the off hand
		std::uint64_t sight_dot_frames{};    // frames that showed the sight's red dot (#39)
		// #34: the laser on the flat screen (menus over a level): frames it was tested, frames without the
		// pointing hand tracked, misses and hits, and where the last hit landed (texture coordinates).
		std::uint64_t pointer_frames{};
		std::uint64_t pointer_no_hand{};
		std::uint64_t pointer_misses{};
		std::uint64_t pointer_hits{};
		int pointer_hand{};
		float pointer_u{};
		float pointer_v{};
	};

	// The OpenXR side of the mod: the flat virtual screen (menus, cutscenes), the head-tracked
	// projection layer from the stored eye images, and the quad layers on top (HUD, scope lens,
	// vignette, reticle, held grenade, settings panel, pointer). docs/ARCHITECTURE.md "One frame".
	// The OpenXR session uses the game's own D3D11 device. Every call must come
	// from the thread that presents the swap chain.
	class theater
	{
	public:
		theater();
		~theater();

		theater(const theater&) = delete;
		theater& operator=(const theater&) = delete;

		void on_present(IDXGISwapChain* swap_chain);
		theater_status status() const;

	private:
		struct state;
		std::unique_ptr<state> state_;
	};
}
