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
			{ "gun_fov_life_size", kind::boolean, 0, 0, 0, "1", "", "gun_life_size through the gun pass's own FOV (the game's hip and zoom FOV set to life size) instead of squeezing the first-person camera: sights show their reticle and lens; 0 = the squeeze (no red dot)" },
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
			{ "trigger_press", kind::real, 0.2f, 0.9f, 0.05f, "0.55", "", "how far a trigger is pulled before the game sees it (0..1); released 0.1 below; a finger resting on the trigger reads 0.25-0.5" },
			{ "ads_trigger", kind::boolean, 0, 0, 0, "0", "", "left trigger = the game's aim down sights (zooms the view)" },
			{ "hud_show_crosshair", kind::boolean, 0, 0, 0, "0", "", "HUD: the game's crosshair in the middle" },
			{ "hud_show_hit_markers", kind::boolean, 0, 0, 0, "1", "", "HUD: the hit marker when your shots hit" },
			{ "hud_show_damage", kind::boolean, 0, 0, 0, "1", "", "HUD: the red arcs showing where damage comes from" },
			{ "hud_show_objectives", kind::boolean, 0, 0, 0, "1", "", "HUD: objective markers and hints" },
			{ "hud_show_prompts", kind::boolean, 0, 0, 0, "1", "", "HUD: interaction prompts (pick up, open)" },
			{ "hud_show_score", kind::boolean, 0, 0, 0, "1", "", "HUD: score pop-ups and awards" },
			{ "hud_show_markers", kind::boolean, 0, 0, 0, "1", "", "HUD: markers in the world (squad names and triangles)" },
			{ "body_calibration", kind::choice, 0, 0, 0, "first", "first|always|off", "body calibration (T-pose, both triggers held a second) after a level loads: until done once, every time, or never; it sets body_height, body_span, arm_length and shoulder_width" },
			{ "body_height", kind::real, 0.0f, 2.2f, 0.01f, "0", "", "your height from the calibration, metres (0 = not calibrated: the next level asks); scales the pouches, holsters and shoulders (1.78 = as laid out)" },
			{ "body_span", kind::real, 0.0f, 2.5f, 0.01f, "0", "", "your arm span, grip to grip, from the calibration, metres (information)" },
			{ "arm_length", kind::real, 0.8f, 1.5f, 0.05f, "1.15", "", "first-person arms stretched by this (reach)" },
			{ "hide_legs", kind::boolean, 0, 0, 0, "1", "", "first-person legs hidden (they sit oddly under a tracked head)" },
			{ "torso_hands", kind::boolean, 0, 0, 0, "0", "", "the torso also leans towards two hands held together in front (off: it turns only when the head turns past ~40 degrees, or with the stick)" },
			{ "torso_follow", kind::boolean, 0, 0, 0, "1", "", "the shoulders turn with your real body (the head past ~40 degrees), not only with the stick" },
			{ "off_hand_pose", kind::choice, 0, 0, 0, "neutral", "animated|neutral", "the free off hand's wrist: the gun hand's grip mirrored, as you hold the controller, a held magazine upright in the fist (neutral), or the game's handguard hold turned by the controller (animated)" },
			{ "magazine_grip", kind::choice, 0, 0, 0, "auto", "auto|wrap|cup", "a held magazine: wrapped in the fist, or standing on the upturned palm with the fingers closing on it (auto: cup once the palm faces up)" },
			{ "rack_grip", kind::choice, 0, 0, 0, "auto", "auto|pinch|hook", "the charging handle between the thumb and the side of the index (pinch), or inside the curled index (hook); auto: the pinch with the palm down on it, hooked in the index and middle with the palm up under it" },
			{ "finger_poses", kind::boolean, 0, 0, 0, "1", "", "fingers follow the controller (squeeze, trigger, thumb) and wrap magazines and the charging handle; off = the game's animated hands" },
			{ "shoulder_width", kind::real, -0.1f, 0.2f, 0.01f, "0", "", "first-person shoulders moved outwards by this, metres per side" },
			{ "body_shoulders", kind::boolean, 0, 0, 0, "1", "", "shoulder joints where your body has them (either side of the neck), not where the game's rifle pose holds them (the left one mid-chest)" },
			{ "holsters", kind::boolean, 0, 0, 0, "1", "", "weapon holsters on the body: gun hand + grip over the shoulder (primary), at the hip (secondary), chest (knife), belt (gadgets)" },
			{ "recoil", kind::real, 0.0f, 3.0f, 0.1f, "1", "", "the drawn gun kicks per shot by the gun's own recoil data (muzzle up and sideways, back along the barrel; softer two-handed), 0 = off; with shot_line = gun the shots follow the kick" },
			{ "melee_gesture", kind::boolean, 0, 0, 0, "1", "", "a quick forward thrust of the gun hand is the game's melee (knife stab)" },
			{ "knife_grip", kind::boolean, 0, 0, 0, "1", "", "with the knife out, the blade points where the gun-hand controller points (off: the game's knife-hand pose turned by the controller)" },
			{ "knife_pitch", kind::real, -180.0f, 180.0f, 5.0f, "30", "", "knife_grip: degrees the blade is raised to lie along the controller ray (lower = blade tilts down)" },
			{ "knife_yaw", kind::real, -180.0f, 180.0f, 5.0f, "0", "", "knife_grip: degrees the held knife is turned about the controller's up (+ = to the right)" },
			{ "knife_roll", kind::real, -180.0f, 180.0f, 5.0f, "0", "", "knife_grip: degrees the held knife is rolled about the controller ray (+ = clockwise seen from behind)" },
			{ "use_reach", kind::boolean, 0, 0, 0, "1", "", "reach out ahead with the off hand (away from the gun) and squeeze its grip: the game's use (pick up, doors, prompts)" },
			{ "sling", kind::boolean, 0, 0, 0, "1", "", "with free_hands: a gun let go by both hands hangs on its sling at your belly (follows you, grip it there) instead of dropping to the floor" },
			{ "free_hands", kind::boolean, 0, 0, 0, "1", "", "hold the gun with the gun-hand grip: let go and it drops to the floor below your hand (no firing); reach down and grip to pick it up" },
			{ "c4_trigger", kind::boolean, 0, 0, 0, "1", "", "with the C4 out, the off-hand trigger throws a C4 (the game's aim button); the gun-hand trigger on the detonator sets them off" },
			{ "squad_signal", kind::boolean, 0, 0, 0, "1", "", "squad order (Q): raise the off hand ahead above your eyes and squeeze the grip" },
			{ "grenade", kind::choice, 0, 0, 0, "physical", "button|physical", "grenades: the game's button (LB), or by hand (belt, pin, throw)" },
			{ "grenade_throw_scale", kind::real, 0.5f, 4.0f, 0.1f, "1.5", "", "physical grenades: launch speed = hand speed times this" },
			{ "grenade_model", kind::boolean, 0, 0, 0, "1", "", "physical grenades: draw the grenade in the off hand while you hold it (the game shows none until the throw)" },
			{ "grenade_mesh", kind::boolean, 0, 0, 0, "1", "", "physical grenades: the held grenade is the game's own model, drawn in your fist while a gun is out (captured when you first take one: the gun goes away for about half a second); off, or with no gun out: the flat picture" },
			{ "pouch_models", kind::boolean, 0, 0, 0, "1", "", "the grenade at its pouch and the knife in its chest holster, the game's own models (each once captured: the first grenade taken, the first time the knife is out) instead of the grab rings while not in hand" },
			{ "reload_sounds", kind::boolean, 0, 0, 0, "1", "", "sounds for magazines, chambering, grenade pouch and pin (a WAV in bfvr_sounds replaces one)" },
			{ "game_sounds", kind::boolean, 0, 0, 0, "1", "", "reload sounds: the game's own magazine-out, magazine-in and bolt sounds where the gun's are found (assault rifles)" },
			{ "drop_discards_ammo", kind::boolean, 0, 0, 0, "1", "", "physical reload: a dropped magazine loses its rounds (off: they go back to the reserve)" },
			{ "extra_meshes", kind::boolean, 0, 0, 0, "1", "", "a second magazine (a new one held while one sits in the gun, the caught one while palming, one falling) drawn as a copy of the game's own magazine; off: the gun's magazine moves to the hand" },
			{ "free_hand_reload", kind::boolean, 0, 0, 0, "1", "", "physical reload, free hands: while the left hand holds the gun, the right hand changes magazines too (0: it only racks)" },
			{ "magazine_snap", kind::boolean, 0, 0, 0, "1", "", "physical reload: the open off hand near the seated magazine snaps onto it with the game's magazine grip, so taking it does not jump (#40)" },
			{ "palm_reload", kind::boolean, 0, 0, 0, "1", "", "physical reload, AR (SCAR-H): a new magazine held at the magwell of a loaded gun turns aside in the hand; B drops the old one into the same hand, turn the hand back to seat the new one, the old one stays in the hand for the pouch" },
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
			{ "reticle_depth", kind::boolean, 0, 0, 0, "1", "", "the dot sits on the surface the barrel line meets (scene depth) and the aim converges there; 0 = aim_convergence metres away" },
			{ "reticle_size", kind::real, 0.3f, 3.0f, 0.1f, "1", "", "aim reticle size, degrees" },
			{ "two_handed", kind::boolean, 0, 0, 0, "1", "", "off hand + grip on the handguard holds the gun with both hands: the barrel points along the line between them" },
			{ "two_hand_mode", kind::choice, 0, 0, 0, "lock", "lock|soft", "two-handed: the barrel lies on the line between your hands (lock), or the hand line only steers the gun from where it was at the grab (soft)" },
			{ "aim_with_body", kind::boolean, 0, 0, 0, "1", "", "with shots_from_gun: the game's own aim stays with the body, so its first-person body never turns with the gun (off: the game aims with the gun, as before)" },
			{ "shots_from_gun", kind::boolean, 0, 0, 0, "1", "", "aim = controller: bullets start at the drawn gun's sight line and fly along its barrel (off: from your eyes towards the point 25 m along it)" },
			{ "shot_line", kind::choice, 0, 0, 0, "gun", "gun|controller", "aim = controller: shots and the dot follow the gun as drawn (its sight), or the controller's pointing ray" },
			{ "sight_ads", kind::boolean, 0, 0, 0, "1", "", "hold the gun's sight to your eye and the game aims down it (the optic's lens clears, ADS accuracy); not for a clear holo while hip_accuracy is 1" },
			{ "sight_dot", kind::boolean, 0, 0, 0, "1", "", "guns with an optic: a red dot at infinity along the sight line, seen only by an eye that looks through the lens, aiming or not (the game's own reticle follows the view's centre, not the sight)" },
			{ "hip_accuracy", kind::real, 0.0f, 1.0f, 0.1f, "1", "", "hip-fire spread of your weapons: 1 = as tight as aiming down sights (sights work without the game's aim), 0 = the game's 3-7 degree hip cone" },
			{ "sight_dot_size", kind::real, 0.1f, 2.0f, 0.05f, "0.6", "", "sight dot size, degrees (the glow; the bright core is about half)" },
			{ "sight_dot_reach", kind::real, 1.0f, 6.0f, 0.5f, "2", "", "how far off the sight line an eye still sees the red dot, cm (the lens is about 1.5; more forgives an unsteady hold)" },
			{ "sight_dot_eyes", kind::choice, 0, 0, 0, "sighting", "sighting|both", "who sees the red dot: only the eye behind the sight (like a real one), or both eyes (one layer for both; a check for headsets that do not show single-eye layers)" },
			{ "sight_dot_test", kind::boolean, 0, 0, 0, "0", "", "test: the red dot shown to both eyes wherever the gun is held (checks that the headset shows the dot layer at all)" },
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
