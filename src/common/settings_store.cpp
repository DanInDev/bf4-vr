#include "common/settings_store.hpp"

#include <windows.h>

#include <fstream>
#include <iterator>
#include <sstream>

#include "common/config.hpp"
#include "common/log.hpp"
#include "input/button_map.hpp"

namespace bfvr::settings
{
	namespace
	{
		// Live tunables. Start-up-only keys (render_size, xr_runtime, stereo, hud_layer, research) stay
		// in config: they cannot change while the game runs.
		const definition definitions[] = {
			{ "gun_scale", kind::real, 0.25f, 4.0f, 0.05f, "1", "", "apparent gun and arms size on top of gun_life_size (1 = life size there, else the game's)" },
			{ "gun_life_size", kind::boolean, 0, 0, 0, "1", "", "draw the gun and arms at life size, on the controllers (the game draws them with a fixed 45 degree FOV: about 2.7 times too large in the headset)" },
			{ "view_crop_h", kind::real, 0.4f, 1.0f, 0.02f, "1", "", "rendered part of the headset FOV, horizontal" },
			{ "view_crop_v", kind::real, 0.4f, 1.0f, 0.02f, "1", "", "rendered part of the headset FOV, vertical" },
			{ "view_crop_bottom", kind::real, 0.0f, 30.0f, 1.0f, "0", "", "degrees cut off the bottom of the view (the top stays): the lens shows less below than above, so the image gets sharper; raise until the bottom edge shows, then back off" },
			{ "foveation", kind::choice, 0, 0, 0, "light", "off|light|medium|strong", "fixed foveated rendering (NVIDIA): the edge of each eye's image is shaded at a lower rate, the lens blurs it anyway; more frame rate" },
			{ "steady_walk", kind::boolean, 0, 0, 0, "1", "", "remove the game's walking sway (the view swung ~6 cm side to side with every step); your own head still moves it" },
			{ "vignette", kind::choice, 0, 0, 0, "off", "off|turning|moving|both", "comfort vignette: darken the edge of the view while the game turns or moves you (not your head)" },
			{ "vignette_strength", kind::real, 0.1f, 1.0f, 0.05f, "0.7", "", "comfort vignette: how far it closes in at full turn or speed" },
			{ "ipd_scale", kind::real, 0.5f, 2.0f, 0.05f, "1", "", "eye distance multiplier (world scale), 1 = the runtime's IPD" },
			{ "hud_distance", kind::real, 0.3f, 20.0f, 0.1f, "1.5", "", "HUD layer distance, metres" },
			{ "hud_width", kind::real, 0.2f, 10.0f, 0.05f, "1.3", "", "HUD layer width, metres" },
			{ "hud_mode", kind::choice, 0, 0, 0, "body", "head|body", "HUD head-locked, or projected (markers on targets) with minimap and ammo on the body" },
			{ "hud_marker_distance", kind::real, 2.0f, 50.0f, 1.0f, "10", "", "hud_mode = body: distance of the projected HUD, metres" },
			{ "hud_watch_width", kind::real, 0.05f, 0.3f, 0.01f, "0.12", "", "hud_mode = body: minimap width on the forearm, metres" },
			{ "hud_ammo_width", kind::real, 0.05f, 0.3f, 0.01f, "0.1", "", "hud_mode = body: ammo counter width beside the gun, metres" },
			{ "hud_show_main", kind::boolean, 0, 0, 0, "1", "", "HUD layer: everything above the minimap and ammo (markers, objective text, prompts)" },
			{ "hud_show_minimap", kind::boolean, 0, 0, 0, "1", "", "HUD: minimap" },
			{ "hud_show_ammo", kind::boolean, 0, 0, 0, "1", "", "HUD: ammo and grenade counter" },
			{ "hud_show_bottom", kind::boolean, 0, 0, 0, "1", "", "HUD: weapon and stance icons at the bottom" },
			{ "stereo_render", kind::choice, 0, 0, 0, "pair", "aer|pair", "both eyes from one game frame, or alternate eyes" },
			{ "cutscene_view", kind::choice, 0, 0, 0, "screen", "screen|look", "while the game moves the camera (cutscenes, the death fall): a flat screen, or 3D with your head looking around in the scene" },
			{ "shoulder_offset", kind::vec3, -0.5f, 0.5f, 0.0f, "0 0 0", "", "shoulders relative to the head, metres (x right, y up, z back)" },
			{ "arm_reach", kind::boolean, 0, 0, 0, "1", "", "a hand out of the arm's reach moves the shoulder forward (shoulder_reach) and stretches the arm (arm_stretch)" },
			{ "shoulder_reach", kind::real, 0.0f, 0.3f, 0.01f, "0.12", "", "a shoulder moves up to this far towards a hand out of reach before the arm stretches, metres" },
			{ "arm_ik", kind::boolean, 0, 0, 0, "1", "", "first-person arms follow the VR controllers" },
			{ "arm_stretch", kind::real, 0.0f, 0.6f, 0.05f, "0.25", "", "an arm still out of reach (after arm_length and shoulder_reach) may get this much longer (pipe arms: reach > 1 = out of reach)" },
			{ "right_stick", kind::choice, 0, 0, 0, "turn", "look|turn", "right stick aims, or turns and changes stance" },
			{ "turn", kind::choice, 0, 0, 0, "snap", "snap|smooth|off", "right stick turn style (right_stick = turn)" },
			{ "snap_turn_degrees", kind::integer, 5, 90, 5, "30", "", "angle per snap turn" },
			{ "dominant_hand", kind::choice, 0, 0, 0, "right", "right|left", "gun hand: left mirrors the pad and swaps the hand roles" },
			{ "reload", kind::choice, 0, 0, 0, "physical", "button|physical", "reload: the game's button, or by hand (AK: pull the magazine, AR: B releases it; chest pouch)" },
			{ "grips_to_game", kind::boolean, 0, 0, 0, "0", "", "controller grips also reach the game as LB/RB (grenade, fire in some layouts); off: they are the mod's" },
			{ "x_menu", kind::boolean, 0, 0, 0, "1", "", "left X opens the game's menu (Start); the right B reloads" },
			{ "ads_trigger", kind::boolean, 0, 0, 0, "0", "", "left trigger = the game's aim down sights (zooms the view)" },
			{ "hud_show_crosshair", kind::boolean, 0, 0, 0, "0", "", "HUD: the game's crosshair in the middle" },
			{ "hud_show_hit_markers", kind::boolean, 0, 0, 0, "1", "", "HUD: the hit marker when your shots hit" },
			{ "hud_show_damage", kind::boolean, 0, 0, 0, "1", "", "HUD: the red arcs showing where damage comes from" },
			{ "hud_show_objectives", kind::boolean, 0, 0, 0, "1", "", "HUD: objective markers and hints" },
			{ "hud_show_prompts", kind::boolean, 0, 0, 0, "1", "", "HUD: interaction prompts (pick up, open)" },
			{ "hud_show_score", kind::boolean, 0, 0, 0, "1", "", "HUD: score pop-ups and awards" },
			{ "hud_show_markers", kind::boolean, 0, 0, 0, "1", "", "HUD: markers in the world (squad names and triangles)" },
			{ "arm_length", kind::real, 0.8f, 1.5f, 0.05f, "1.15", "", "first-person arms stretched by this (reach)" },
			{ "hide_legs", kind::boolean, 0, 0, 0, "1", "", "first-person legs hidden (they sit oddly under a tracked head)" },
			{ "torso_follow", kind::boolean, 0, 0, 0, "1", "", "the shoulders turn with your real body (from head and hands), not only with the stick" },
			{ "off_hand_pose", kind::choice, 0, 0, 0, "neutral", "animated|neutral", "the free off hand's wrist: the gun hand's grip mirrored, as you hold the controller, a held magazine upright in the fist (neutral), or the game's handguard hold turned by the controller (animated)" },
			{ "magazine_grip", kind::choice, 0, 0, 0, "auto", "auto|wrap|cup", "a held magazine: wrapped in the fist, or standing on the upturned palm with the fingers closing on it (auto: cup once the palm faces up)" },
			{ "rack_grip", kind::choice, 0, 0, 0, "pinch", "pinch|hook", "the charging handle between the thumb and the side of the index (pinch), or inside the curled index (hook)" },
			{ "finger_poses", kind::boolean, 0, 0, 0, "1", "", "fingers follow the controller (squeeze, trigger, thumb) and wrap magazines and the charging handle; off = the game's animated hands" },
			{ "shoulder_width", kind::real, -0.1f, 0.2f, 0.01f, "0", "", "first-person shoulders moved outwards by this, metres per side" },
			{ "body_shoulders", kind::boolean, 0, 0, 0, "1", "", "shoulder joints where your body has them (either side of the neck), not where the game's rifle pose holds them (the left one mid-chest)" },
			{ "holsters", kind::boolean, 0, 0, 0, "1", "", "weapon holsters on the body: gun hand + grip over the shoulder (primary), at the hip (secondary), chest (knife), belt (gadgets)" },
			{ "recoil", kind::real, 0.0f, 3.0f, 0.1f, "1", "", "the drawn gun kicks per shot by the gun's own recoil data (muzzle up and sideways, back along the barrel; softer two-handed), 0 = off; with shot_line = gun the shots follow the kick" },
			{ "melee_gesture", kind::boolean, 0, 0, 0, "1", "", "a quick forward thrust of the gun hand is the game's melee (knife stab)" },
			{ "knife_grip", kind::boolean, 0, 0, 0, "1", "", "with the knife out, the blade points where the gun-hand controller points (off: the game's knife-hand pose turned by the controller)" },
			{ "knife_pitch", kind::real, -90.0f, 90.0f, 5.0f, "30", "", "knife_grip: degrees the blade is raised to lie along the controller ray (lower = blade tilts down)" },
			{ "use_reach", kind::boolean, 0, 0, 0, "1", "", "reach out ahead with the off hand (away from the gun) and squeeze its grip: the game's use (pick up, doors, prompts)" },
			{ "free_hands", kind::boolean, 0, 0, 0, "0", "", "hold the gun with the gun-hand grip: let go and it drops to the floor below your hand (no firing); reach down and grip to pick it up" },
			{ "c4_trigger", kind::boolean, 0, 0, 0, "1", "", "with the C4 out, the off-hand trigger throws a C4 (the game's aim button); the gun-hand trigger on the detonator sets them off" },
			{ "squad_signal", kind::boolean, 0, 0, 0, "1", "", "squad order (Q): raise the off hand ahead above your eyes and squeeze the grip" },
			{ "grenade", kind::choice, 0, 0, 0, "physical", "button|physical", "grenades: the game's button (LB), or by hand (belt, pin, throw)" },
			{ "grenade_throw_scale", kind::real, 0.5f, 4.0f, 0.1f, "1.5", "", "physical grenades: launch speed = hand speed times this" },
			{ "grenade_model", kind::boolean, 0, 0, 0, "1", "", "physical grenades: draw the grenade in the off hand while you hold it (the game shows none until the throw)" },
			{ "reload_sounds", kind::boolean, 0, 0, 0, "1", "", "sounds for magazines, chambering, grenade pouch and pin (a WAV in bfvr_sounds replaces one)" },
			{ "game_sounds", kind::boolean, 0, 0, 0, "1", "", "reload sounds: the game's own magazine-out, magazine-in and bolt sounds where the gun's are found (assault rifles)" },
			{ "drop_discards_ammo", kind::boolean, 0, 0, 0, "1", "", "physical reload: a dropped magazine loses its rounds (off: they go back to the reserve)" },
			{ "accessory_toggle", kind::choice, 0, 0, 0, "trigger", "trigger|touch|off", "accessory toggle (laser, light, magnifier: D-pad up): trigger = holding the gun two-handed, press the off-hand trigger; touch = off hand on the optic or front rail + grip" },
			{ "vr_panel", kind::boolean, 0, 0, 0, "1", "", "hold the left menu button to open this panel (a short press stays the game's Start)" },
			{ "stick_menu", kind::boolean, 0, 0, 0, "1", "", "hold both thumbstick clicks ~0.5 s to open or close this panel (SteamVR can take the left menu button)" },
			{ "mark_tap", kind::boolean, 0, 0, 0, "1", "", "tap both thumbstick clicks briefly to mark a moment for feedback (state and screenshot saved to bfvr_marks)" },
			{ "panel_opacity", kind::real, 0.2f, 1.0f, 0.05f, "0.6", "", "how solid this panel's backdrop is (1 = opaque); the pointer beam and dot show where it points" },
			{ "menu_hint", kind::boolean, 0, 0, 0, "1", "", "after a level loads, show how to open this panel for 8 s" },
			{ "focus_notice", kind::boolean, 0, 0, 0, "1", "", "a notice while SteamVR has the controllers (its dashboard is open, the game gets no input)" },
			{ "pause_flat", kind::boolean, 0, 0, 0, "1", "", "the pause menu (and other menus over a level) is shown on the flat screen with the pointer" },
			{ "haptics", kind::boolean, 0, 0, 0, "1", "", "controller vibration: the game's rumble (firing, explosions, hits) and taps for grabs and gestures" },
			{ "haptics_strength", kind::real, 0.0f, 1.0f, 0.05f, "0.8", "", "controller vibration strength" },
			{ "haptics_game", kind::boolean, 0, 0, 0, "1", "", "haptics: the game's rumble (firing on the gun hand, both hands for explosions and hits)" },
			{ "haptics_gestures", kind::boolean, 0, 0, 0, "1", "", "haptics: taps for holsters, magazines, grenades, the two-handed grab, use, menu clicks" },
			{ "physical_pad", kind::boolean, 0, 0, 0, "1", "", "a real gamepad on the first slot also reaches the game (off: only the VR controllers; a stuck trigger there fires the gun)" },
			{ "grab_debug", kind::boolean, 0, 0, 0, "0", "", "draw every grab zone (magazine, handle, pouch, handguard, holsters) as a ring as large as its reach; green while a hand is inside" },
			{ "menu_input", kind::choice, 0, 0, 0, "pointer", "pointer|pad", "menus: point with a controller and pull the trigger, or the pad only" },
			{ "aim", kind::choice, 0, 0, 0, "controller", "head|controller", "what aims the gun: the gun-hand controller (bullets leave the drawn barrel) or the view (game default)" },
			{ "gun_pose", kind::choice, 0, 0, 0, "aim", "aim|animated", "where the drawn gun points: exactly along the controller ray, or the game's hip pose turned by the controller (its idle angle and sway stay)" },
			{ "reticle", kind::boolean, 0, 0, 0, "1", "", "aim = controller: a dot where the shots converge (aim_convergence ahead along the barrel)" },
			{ "reticle_size", kind::real, 0.3f, 3.0f, 0.1f, "1", "", "aim reticle size, degrees" },
			{ "two_handed", kind::boolean, 0, 0, 0, "1", "", "off hand + grip on the handguard holds the gun with both hands: the barrel points along the line between them" },
			{ "two_hand_mode", kind::choice, 0, 0, 0, "lock", "lock|soft", "two-handed: the barrel lies on the line between your hands (lock), or the hand line only steers the gun from where it was at the grab (soft)" },
			{ "shot_line", kind::choice, 0, 0, 0, "gun", "gun|controller", "aim = controller: shots and the dot follow the gun as drawn (its sight), or the controller's pointing ray" },
			{ "sight_ads", kind::boolean, 0, 0, 0, "1", "", "hold the gun's sight to your eye and the game aims down it (the optic's lens clears, ADS accuracy)" },
			{ "scope", kind::boolean, 0, 0, 0, "1", "", "magnifying sights (the AK-12's flip magnifier): while aiming, the lens shows the world magnified (one more render)" },
			{ "scope_zoom", kind::real, 0.5f, 4.0f, 0.1f, "1", "", "scope magnification times the game's own (1 = the game's: the magnifier 1.7x)" },
			{ "aim_convergence", kind::real, 2.0f, 100.0f, 1.0f, "25", "", "aim = controller: shots meet the barrel line this far away, metres (bullets leave the game camera, not the gun)" },
			{ "move_direction", kind::choice, 0, 0, 0, "head", "head|gun|body", "left stick forward walks where your head faces, where the gun points, or along the body (the game's own way)" },
			{ "neutral_pitch", kind::boolean, 0, 0, 0, "0", "", "hold the game's aim pitch at 0: only the head pitches the view (level horizon)" },
			{ "stance_flicks", kind::boolean, 0, 0, 0, "1", "", "right stick up/down changes stance (right_stick = turn)" },
			{ "physical_stance", kind::boolean, 0, 0, 0, "0", "", "crouch or lie down in the room and the soldier does too; off when playing seated" },
			{ "controls_preset", kind::choice, 0, 0, 0, "default", "default|jump_on_b|fire_mode_on_y|custom", "controller mapping: a preset sets the buttons below; changing one of them makes it custom" },
			{ "bind_a", kind::choice, 0, 0, 0, "a", std::string(input::pad_button_choices()), "what the right A button presses (pad button; a = jump)" },
			{ "bind_b", kind::choice, 0, 0, 0, "b", std::string(input::pad_button_choices()), "what the right B button presses (b = reload, magazine release)" },
			{ "bind_x", kind::choice, 0, 0, 0, "x", std::string(input::pad_button_choices()), "what the left X button presses (x = the game's menu)" },
			{ "bind_y", kind::choice, 0, 0, 0, "y", std::string(input::pad_button_choices()), "what the left Y button presses (y = switch weapon)" },
			{ "bind_left_stick", kind::choice, 0, 0, 0, "left_stick", std::string(input::pad_button_choices()), "what the left stick click presses (left_stick = sprint)" },
			{ "bind_right_stick", kind::choice, 0, 0, 0, "right_stick", std::string(input::pad_button_choices()), "what the right stick click presses (right_stick = melee)" },
			{ "bind_menu", kind::choice, 0, 0, 0, "start", std::string(input::pad_button_choices()), "what the left menu button presses (start = pause; held: this panel)" },
		};

