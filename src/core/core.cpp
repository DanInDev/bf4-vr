#include "core/core.hpp"

#include <windows.h>

#include <atomic>
#include <cstdio>
#include <cstring>
#include <string>

#include "body/arm_pose.hpp"
#include "body/pose_book.hpp"
#include "common/config.hpp"
#include "common/log.hpp"
#include "common/settings_store.hpp"
#include "control/control_pipe.hpp"
#include "control/crash_log.hpp"
#include "hooks/dxgi_probe.hpp"
#include "hooks/game_window.hpp"
#include "hooks/nvapi_hook.hpp"
#include "hooks/present_hook.hpp"
#include "hooks/rumble_hook.hpp"
#include "hooks/swap_chain_size.hpp"
#include "hooks/xinput_hook.hpp"
#include "input/button_map.hpp"
#include "input/gamepad.hpp"
#include "input/handedness.hpp"
#include "input/haptics.hpp"
#include "input/virtual_pad.hpp"
#include "input/vr_controls.hpp"
#include "safety/mp_guard.hpp"
#include "stereo/aim_depth.hpp"
#include "stereo/camera_hook.hpp"
#include "stereo/eye_pass.hpp"
#include "stereo/muzzle_quads.hpp"
#include "stereo/foveation.hpp"
#include "stereo/hud_capture.hpp"
#include "stereo/stream_view.hpp"
#include "stereo/world_pass.hpp"
#include "weapon/grab_debug.hpp"
#include "weapon/hip_accuracy.hpp"
#include "weapon/unlock_all.hpp"
#include "weapon/wall_stop_game.hpp"
#include "weapon/zoom_mesh.hpp"
#include "weapon/physical_grenade.hpp"
#include "weapon/physical_reload.hpp"
#include "weapon/shot_origin.hpp"
#include "weapon/game_reload_sounds.hpp"
#include "weapon/hit_feedback.hpp"
#include "weapon/reload_sounds.hpp"
#include "ui/hud_widget_table.hpp"
#include "ui/hud_widgets.hpp"
#include "body/body_calibration.hpp"
#include "body/soldier_radius.hpp"
#include "ui/body_calibration_flow.hpp"
#include "ui/vr_panel.hpp"
#include "weapon/two_handed.hpp"
#include "weapon/weapon_pickup.hpp"
#include "weapon/weapon_swap.hpp"
#include "xr/controller_actions.hpp"
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

		// Controller mapping (input/button_map): choosing a preset writes its buttons; changing a
		// button by hand turns the preset to the one that now matches, or custom.
		void apply_button_map(settings::registry& store, bool first)
		{
			static std::string preset_seen;
			const std::string preset = store.text("controls_preset", "default");
			const input::preset* chosen = input::find_preset(preset);
			input::button_map map;
			if (!first && preset != preset_seen && chosen)
			{
				map = chosen->map;
				for (int i = 0; i < input::quest_button_count; ++i)
				{
					const auto button = static_cast<input::quest_button>(i);
					store.set(input::setting_name(button), input::value_name(map.target[i]));
				}
			}
			else
			{
				for (int i = 0; i < input::quest_button_count; ++i)
				{
					const auto button = static_cast<input::quest_button>(i);
					input::pad_button target{};
					if (input::parse_pad_button(store.text(input::setting_name(button)), target))
					{
						map.target[i] = target;
					}
				}
				const input::preset* matching = input::matching_preset(map);
				const std::string_view name = matching ? matching->name : std::string_view("custom");
				// At start-up the ini is taken as it is: no unsaved change before the user made one.
				if (!first && name != preset)
				{
					store.set("controls_preset", name);
				}
			}
			preset_seen = store.text("controls_preset", "default");
			input::set_button_map(map);
		}

		// Pose editor (panel page Skeleton): the sliders are the edited grip's tweak. Choosing a grip (or
		// loading the file) puts its saved tweak on the sliders; moving them changes it live. pose_file is
		// a button: save or load once, then back to keep.
		void apply_pose_editor(settings::registry& store)
		{
			static std::string shown; // the grip whose tweak the sliders hold
			constexpr const char* finger_names[body::fingers::count] = { "thumb", "index", "middle", "ring", "pinky" };
			const std::string edit = store.text("pose_edit", "off");
			const body::poses::grip which = body::poses::grip_named(edit);
			const std::string action = store.text("pose_file", "keep");
			// The sliders first, so a save in the same pass keeps what they show (a load replaces them anyway).
			if (edit == shown && which != body::poses::grip::none && action != "load")
			{
				body::poses::tweak value;
				for (int finger = 0; finger < body::fingers::count; ++finger)
				{
					value.fingers[finger].bend = store.real(std::string("pose_") + finger_names[finger] + "_bend", 0.0f);
					value.fingers[finger].side = store.real(std::string("pose_") + finger_names[finger] + "_side", 0.0f);
				}
				value.thumb_across = store.real("pose_thumb_across", 0.0f);
				value.thumb_out = store.real("pose_thumb_out", 0.0f);
				body::pose_book::set(which, value);
			}
			if (action == "save")
			{
				log::write("poses: %s", body::pose_book::save().c_str());
				store.set("pose_file", "keep");
			}
			else if (action == "load")
			{
				const std::string result = body::pose_book::load();
				log::write("poses: load %s", result.empty() ? "ok" : result.c_str());
				store.set("pose_file", "keep");
				shown.clear();
			}
			if (edit != shown)
			{
				shown = edit;
				if (which != body::poses::grip::none)
				{
					const body::poses::tweak saved = body::pose_book::get(which);
					char value[32];
					for (int finger = 0; finger < body::fingers::count; ++finger)
					{
						std::snprintf(value, sizeof(value), "%g", saved.fingers[finger].bend);
						store.set(std::string("pose_") + finger_names[finger] + "_bend", value);
						std::snprintf(value, sizeof(value), "%g", saved.fingers[finger].side);
						store.set(std::string("pose_") + finger_names[finger] + "_side", value);
					}
					std::snprintf(value, sizeof(value), "%g", saved.thumb_across);
					store.set("pose_thumb_across", value);
					std::snprintf(value, sizeof(value), "%g", saved.thumb_out);
					store.set("pose_thumb_out", value);
				}
			}
			const std::string show = store.text("pose_show", "off");
			body::set_pose_preview(show == "left" ? 0 : show == "right" ? 1 : -1, which);
		}

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
			stereo::set_cutscene_screen(store.text("cutscene_view", "screen") != "look");
			body::set_cutscene_hands(store.text("cutscene_hands", "game") == "game");
			stereo::set_stream_view(store.boolean("stream_view", true), store.real("stream_fov", 80.0f), store.real("stream_smoothing", 0.3f),
				store.boolean("stream_hud", true));
			stereo::set_physical_stance(store.boolean("physical_stance", false));
			// #48: behaviours that once had a setting to compare them with the old one are fixed on.
			stereo::set_gun_life_size(true);
			stereo::set_gun_fov_life_size(true);
			stereo::set_view_crop(store.real("view_crop_h", 1.0f), store.real("view_crop_v", 1.0f));
			stereo::set_view_crop_bottom(store.real("view_crop_bottom", 0.0f));
			stereo::set_steady_walk(true);
			stereo::set_ipd_scale(store.real("ipd_scale", 1.0f));
			body::set_arm_ik(store.boolean("arm_ik", true));
			body::set_arm_stretch(store.real("arm_stretch", 0.25f));
			body::set_shoulder_reach(store.real("shoulder_reach", 0.12f));
			if (const auto shoulders = store.get("shoulder_offset"))
			{
				body::set_shoulder_offset(shoulders->number[0], shoulders->number[1], shoulders->number[2]);
			}
			input::set_neutral_pitch(store.boolean("neutral_pitch", false));
			input::set_controller_aim(store.text("aim", "controller") == "controller");
			body::set_gun_on_ray(store.text("gun_pose", "aim") != "animated");
			const std::string move = store.text("move_direction", "head");
			input::set_move_reference(move == "gun" ? input::move_reference::gun : move == "body" ? input::move_reference::body : input::move_reference::head);
			input::set_sprint_steer(store.boolean("sprint_steer", true));
			weapon::set_physical_reload(store.text("reload", "physical") == "physical");
			weapon::set_drop_discards_ammo(store.boolean("drop_discards_ammo", true));
			weapon::set_palm_reload(true);
			weapon::set_magazine_snapping(true);
			weapon::set_magazine_reach(store.real("magazine_reach", 0.13f));
			weapon::set_free_hand_reload(true);
			body::set_extra_meshes(true);
			weapon::set_game_reload_sounds(store.boolean("reload_sounds", true));
			weapon::set_magazine_flight(store.boolean("magazine_flight", true));
			weapon::set_grip_lock(store.boolean("grip_lock", true));
			weapon::set_wall_stop(store.boolean("wall_stop", true));
			apply_pose_editor(store);
			const std::string accessory = store.text("accessory_toggle", "trigger");
			weapon::set_accessory_touch(accessory == "touch");
			weapon::set_accessory_trigger(accessory == "trigger");
			weapon::set_holsters(true);
			weapon::set_grab_debug(store.boolean("grab_debug", false));
			weapon::set_sounds(store.boolean("reload_sounds", true));
			hooks::set_physical_pad(store.boolean("physical_pad", true));
			xr::set_trigger_press(store.real("trigger_press", 0.55f));
			input::set_game_buttons(store.boolean("grips_to_game", false), store.boolean("ads_trigger", false), store.boolean("x_menu", true));
			body::set_arm_shape(store.real("arm_length", 1.15f), store.real("shoulder_width", 0.0f));
			{
				// #46, #50: the body zones and the shoulders follow the height (1.78 m: as laid out); a measure
				// set by hand (not 0) wins over the height.
				const body::body_size size = body::size_for({ store.real("body_height", 0.0f), store.real("shoulder_drop", 0.0f),
					store.real("chest_depth", 0.0f), store.real("hip_drop", 0.0f) });
				body::set_body_size(size);
				weapon::set_body_size(size);
				body::set_shoulder_back(store.real("shoulder_back", 0.0f));
			}
			body::set_legs_hidden(store.boolean("hide_legs", true));
			body::set_soldier_radius(store.real("soldier_radius", 0.0f));
			body::set_torso_follow(store.boolean("torso_follow", true));
			stereo::set_aim_follows_torso(store.boolean("aim_follows_torso", true));
			body::set_torso_hands(store.boolean("torso_hands", false));
			body::set_body_shoulders(true);
			body::set_finger_poses(true);
			body::set_off_hand_neutral(store.text("off_hand_pose", "neutral") == "neutral");
			body::set_gun_hand_neutral(store.text("gun_hand_pose", "neutral") == "neutral");
			body::set_magazine_grip(store.text("magazine_grip", "auto"));
			body::set_rack_grip(store.text("rack_grip", "fist"));
			body::set_support_slide(store.boolean("support_lock", true), store.real("support_slide_back", 0.06f), store.real("support_slide_forward", 0.08f));
			body::set_hand_collision(store.boolean("hand_collision", true));
			stereo::muzzle_quads::set_on(store.boolean("flash_at_gun", true));
			body::set_rack_fist_offsets(store.real("rack_over_out", 0.05f), store.real("rack_over_up", 0.01f), store.real("rack_under_out", 0.03f),
				store.real("rack_under_down", 0.04f));
			body::set_gun_recoil(store.real("recoil", 1.0f));
			body::set_one_hand_recoil(store.real("one_hand_recoil", 1.0f));
			weapon::set_squad_signal(store.boolean("squad_signal", true));
			weapon::set_melee_gesture(store.boolean("melee_gesture", true));
			weapon::set_knife_grip(true);
			weapon::set_use_reach(store.boolean("use_reach", true));
			weapon::set_weapon_pickup(store.boolean("weapon_pickup", true));
			weapon::set_free_hands(store.boolean("free_hands", true));
			weapon::set_free_hands_release(store.real("free_hands_release", 0.0f));
			weapon::set_sling(store.boolean("sling", true));
			weapon::set_sling_show_primary(store.boolean("sling_show_primary", true));
			weapon::set_slot_models_at_start(store.boolean("slot_models_at_start", true));
			weapon::set_slot_models_ahead(store.real("slot_models_ahead", 0.08f));
			weapon::set_c4_trigger(store.boolean("c4_trigger", true));
			weapon::set_action_aim(store.boolean("action_aim", true));
			input::configure_haptics({ store.boolean("haptics", true), store.real("haptics_strength", 0.8f), store.boolean("haptics_game", true),
				store.boolean("haptics_gestures", true) });
			body::set_knife_pitch(store.real("knife_pitch", 145.0f));
			body::set_knife_turn(store.real("knife_yaw", -15.0f), store.real("knife_roll", 170.0f));
			weapon::set_physical_grenades(store.text("grenade", "physical") == "physical");
			body::set_grenade_mesh(store.text("grenade", "physical") == "physical");
			weapon::set_grenade_throw_scale(store.real("grenade_throw_scale", 1.5f));
			weapon::set_two_handed(true);
			weapon::set_two_hand_lock(true);
			weapon::set_aim_convergence(store.real("aim_convergence", 25.0f));
			stereo::set_aim_depth(true);
			weapon::set_shot_from_gun(true);
			weapon::set_shots_from_gun(true);
			weapon::set_aim_with_body(true);
			weapon::set_sight_ads(store.boolean("sight_ads", true));
			weapon::set_sight_dot_reach(store.real("sight_dot_reach", 2.0f) * 0.01f);
			weapon::set_hip_accuracy(store.real("hip_accuracy", 1.0f));
			weapon::set_unlock_all_weapons(store.boolean("unlock_all_weapons", true));
			weapon::set_zoom_mesh(store.boolean("sight_zoom_mesh", true));
			weapon::set_sight_reticle(store.boolean("sight_reticle", true));
			stereo::set_scope_in_frame(store.boolean("scope_in_frame", true));
			stereo::set_sight_lens(store.boolean("sight_lens", true), store.real("sight_lens_fov", 40.0f), store.real("sight_lens_brightness", 4.0f));
			weapon::set_zoom_mesh_always(store.boolean("sight_lens", true));
			weapon::set_sight_lens_cone(store.real("sight_lens_cone", 8.0f));
			stereo::set_foveation(store.text("foveation", "light"));
			{
				bool shown[32]{};
				const auto elements = ui::hud_widgets::elements();
				for (std::size_t i = 0; i < elements.size() && i < 32; ++i)
				{
					shown[i] = store.boolean(elements[i].setting, elements[i].shown_by_default);
				}
				ui::set_hud_widgets_hidden(ui::hud_widgets::hidden_mask({ shown, elements.size() }));
			}
			input::set_left_handed(store.text("dominant_hand", "right") == "left");
			input::configure_turn(store.text("turn", "snap"), store.integer("snap_turn_degrees", 30), store.boolean("stance_flicks", true));
			// Modes are switched only on a change: start-up already applied the ini values.
			const std::string render = store.text("stereo_render", "pair");
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
			apply_button_map(store, first);
		}

		void on_present(IDXGISwapChain* swap_chain)
		{
			present_thread = GetCurrentThreadId();
			apply_settings();
			weapon::update_physical_reload();
			weapon::update_hip_accuracy();
			weapon::update_unlock_all_weapons();
			weapon::update_physical_grenades();
			weapon::update_wall_stop();
			weapon::update_two_handed();
			weapon::update_zoom_mesh();
			ui::update_body_calibration();
			body::update_soldier_radius();
			ui::update_vr_panel();
			weapon::update_weapon_swap();
			weapon::update_hit_feedback();
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
			// After the theater took this frame's eyes: the window's picture is drawn over.
			stereo::stream_view_on_present(swap_chain, theater && theater->status().menu_over_level);
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
				"xinput get_state_calls=%llu get_capabilities_calls=%llu set_state_calls=%llu virtual_pad_offered=%llu virtual_pad_xinput=%llu\n"
				"camera copies=%llu copy_thread=%lu present_thread=%lu eye_offset=%.4f stereo=%d eye_images=%llu",
				BFVR_GAME_NAME, vr.phase, vr.session_state, vr.presents_seen, vr.frames_submitted, vr.max_sync_ms, vr.max_frame_ms,
				vr.stage ? vr.stage : "-", vr.stage_ms,
				pad.connected ? 1 : 0, pad.buttons,
				pad.left_trigger, pad.right_trigger, pad.left_x, pad.left_y, pad.right_x, pad.right_y,
				hooks::xinput_call_counts().state_calls, hooks::xinput_call_counts().capability_calls, hooks::xinput_call_counts().set_state_calls,
				virtual_pad.offered, virtual_pad.xinput_answers,
				camera.copies, camera.copy_thread, present_thread.load(), stereo::eye_offset(), vr.stereo ? 1 : 0, vr.eye_images_stored);
			return std::string(text) + "\n" + hooks::physical_pad_report() + "\n" + hooks::render_size_report() + " focus_losses_hidden=" + std::to_string(hooks::focus_losses_hidden())
				+ "\neyepass " + stereo::eye_pass_report() + "\nmenu_over_level=" + (vr.menu_over_level ? "1" : "0") + " sight_dot_frames=" + std::to_string(vr.sight_dot_frames)
				+ "\nmenu_pointer frames=" + std::to_string(vr.pointer_frames) + " no_hand=" + std::to_string(vr.pointer_no_hand) + " misses=" + std::to_string(vr.pointer_misses)
				+ " hits=" + std::to_string(vr.pointer_hits) + " hand=" + std::to_string(vr.pointer_hand) + " last_uv=" + std::to_string(vr.pointer_u) + "," + std::to_string(vr.pointer_v)
				+ " " + hooks::game_pointer_report()
				+ "\nxr_focus session=" + (vr.session_state_name ? vr.session_state_name : "-") + " input_focus=" + (vr.input_focus ? "1" : "0")
				+ " focus_losses=" + std::to_string(vr.focus_losses) + " unfocused_s=" + std::to_string(static_cast<int>(vr.unfocused_seconds))
				+ (vr.session_state_name && !vr.input_focus && std::strcmp(vr.session_state_name, "VISIBLE") == 0 ? " (SteamVR has the controllers)" : "");
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
			// Shots start at the drawn gun (controller aim); the setting switches it live.
			weapon::install_shot_origin_hook();

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
			ui::install_hud_widgets();

			// The VR controllers reach the game as an Xbox pad through its own XInput calls.
			if (theater && config::get_bool("gamepad", true))
			{
				hooks::install_xinput_hooks();
				hooks::install_rumble_hook();
				body::install_soldier_radius_hook();
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
