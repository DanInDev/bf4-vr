#include "common/setting_catalog.hpp"

#include "input/button_map.hpp"

namespace bfvr::settings
{
	namespace
	{
		constexpr std::string_view pages[] = { "Start-up", "View", "Body", "Weapon", "HUD", "Controls", "Advanced" };

		constexpr presentation entries[] = {
			// Start-up: read when the game starts; the launcher shows them, the in-VR panel cannot change them.
			{ "theater", "Start-up", "When the game starts", "VR", "Show the game in the headset", "", false, true },
			{ "stereo", "Start-up", "When the game starts", "Head-tracked 3D", "Off: a flat screen in the headset", "", false, true },
			{ "stereo_first_person", "Start-up", "When the game starts", "Gun stays on the aim", "While your head looks around", "", false, true },
			{ "gamepad", "Start-up", "When the game starts", "Controllers as the pad", "The VR controllers play the game", "", false, true },
			{ "hud_layer", "Start-up", "When the game starts", "Sharp HUD layer", "The HUD drawn on its own layer", "0=Off|1=On|2=Live", false, true },
			{ "xr_runtime", "Start-up", "When the game starts", "VR runtime", "Auto: SteamVR when it is running", "auto=Auto|steamvr=SteamVR|system=Windows", false, true },
			// View
			{ "stereo_render", "View", "Rendering", "3D rendering", "How the two eyes are drawn", "pair=Both eyes per frame|aer=Alternate eyes" },
			{ "foveation", "View", "Rendering", "Foveated rendering", "Softer edges for more frame rate", "off=Off|light=Light|medium=Medium|strong=Strong" },
			{ "view_crop_h", "View", "Rendering", "Rendered width", "Part of the lens that is drawn", "" },
			{ "view_crop_v", "View", "Rendering", "Rendered height", "Part of the lens that is drawn", "" },
			{ "view_crop_bottom", "View", "Rendering", "Trim the bottom edge", "Degrees cut off for a sharper picture", "" },
			{ "ipd_scale", "View", "Scale", "World scale", "1 = your own eye distance", "" },
			{ "gun_scale", "View", "Scale", "Gun size", "On top of life size", "" },
			{ "vignette", "View", "Comfort", "Comfort vignette", "Darkens the edges when the game moves you", "off=Off|turning=Turning|moving=Moving|both=Both" },
			{ "vignette_strength", "View", "Comfort", "Vignette strength", "How far it closes in", "" },
			{ "cutscene_view", "View", "Comfort", "Cutscenes", "A flat screen, or look around in 3D", "screen=Flat screen|look=Look around" },

			// Body
			{ "arm_ik", "Body", "Arms", "Arms follow the controllers", "The soldier's arms reach for your hands", "" },
			{ "body_calibration", "Body", "Calibration", "Body calibration", "T-pose, both triggers, after a level loads", "first=Until done|always=Every level|off=Off" },
			{ "body_height", "Body", "Calibration", "Your height", "Metres; 0 asks again at the next level", "" },
			{ "body_span", "Body", "Calibration", "Your arm span", "Grip to grip, metres (measured)", "" },
			{ "arm_length", "Body", "Arms", "Arm length", "Longer arms reach further", "" },
			{ "shoulder_width", "Body", "Arms", "Shoulder width", "Metres added on each side", "" },
			{ "torso_hands", "Body", "Body", "Lean towards both hands", "Off: the body turns only with your head", "" },
			{ "torso_follow", "Body", "Body", "Body turns with you", "Follows your real body, not only the stick", "" },
			{ "hide_legs", "Body", "Body", "Hide the legs", "They sit oddly under a tracked head", "" },
			{ "off_hand_pose", "Body", "Hands", "Free hand pose", "How the empty off hand is held", "neutral=Neutral|animated=Animated" },
			{ "magazine_grip", "Body", "Hands", "Magazine grip", "Fist, or standing on the palm", "auto=Auto|wrap=Fist|cup=Palm" },
			{ "rack_grip", "Body", "Hands", "Charging handle grip", "Auto: pinched palm down, hooked palm up", "auto=Auto|pinch=Pinch|hook=Hook" },
			{ "shoulder_reach", "Body", "Arms", "Shoulder reach", "Metres a shoulder moves towards a far hand", "", true },
			{ "arm_stretch", "Body", "Arms", "Arm stretch", "Extra length for a hand still out of reach", "", true },
			{ "shoulder_offset", "Body", "Arms", "Shoulder offset", "Shoulders relative to the head, metres", "", true },

			// Weapon
			{ "sight_ads", "Weapon", "Aiming", "Aim by raising the gun", "Sight to your eye: the game aims down it", "" },
			{ "reticle", "Weapon", "Aiming", "Aim dot", "A dot where the shots go", "" },
			{ "reticle_size", "Weapon", "Aiming", "Aim dot size", "Degrees", "" },
			{ "recoil", "Weapon", "Aiming", "Recoil", "The gun kicks in your hand; 0 = off", "" },
			{ "sight_dot", "Weapon", "Aiming", "Red dot in the sight", "Through the lens, aiming or not", "" },
			{ "sight_dot_size", "Weapon", "Aiming", "Red dot size", "Degrees", "" },
			{ "hip_accuracy", "Weapon", "Aiming", "Hip-fire accuracy", "1 = as accurate as aiming, 0 = the game's", "" },
			{ "scope", "Weapon", "Aiming", "Magnifying scopes", "The lens shows the world magnified", "" },
			{ "scope_zoom", "Weapon", "Aiming", "Scope zoom", "Times the game's own magnification", "", true },
			{ "reload", "Weapon", "Reloading", "Reload", "Magazines by hand, or the game's button", "physical=By hand|button=Button" },
			{ "magazine_reach", "Weapon", "Reloading", "Magazine pouch reach", "Metres around the belly and the off-side hip", "", true },
			{ "drop_discards_ammo", "Weapon", "Reloading", "Dropped magazines lose rounds", "Off: they go back to the reserve", "" },
			{ "reload_sounds", "Weapon", "Reloading", "Reload sounds", "Magazines, bolt, pouch and pin", "" },
			{ "accessory_toggle", "Weapon", "Reloading", "Laser, light, magnifier", "Off-hand trigger, or touch it", "trigger=Trigger|touch=Touch|off=Off" },
			{ "grenade", "Weapon", "Grenades and gadgets", "Grenades", "From the belt by hand, or the button", "physical=By hand|button=Button" },
			{ "grenade_throw_scale", "Weapon", "Grenades and gadgets", "Throw strength", "Launch speed times your hand speed", "" },
			{ "c4_trigger", "Weapon", "Grenades and gadgets", "C4 with the triggers", "Off hand throws, gun hand detonates", "" },
			{ "free_hands", "Weapon", "Hands and melee", "Free hands", "Let go and the gun drops; grip to pick up", "" },
			{ "sling", "Weapon", "Hands and melee", "Gun on a sling", "With free hands: it hangs at your belly", "" },
			{ "sling_show_primary", "Weapon", "Hands and melee", "Rifle on the sling", "Shown while the pistol is out", "" },
			{ "slot_models_at_start", "Weapon", "Hands and melee", "Grenade and knife at their slots", "From level start: both are drawn once", "" },
			{ "use_reach", "Weapon", "Hands and melee", "Reach to use", "Off hand ahead + grip: pick up, doors", "" },
			{ "melee_gesture", "Weapon", "Hands and melee", "Stab by thrusting", "A quick push of the gun hand", "" },
			{ "squad_signal", "Weapon", "Hands and melee", "Squad order by hand", "Off hand up above your eyes + grip", "" },
			{ "aim", "Weapon", "Aiming", "Aim with", "Research: the gun hand, or the view", "controller=Controller|head=Head", true },
			{ "gun_pose", "Weapon", "Aiming", "Gun pose", "Research: on the controller ray, or the game's", "aim=On the ray|animated=Animated", true },
			{ "aim_convergence","Weapon", "Aiming", "Aim convergence", "Metres where shots meet the barrel line", "", true },
			{ "knife_pitch", "Weapon", "Hands and melee", "Knife angle", "Degrees the blade is raised", "", true },
			{ "knife_yaw", "Weapon", "Hands and melee", "Knife turn", "Degrees turned left or right", "", true },
			{ "knife_roll", "Weapon", "Hands and melee", "Knife roll", "Degrees rolled about the blade", "", true },
			{ "grips_to_game", "Weapon", "Buttons", "Grips to the game", "Grips also press LB and RB", "", true },
			{ "trigger_press", "Weapon", "Buttons", "Trigger pull to fire", "Higher: a resting finger never fires", "" },
			{ "ads_trigger", "Weapon", "Buttons", "Left trigger aims", "The game's zoom on the left trigger", "", true },
			{ "grab_debug", "Weapon", "Testing", "Show grab zones", "Rings around every grab spot", "", true },

			// HUD
			{ "hud_mode", "HUD", "Placement", "HUD placement", "On the body, or in front of you", "body=On the body|head=In front" },
			{ "hud_distance", "HUD", "Placement", "HUD distance", "Metres in front of you", "" },
			{ "hud_width", "HUD", "Placement", "HUD width", "Metres", "" },
			{ "hud_show_crosshair", "HUD", "Shown", "Crosshair", "The game's crosshair in the middle", "" },
			{ "hud_show_hit_markers", "HUD", "Shown", "Hit markers", "When your shots hit", "" },
			{ "hud_show_damage", "HUD", "Shown", "Damage direction", "Red arcs towards the shooter", "" },
			{ "hud_show_objectives", "HUD", "Shown", "Objectives", "Markers and hints", "" },
			{ "hud_show_prompts", "HUD", "Shown", "Prompts", "Pick up, open", "" },
			{ "hud_show_score", "HUD", "Shown", "Score pop-ups", "Points and awards", "" },
			{ "hud_show_markers", "HUD", "Shown", "World markers", "Squad names and triangles", "" },
			{ "hud_show_minimap", "HUD", "Shown", "Minimap", "On your forearm", "" },
			{ "hud_show_ammo", "HUD", "Shown", "Ammo counter", "Beside the gun", "" },
			{ "hud_show_bottom", "HUD", "Shown", "Weapon and stance icons", "At the bottom", "" },
			{ "hud_show_main", "HUD", "Shown", "Everything else", "Markers, objective text, prompts", "" },
			{ "hud_marker_distance", "HUD", "Placement", "Marker distance", "Metres, HUD on the body", "", true },
			{ "hud_watch_width", "HUD", "Placement", "Minimap size", "Metres on the forearm", "", true },
			{ "hud_ammo_width", "HUD", "Placement", "Ammo counter size", "Metres beside the gun", "", true },

			// Controls
			{ "controls_preset", "Controls", "Buttons", "Button layout", "A preset, or your own below", "default=Default|jump_on_b=Jump on B|custom=Custom" },
			{ "bind_a", "Controls", "Buttons", "Right A", "", "" },
			{ "bind_b", "Controls", "Buttons", "Right B", "", "" },
			{ "bind_x", "Controls", "Buttons", "Left X", "", "" },
			{ "bind_y", "Controls", "Buttons", "Left Y", "", "" },
			{ "bind_left_stick", "Controls", "Buttons", "Left stick click", "", "" },
			{ "bind_right_stick", "Controls", "Buttons", "Right stick click", "", "" },
			{ "bind_menu", "Controls", "Buttons", "Left menu button", "", "" },
			{ "dominant_hand", "Controls", "Moving and turning", "Gun hand", "Left mirrors the controllers", "right=Right|left=Left" },
			{ "move_direction", "Controls", "Moving and turning", "Walk towards", "Where you look, the gun, or the body", "head=Head|gun=Gun|body=Body" },
			{ "right_stick", "Controls", "Moving and turning", "Right stick", "Turn and stance, or the game's aiming", "turn=Turn|look=Aim" },
			{ "turn", "Controls", "Moving and turning", "Turning", "With the right stick", "snap=Snap|smooth=Smooth|off=Off" },
			{ "snap_turn_degrees", "Controls", "Moving and turning", "Snap turn angle", "Degrees per flick", "" },
			{ "stance_flicks", "Controls", "Moving and turning", "Stance by stick", "Right stick down crouches, up stands", "" },
			{ "physical_stance", "Controls", "Moving and turning", "Crouch in the room", "The soldier crouches when you do", "" },
			{ "haptics", "Controls", "Vibration", "Vibration", "Shots, explosions, hits and grabs", "" },
			{ "haptics_strength", "Controls", "Vibration", "Vibration strength", "0 to 1", "" },
			{ "haptics_game", "Controls", "Vibration", "The game's rumble", "Firing, explosions, hits", "" },
			{ "haptics_gestures", "Controls", "Vibration", "Taps for gestures", "Holsters, magazines, grabs, clicks", "" },
			{ "menu_input", "Controls", "Menus", "Menu pointer", "Point and pull the trigger, or the pad", "pointer=Pointer|pad=Pad" },
			{ "vr_panel", "Controls", "Menus", "Hold the menu button for this panel", "A short press stays the game's pause", "" },
			{ "stick_menu", "Controls", "Menus", "Both stick clicks for this panel", "Hold them half a second", "" },
			{ "panel_opacity", "Controls", "Menus", "Panel opacity", "1 = solid", "" },
			{ "menu_hint", "Controls", "Menus", "How-to hint after loading", "Shown for 8 seconds", "" },
			{ "mark_tap", "Controls", "Menus", "Tap both sticks to mark", "Saves a moment for feedback", "" },
			{ "x_menu", "Controls", "Buttons", "X opens the game's menu", "B reloads", "", true },
			{ "neutral_pitch", "Controls", "Moving and turning", "Level horizon", "Only your head pitches the view", "", true },
			{ "physical_pad", "Controls", "Menus", "A real gamepad too", "Off: only the VR controllers", "", true },
		};
	}

	std::span<const presentation> catalog()
	{
		return entries;
	}

	const presentation* presentation_of(std::string_view name)
	{
		for (const presentation& entry : entries)
		{
			if (entry.name == name)
			{
				return &entry;
			}
		}
		return nullptr;
	}

	std::span<const std::string_view> catalog_pages()
	{
		return pages;
	}

	std::string choice_label(std::string_view name, std::string_view value)
	{
		if (name.substr(0, 5) == "bind_")
		{
			input::pad_button button{};
			return input::parse_pad_button(value, button) ? std::string(input::action_label(button)) : std::string(value);
		}
		if (const presentation* entry = presentation_of(name))
		{
			std::string_view rest = entry->choices;
			while (!rest.empty())
			{
				const std::size_t bar = rest.find('|');
				const std::string_view item = rest.substr(0, bar);
				const std::size_t equals = item.find('=');
				if (equals != std::string_view::npos && item.substr(0, equals) == value)
				{
					return std::string(item.substr(equals + 1));
				}
				rest = bar == std::string_view::npos ? std::string_view{} : rest.substr(bar + 1);
			}
		}
		return std::string(value);
	}
}