		// Read once by the game (common/config): theater.cpp, core.cpp, camera_hook.cpp, hud_capture.cpp,
		// runtime_selection.cpp. Defaults as there.
		const definition startup[] = {
			{ "theater", kind::boolean, 0, 0, 0, "1", "", "VR on: the game is shown in the headset" },
			{ "stereo", kind::boolean, 0, 0, 0, "0", "", "head-tracked 3D (off: the game on a flat virtual screen)" },
			{ "stereo_first_person", kind::boolean, 0, 0, 0, "1", "", "3D: the gun stays on the soldier's aim while the head looks around" },
			{ "gamepad", kind::boolean, 0, 0, 0, "1", "", "the VR controllers drive the game as an Xbox pad" },
			{ "hud_layer", kind::choice, 0, 0, 0, "0", "0|1|2", "3D: the HUD on its own sharp layer (2: captured every frame)" },
			{ "xr_runtime", kind::choice, 0, 0, 0, "auto", "auto|steamvr|system", "OpenXR runtime: auto = SteamVR if it is running, else the Windows default" },
		};

		registry& instance()
		{
			static registry store;
			return store;
		}
	}

	registry& global()
	{
		return instance();
	}

	void load_global()
	{
		registry& store = instance();
		for (const definition& setting : definitions)
		{
			if (!store.add(setting))
			{
				log::write("settings: definition of %s rejected", setting.name.c_str());
			}
		}
		// A sentinel tells "missing" from "empty" (config::get has no presence query).
		constexpr const char* missing = "\x01";
		const auto warnings = store.load([&](const std::string& name) -> std::optional<std::string>
		{
			std::string text = config::get(name.c_str(), missing);
			return text == missing ? std::nullopt : std::optional<std::string>(std::move(text));
		});
		for (const std::string& warning : warnings)
		{
			log::write("settings: %s", warning.c_str());
		}
		log::write("settings: %zu live settings loaded", store.names().size());
	}

	std::string save_global()
	{
		const std::wstring path = config::ini_path();
		if (path.empty())
		{
			return "error: no bfvr.ini path";
		}
		std::string existing;
		{
			std::ifstream file(path, std::ios::binary);
			existing.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
		}
		const std::string merged = instance().merge_ini(existing);
		const std::wstring temporary = path + L".tmp";
		{
			std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
			file.write(merged.data(), static_cast<std::streamsize>(merged.size()));
			if (!file)
			{
				return "error: cannot write bfvr.ini.tmp";
			}
		}
		if (!MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING))
		{
			return "error: cannot replace bfvr.ini (" + std::to_string(GetLastError()) + ")";
		}
		instance().mark_saved();
		return {};
	}

	std::span<const definition> live_definitions()
	{
		return definitions;
	}

	std::span<const definition> startup_definitions()
	{
		return startup;
	}

	std::string pipe_command(std::string_view line)
	{
		if (line == "save")
		{
			const std::string error = save_global();
			return error.empty() ? "saved" : error;
		}
		return instance().command(line.empty() ? std::string_view("list") : line);
	}
}
