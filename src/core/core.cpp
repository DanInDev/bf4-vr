#include "core/core.hpp"

#include <windows.h>

#include <atomic>
#include <cstdio>
#include <string>

#include "body/arm_pose.hpp"
#include "common/config.hpp"
#include "common/log.hpp"
#include "common/settings_store.hpp"
#include "control/control_pipe.hpp"
#include "control/crash_log.hpp"
#include "hooks/dxgi_probe.hpp"
#include "hooks/game_window.hpp"
#include "hooks/nvapi_hook.hpp"
#include "hooks/present_hook.hpp"
#include "hooks/swap_chain_size.hpp"
#include "hooks/xinput_hook.hpp"
#include "input/gamepad.hpp"
#include "input/handedness.hpp"
#include "input/virtual_pad.hpp"
#include "input/vr_controls.hpp"
#include "safety/mp_guard.hpp"
#include "stereo/camera_hook.hpp"
#include "stereo/eye_pass.hpp"
#include "stereo/hud_capture.hpp"
#include "stereo/world_pass.hpp"
#include "weapon/physical_grenade.hpp"
#include "weapon/physical_reload.hpp"
#include "weapon/reload_sounds.hpp"
#include "weapon/weapon_swap.hpp"
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

		// Live settings (common/settings_store.hpp) pushed into the modules when they changed.
		void apply_settings()
		{
			static std::uint64_t applied = ~0ull;
			static std::string stereo_render, right_stick;
			settings::registry& store = settings::global();
			const std::uint64_t revision = store.revision();
			if (revision == applied)
			{
				return;
			}
			const bool first = applied == ~0ull;
			applied = revision;
			stereo::set_gun_scale(store.real("gun_scale", 1.0f));
			stereo::set_view_crop(store.real("view_crop_h", 1.0f), store.real("view_crop_v", 1.0f));
			stereo::set_ipd_scale(store.real("ipd_scale", 1.0f));
			body::set_arm_ik(store.boolean("arm_ik", true));
			if (const auto shoulders = store.get("shoulder_offset"))
			{
				body::set_shoulder_offset(shoulders->number[0], shoulders->number[1], shoulders->number[2]);
			}
			input::set_neutral_pitch(store.boolean("neutral_pitch", false));
			input::set_controller_aim(store.text("aim", "head") == "controller");
			weapon::set_physical_reload(store.text("reload", "physical") == "physical");
			weapon::set_drop_discards_ammo(store.boolean("drop_discards_ammo", true));
			weapon::set_accessory_touch(store.boolean("accessory_touch", true));
			weapon::set_holsters(store.boolean("holsters", true));
			weapon::set_sounds(store.boolean("reload_sounds", true));
			input::set_game_buttons(store.boolean("grips_to_game", false), store.boolean("ads_trigger", false), store.boolean("x_menu", true));
			body::set_arm_shape(store.real("arm_length", 1.15f), store.real("shoulder_width", 0.0f));
			body::set_legs_hidden(store.boolean("hide_legs", true));
			weapon::set_squad_signal(store.boolean("squad_signal", true));
			weapon::set_physical_grenades(store.text("grenade", "physical") == "physical");
			weapon::set_grenade_throw_scale(store.real("grenade_throw_scale", 1.5f));
			input::set_left_handed(store.text("dominant_hand", "right") == "left");
			input::configure_turn(store.text("turn", "snap"), store.integer("snap_turn_degrees", 30), store.boolean("stance_flicks", true));
			// Modes are switched only on a change: start-up already applied the ini values.
			const std::string render = store.text("stereo_render", "aer");
			if (!first && render != stereo_render)
			{
				stereo::set_stereo_render(render);
			}
			stereo_render = render;
			const std::string stick = store.text("right_stick", "turn");
			if (!first && stick != right_stick)
			{
				input::set_right_stick_mode(stick);
			}
			right_stick = stick;
		}

		void on_present(IDXGISwapChain* swap_chain)
		{
			present_thread = GetCurrentThreadId();
			apply_settings();
			weapon::update_physical_reload();
			weapon::update_physical_grenades();
			weapon::update_weapon_swap();
			if (control_enabled)
			{
				control::on_present(swap_chain);
			}
			// Before the theater: it shows the HUD captured for this frame.
			stereo::hud_capture_on_present(swap_chain);
			stereo::eye_pass_on_present(swap_chain);
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
			return std::string(text) + "\n" + hooks::render_size_report() + " focus_losses_hidden=" + std::to_string(hooks::focus_losses_hidden())
				+ "\neyepass " + stereo::eye_pass_report() + "\nhud captured=" + std::to_string(stereo::latest_hud().frame)
				+ " ui_on_screen=" + (stereo::ui_on_screen() ? "1" : "0");
		}

		DWORD WINAPI start_thread(LPVOID)
		{
			if (!safety::command_line_allows_mod())
			{
				return 0;
			}
			safety::start_multiplayer_guard();
			settings::load_global();
			if (safety::mod_disabled())
			{
				return 0; // no guard, no hooks
			}

			// First, so it sees the game's earliest NVAPI queries.
			if (config::get_bool("nvapi_probe", false))
			{
				hooks::install_nvapi_probe();
			}
			// Before the game's first swap chain and before the Present hook's probe swap chain.
			hooks::install_swap_chain_hooks();
			if (config::get_bool("dxgi_probe", false))
			{
				hooks::install_dxgi_stereo_probe();
			}

			// Camera hook for stereo; idle until an eye offset is set.
			if (config::get_bool("camera_hook", true))
			{
				stereo::install_camera_hook();
			}
			if (config::research_enabled())
			{
				control::install_crash_log();
				stereo::install_view_experiment();
			}
			stereo::install_hud_capture();
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
				input::install_vr_controls();
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
