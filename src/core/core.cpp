#include "core/core.hpp"

#include <windows.h>

#include <atomic>
#include <cstdio>
#include <string>

#include "body/arm_pose.hpp"
#include "common/config.hpp"
#include "common/log.hpp"
#include "control/control_pipe.hpp"
#include "hooks/dxgi_probe.hpp"
#include "hooks/nvapi_hook.hpp"
#include "hooks/present_hook.hpp"
#include "hooks/xinput_hook.hpp"
#include "input/gamepad.hpp"
#include "input/virtual_pad.hpp"
#include "safety/mp_guard.hpp"
#include "stereo/camera_hook.hpp"
#include "xr/theater.hpp"

namespace bfvr::core
{
	namespace
	{
		// Owned for the life of the process and never destroyed: tearing down the VR runtime
		// during process exit can deadlock under the loader lock.
		xr::theater* theater{};
		bool control_enabled{};
		std::atomic<DWORD> present_thread{};

		void on_present(IDXGISwapChain* swap_chain)
		{
			present_thread = GetCurrentThreadId();
			if (control_enabled)
			{
				control::on_present(swap_chain);
			}
			if (theater)
			{
				theater->on_present(swap_chain);
			}
		}

		std::string status_text()
		{
			char text[768];
			const input::gamepad_state pad = input::latest();
			const xr::theater_status vr = theater ? theater->status() : xr::theater_status{ "disabled" };
			const stereo::camera_stats camera = stereo::stats();
			const input::virtual_pad_stats virtual_pad = input::virtual_pad_counts();
			std::snprintf(text, sizeof(text),
				"game=%s\ntheater=%s session_state=%d presents=%llu submitted=%llu max_sync_ms=%.2f max_frame_ms=%.2f stage=%s stage_ms=%.0f\n"
				"pad connected=%d buttons=%04X lt=%u rt=%u left=(%d,%d) right=(%d,%d)\n"
				"xinput get_state_calls=%llu get_capabilities_calls=%llu virtual_pad_offered=%llu virtual_pad_xinput=%llu\n"
				"camera copies=%llu copy_thread=%lu present_thread=%lu eye_offset=%.4f stereo=%d eye_images=%llu",
				BFVR_GAME_NAME, vr.phase, vr.session_state, vr.presents_seen, vr.frames_submitted, vr.max_sync_ms, vr.max_frame_ms,
				vr.stage ? vr.stage : "-", vr.stage_ms,
				pad.connected ? 1 : 0, pad.buttons,
				pad.left_trigger, pad.right_trigger, pad.left_x, pad.left_y, pad.right_x, pad.right_y,
				hooks::xinput_call_counts().state_calls, hooks::xinput_call_counts().capability_calls,
				virtual_pad.offered, virtual_pad.xinput_answers,
				camera.copies, camera.copy_thread, present_thread.load(), stereo::eye_offset(), vr.stereo ? 1 : 0, vr.eye_images_stored);
			return text;
		}

		DWORD WINAPI start_thread(LPVOID)
		{
			if (!safety::command_line_allows_mod())
			{
				return 0;
			}
			safety::start_multiplayer_guard();
			if (safety::mod_disabled())
			{
				return 0; // no guard, no hooks
			}

			// First, so it sees the game's earliest NVAPI queries.
			if (config::get_bool("nvapi_probe", false))
			{
				hooks::install_nvapi_probe();
			}
			if (config::get_bool("dxgi_probe", false))
			{
				hooks::install_dxgi_stereo_probe();
			}

			// Camera hook for stereo; idle until an eye offset is set.
			if (config::get_bool("camera_hook", true))
			{
				stereo::install_camera_hook();
			}
			// Arm pose hook: arm IK to the controllers, and the research bone edits.
			if (config::get_bool("arm_ik", true) || (config::research_enabled() && config::get_bool("arm_pose_hook", true)))
			{
				body::install_arm_pose_hook();
			}

			control_enabled = config::get_bool("control_pipe", true);
			if (control_enabled)
			{
				control::start(&status_text);
			}

			if (config::get_bool("theater", true))
			{
				theater = new xr::theater();
			}
			else
			{
				log::write("core: theater mode disabled (theater=0)");
			}

			if (!hooks::install_present_hook(&on_present))
			{
				log::write("core: Present hook unavailable; theater mode and screenshots inactive");
				return 0;
			}

			// The VR controllers reach the game as an Xbox pad through its own XInput calls.
			if (theater && config::get_bool("gamepad", true))
			{
				hooks::install_xinput_hooks();
			}
			return 0;
		}
	}

	void start()
	{
		// The new thread waits for the loader lock, so it runs only after DllMain returns.
		const HANDLE thread = CreateThread(nullptr, 0, &start_thread, nullptr, 0, nullptr);
		if (thread)
		{
			CloseHandle(thread);
		}
		else
		{
			log::write("core: could not start the runtime thread: %lu", GetLastError());
		}
	}
}
