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
	};

	// Stage 1: shows the game's back buffer on a virtual screen in the headset.
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
