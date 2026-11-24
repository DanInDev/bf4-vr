#include "common/setting_catalog.hpp"

#include "input/button_map.hpp"
#include "weapon/crate_guns.hpp"

namespace bfvr::settings
{
	namespace
	{
		constexpr std::string_view pages[] = { "Start-up", "View", "Body", "Weapon", "HUD", "Controls", "Skeleton", "Advanced" };

		constexpr presentation entries[] = {
			// Start-up: read when the game starts; the launcher shows them, the in-VR panel cannot change them.
			{ "theater", "Start-up", "When the game starts", "VR", "Show the game in the headset", "", false, true },
			{ "stereo", "Start-up", "When the game starts", "Head-tracked 3D", "Off: a flat screen in the headset", "", false, true },
			{ "stereo_first_person", "Start-up", "When the game starts", "Gun stays on the aim", "While your head looks around", "", false, true },
			{ "gamepad", "Start-up", "When the game starts", "Controllers as the pad", "The VR controllers play the game", "", false, true },
			{ "hud_layer", "Start-up", "When the game starts", "Sharp HUD layer", "The HUD drawn on its own layer", "0=Off|1=On|2=Live", false, true },
			{ "xr_runtime", "Start-up", "When the game starts", "VR runtime", "Auto: SteamVR when it is running", "auto=Auto|steamvr=SteamVR|system=Windows", false, true },
			// View
			{ "foveation", "View", "Rendering", "Foveated rendering", "Softer edges for more frame rate", "off=Off|light=Light|medium=Medium|strong=Strong" },
			{ "view_crop_h", "View", "Rendering", "Rendered width", "Part of the lens that is drawn", "" },
			{ "view_crop_v", "View", "Rendering", "Rendered height", "Part of the lens that is drawn", "" },
			{ "view_crop_bottom", "View", "Rendering", "Trim the bottom edge", "Degrees cut off for a sharper picture", "" },
			{ "ipd_scale", "View", "Scale", "World scale", "1 = your own eye distance", "" },
			{ "vignette", "View", "Comfort", "Comfort vignette", "Darkens the edges when the game moves you", "off=Off|turning=Turning|moving=Moving|both=Both" },
			{ "vignette_strength", "View", "Comfort", "Vignette strength", "How far it closes in", "" },
			{ "cutscene_view", "View", "Comfort", "Cutscenes", "A flat screen, or look around in 3D", "screen=Flat screen|look=Look around" },
			{ "stream_view", "View", "Recording", "Stream view", "Steady 16:9 picture in the game window, for OBS", "" },
			{ "stream_fov", "View", "Recording", "Stream width", "Degrees across", "" },
			{ "stream_smoothing", "View", "Recording", "Stream steadiness", "Seconds to settle; 0 = follows your head exactly", "" },
			{ "stream_hud", "View", "Recording", "HUD in the stream", "Crosshair, markers and objective text", "" },
			{ "cutscene_hands", "View", "Comfort", "Hands in cutscenes", "The game's animation, or your controllers", "game=The game's|controllers=Controllers" },

			// Body
			{ "body_calibration", "Body", "Calibration", "Body calibration", "T-pose, both triggers, after a level loads", "first=Until done|always=Every level|off=Off" },
			{ "body_height", "Body", "Calibration", "Your height", "Metres; 0 asks again at the next level", "" },
			{ "extra_height", "Body", "Calibration", "Extra height", "cm above the soldier's eye; 12 = the squad's height", "" },
			{ "body_calibrate_now", "Body", "Calibration", "Calibrate now", "Stand in a T and hold both triggers", "" },
			{ "body_span", "Body", "Calibration", "Your arm span", "Grip to grip, metres (measured)", "" },
			{ "shoulder_width", "Body", "Size", "Shoulder width", "Metres added on each side", "" },
			{ "shoulder_drop", "Body", "Size", "Shoulders below the eyes", "Metres; 0 = from your height", "" },
			{ "shoulder_back", "Body", "Size", "Shoulders back", "Metres; + towards you, - forward", "" },
			{ "chest_depth", "Body", "Size", "Chest pouches in front", "Metres from the eyes; 0 = from your height", "" },
			{ "hip_drop", "Body", "Size", "Hip holsters below the eyes", "Metres; 0 = from your height", "" },
			{ "arm_length", "Body", "Size", "Arm length", "Longer arms reach further", "" },
			{ "soldier_radius", "Body", "Body", "Room to the walls", "Metres; 0 = the game's 0.35. From the next spawn", "" },
			{ "gun_hand_pose", "Body", "Hands", "Gun hand without a gun", "Grenade, C4 detonator, the gun in the other hand", "neutral=As you hold the controller|animated=Animated (research)", true },
			{ "off_hand_pose", "Body", "Hands", "Free hand pose", "Animated turns the hand with the game's rifle pose", "neutral=As you hold the controller|animated=Animated (research)", true },
			{ "rack_grip", "Body", "Hands", "Charging handle grip", "Fist: over or under, by where you grab", "fist=Fist|auto=Pinch or hook|pinch=Pinch|hook=Hook" },
			{ "empty_hand_turn", "Body", "Hands", "Empty hands: wrist turn", "Degrees the thumbs turn in, no weapon out", "" },
			// #151: what is drawn on your body; changes show live on the chest.
			{ "slot_models_ahead", "Body", "Items on your body", "Grenade and knife off the chest", "Metres ahead of their grab spots", "" },
			{ "knife_holster_right", "Body", "Items on your body", "Knife: right", "cm (- left)", "" },
			{ "knife_holster_up", "Body", "Items on your body", "Knife: up", "cm (- down)", "" },
			{ "knife_holster_forward", "Body", "Items on your body", "Knife: forward", "cm (- towards you)", "" },
			{ "knife_holster_turn", "Body", "Items on your body", "Knife: turn", "Degrees about the blade: 90 edge left", "" },
			{ "knife_holster_tilt", "Body", "Items on your body", "Knife: tip", "Degrees, + handle towards you", "" },
			{ "knife_holster_lean", "Body", "Items on your body", "Knife: lean", "Degrees sideways", "" },
			{ "grenade_pouch_right", "Body", "Items on your body", "Grenade: right", "cm (- left)", "" },
			{ "grenade_pouch_up", "Body", "Items on your body", "Grenade: up", "cm (- down)", "" },
			{ "grenade_pouch_forward", "Body", "Items on your body", "Grenade: forward", "cm (- towards you)", "" },
			{ "grenade_pouch_turn", "Body", "Items on your body", "Grenade: turn", "Degrees about the vertical", "" },
			{ "shoulder_reach", "Body", "Arms", "Shoulder reach", "Metres a shoulder moves towards a far hand", "", true },
			{ "arm_stretch", "Body", "Arms", "Arm stretch", "Extra length for a hand still out of reach", "", true },
			{ "shoulder_offset", "Body", "Arms", "Shoulder offset", "Shoulders relative to the head, metres", "", true },

			// Weapon
			// #80 (owner 2026-11-24): the Armory: any crate gun, without attachments, into your hands in the normal campaign.
			{ "crate_primary", "Weapon", "Armory", "Primary gun", "Bare, no attachments: in your hand a second after you stop", weapon::crate::choices },
			{ "crate_secondary", "Weapon", "Armory", "Second gun", "Bare, no attachments: into the second slot", weapon::crate::choices },
			{ "unlock_all_weapons", "Weapon", "Armory", "All crate weapons", "Every gun in the game's weapon crates can be picked too", "" },
			{ "hip_accuracy", "Weapon", "Aiming", "Hip-fire accuracy", "1 = as accurate as aiming, 0 = the game's", "" },
			{ "scope_eye_box", "Weapon", "Aiming", "Scope eye box", "cm off the sight line the whole picture stays", "" },
			{ "sight_lens_cone", "Weapon", "Aiming", "Clear lens angle", "Degrees off the sight line the lens stays clear", "", true },
			{ "sight_lens_fov", "Weapon", "Aiming", "Reticle size", "Aiming view it is sized for (0 = the optic's own)", "" },
			{ "sight_lens_brightness", "Weapon", "Aiming", "Reticle brightness", "", "" },
			{ "scope_zoom", "Weapon", "Aiming", "Scope zoom", "Times the game's own magnification", "", true },
			{ "reload", "Weapon", "Reloading", "Reload", "Magazines by hand, or the game's button", "physical=By hand|button=Button" },
			{ "physical_reload_all", "Weapon", "Reloading", "Hand reload for every gun", "Off: belt-fed LMGs and the revolver reload the game's way", "" },
			{ "revolver_by_hand", "Weapon", "Reloading", "Revolver by hand", "Off: the revolver reloads the game's way (B)", "" },
			{ "belts_by_hand", "Weapon", "Reloading", "Machine-gun belts by hand", "Off: belt-fed guns reload the game's way (B)", "" },
			{ "shells_by_hand", "Weapon", "Reloading", "Shotgun shells by hand", "Off: shotguns reload the game's way (B)", "" },
			{ "manual_cycle", "Weapon", "Reloading", "Bolt and pump by hand", "Work the bolt or pump after each shot", "" },
			{ "magazine_reach", "Weapon", "Reloading", "Magazine pouch reach", "Metres around the belly and the off-side hip", "", true },
			{ "magazine_palming", "Weapon", "Reloading", "Palming (AR)", "Catch the old magazine beside the new one", "" },
			{ "drop_discards_ammo", "Weapon", "Reloading", "Dropped magazines lose rounds", "Off: they go back to the reserve", "" },
			{ "support_slide_back", "Weapon", "Hands and melee", "Handguard reach back", "Metres behind the game's grip spot", "", true },
			{ "support_slide_forward", "Weapon", "Hands and melee", "Handguard reach forward", "Metres ahead of the game's grip spot", "", true },
			{ "rack_over_out", "Skeleton", "Rack fist placement", "Overhand: out to the side", "Metres from the knob, away from the gun", "" },
			{ "rack_over_up", "Skeleton", "Rack fist placement", "Overhand: up", "Metres above the knob", "" },
			{ "rack_under_out", "Skeleton", "Rack fist placement", "Underhand: out to the side", "Metres from the knob, away from the gun", "" },
			{ "rack_under_down", "Skeleton", "Rack fist placement", "Underhand: down", "Metres below the knob", "" },
			{ "pose_edit", "Skeleton", "Pose editor", "Edit grip", "The sliders tune this grip, live wherever it shows", "off=Off|relaxed=Free hand|reach=Reaching|fist=Fist|cup=Cupped|palm=Palming|magazine_ak=AK magazine|magazine_ar=AR magazine|hook=Handle hook|pinch=Handle pinch|underhand=Handle underhand|rack_over=Rack fist over|rack_under=Rack fist under" },
			{ "pose_show", "Skeleton", "Pose editor", "Hold it on", "That hand keeps the grip while you tune it", "off=Off|left=Left hand|right=Right hand" },
			{ "pose_file", "Skeleton", "Pose editor", "Pose file", "bfvr_poses.ini: Save keeps them, Load undoes", "keep=Live|save=Save|load=Load" },
			{ "pose_thumb_bend", "Skeleton", "Pose editor", "Thumb bend", "Degrees at each joint, + closes", "" },
			{ "pose_thumb_side", "Skeleton", "Pose editor", "Thumb sideways", "Degrees at the knuckle, + towards the thumb", "" },
			{ "pose_index_bend", "Skeleton", "Pose editor", "Index bend", "Degrees at each joint, + closes", "" },
			{ "pose_index_side", "Skeleton", "Pose editor", "Index sideways", "Degrees at the knuckle, + towards the thumb", "" },
			{ "pose_middle_bend", "Skeleton", "Pose editor", "Middle bend", "Degrees at each joint, + closes", "" },
			{ "pose_middle_side", "Skeleton", "Pose editor", "Middle sideways", "Degrees at the knuckle, + towards the thumb", "" },
			{ "pose_ring_bend", "Skeleton", "Pose editor", "Ring bend", "Degrees at each joint, + closes", "" },
			{ "pose_ring_side", "Skeleton", "Pose editor", "Ring sideways", "Degrees at the knuckle, + towards the thumb", "" },
			{ "pose_pinky_bend", "Skeleton", "Pose editor", "Pinky bend", "Degrees at each joint, + closes", "" },
			{ "pose_pinky_side", "Skeleton", "Pose editor", "Pinky sideways", "Degrees at the knuckle, + towards the thumb", "" },
			{ "pose_thumb_across", "Skeleton", "Pose editor", "Thumb base across", "+ folds it over the palm, - stands it up", "" },
			{ "pose_thumb_out", "Skeleton", "Pose editor", "Thumb base out", "+ opens it away from the index, - presses it to it", "" },
			{ "reload_sounds", "Weapon", "Reloading", "Reload sounds", "Magazines, bolt, pouch and pin", "" },
			{ "grenade_throw_scale", "Weapon", "Grenades and gadgets", "Throw strength", "Launch speed times your hand speed", "" },
			{ "pin_volume", "Weapon", "Grenades and gadgets", "Pin pull volume", "0 silent, 1 full", "" },
			{ "free_hands_release", "Weapon", "Hands and melee", "Let-go delay", "Seconds before an opened hand drops the gun", "", true },
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
			{ "zone_edit", "Weapon", "Tune grab zones", "Zone to tune", "On the gun in your hand; rings show while tuning", "off=Off|handle=Charging handle / bolt / pump|magazine=Magazine / loading port|lid=Lid / revolver frame" },
			{ "zone_right", "Weapon", "Tune grab zones", "Right", "Centimetres, seen from behind the gun (- left)", "" },
			{ "zone_up", "Weapon", "Tune grab zones", "Up", "Centimetres (- down)", "" },
			{ "zone_forward", "Weapon", "Tune grab zones", "Forward", "Centimetres along the barrel (- back)", "" },
			{ "zone_size", "Weapon", "Tune grab zones", "Size", "Times the zone's normal size", "" },
			{ "zone_file", "Weapon", "Tune grab zones", "Save", "Save writes every gun to bfvr_zones.ini", "keep=-|save=Save all guns|load=Undo unsaved|reset=Reset this zone" },
			{ "zone_tunes", "Weapon", "Tune grab zones", "Use tuned zones", "Off: every gun as built, to compare", "" },

			// HUD
			{ "hud_mode", "HUD", "Placement", "HUD placement", "On the body, or in front of you", "body=On the body|head=In front" },
			{ "hud_distance", "HUD", "Placement", "HUD distance", "Metres in front of you", "" },
			{ "hud_width", "HUD", "Placement", "HUD width", "Metres", "" },
			// #156: placement live on the VR view (with the panel open the minimap and the gun counters stay shown).
			{ "hud_right", "HUD", "Placement", "HUD in front: right", "cm (- left); HUD in front of you", "" },
			{ "hud_up", "HUD", "Placement", "HUD in front: up", "cm (- down); HUD in front of you", "" },
			{ "hud_watch_width", "HUD", "Minimap on the forearm", "Minimap size", "Metres", "" },
			{ "hud_watch_along", "HUD", "Minimap on the forearm", "Along the arm", "cm from the wrist towards the elbow", "" },
			{ "hud_watch_side", "HUD", "Minimap on the forearm", "Around the arm", "cm towards the thumb (- away)", "" },
			{ "hud_ammo_width", "HUD", "Counters at the gun", "Counters size", "Health, ammo, grenades; metres", "" },
			{ "hud_ammo_right", "HUD", "Counters at the gun", "Right", "cm (- left), seen from behind the gun", "" },
			{ "hud_ammo_up", "HUD", "Counters at the gun", "Up", "cm (- down)", "" },
			{ "hud_ammo_forward", "HUD", "Counters at the gun", "Forward", "cm along the barrel (- back)", "" },
			{ "hud_show_crosshair", "HUD", "Shown", "Crosshair", "The game's crosshair in the middle", "" },
			{ "hud_show_hit_markers", "HUD", "Shown", "Hit markers", "When your shots hit", "" },
			{ "hud_show_damage", "HUD", "Shown", "Damage direction", "Red arcs; off: they follow the head", "" },
			{ "hud_show_objectives", "HUD", "Shown", "Objectives", "Markers and hints", "" },
			{ "hud_show_prompts", "HUD", "Shown", "Prompts", "Pick up, open", "" },
			{ "hud_show_score", "HUD", "Shown", "Score pop-ups", "Points and awards", "" },
			{ "hud_show_markers", "HUD", "Shown", "World markers", "Squad names and triangles", "" },
			{ "hud_show_minimap", "HUD", "Shown", "Minimap", "On your forearm", "" },
			{ "hud_show_ammo", "HUD", "Shown", "Health and ammo", "At the gun on a mag check", "" },
			{ "hud_show_bottom", "HUD", "Shown", "Weapon and stance icons", "At the bottom", "" },
			{ "hud_show_main", "HUD", "Shown", "Everything else", "Markers, objective text, prompts", "" },
			{ "hud_marker_distance", "HUD", "Placement", "Marker distance", "Metres, HUD on the body", "", true },
			{ "hud_watch_pitch", "HUD", "Watch glance", "Head down", "Degrees below the horizon to show the minimap", "", true },
			{ "hud_watch_reach", "HUD", "Watch glance", "Wrist distance", "Metres from the eyes at most", "", true },
			{ "hud_watch_facing", "HUD", "Watch glance", "Wrist facing", "Degrees the inner wrist may turn away", "", true },
			{ "hud_watch_dwell", "HUD", "Watch glance", "Glance time", "Seconds before the minimap shows", "", true },
			{ "hud_ammo_gesture", "HUD", "Placement", "Show ammo by", "Health, ammo and grenades at the gun", "magcheck=Mag check|side=Side of the gun|both=Both" },
			{ "hud_magazine_check", "HUD", "Shown", "Rounds in a held magazine", "Look at a magazine in your hand", "" },
			{ "hud_magcheck_roll", "HUD", "Mag check", "Roll", "Degrees upside down at least", "", true },
			{ "hud_magcheck_hold", "HUD", "Mag check", "Hold time", "Seconds before the counters show", "", true },

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
			{ "drive_controls", "Controls", "Driving", "Drive with", "A wheel with the triggers as pedals, or the left stick", "wheel=Steering wheel|stick=Left stick" },
			{ "wheel_lock_degrees", "Controls", "Driving", "Wheel turn for full lock", "Degrees; less is quicker steering", "", true },
			{ "right_stick", "Controls", "Moving and turning", "Right stick", "Turn and stance, or the game's aiming", "turn=Turn|look=Aim" },
			{ "turn", "Controls", "Moving and turning", "Turning", "With the right stick", "snap=Snap|smooth=Smooth|off=Off" },
			{ "snap_turn_degrees", "Controls", "Moving and turning", "Snap turn angle", "Degrees per flick", "" },
			{ "stance_flicks", "Controls", "Moving and turning", "Stance by stick", "Right stick down crouches, up stands", "" },
			{ "physical_stance", "Controls", "Moving and turning", "Crouch in the room", "The soldier crouches when you do", "" },
			{ "haptics", "Controls", "Vibration", "Vibration", "Shots, explosions, hits and grabs", "" },
			{ "haptics_strength", "Controls", "Vibration", "Vibration strength", "0 to 1", "" },
			{ "panel_opacity", "Controls", "Menus", "Panel opacity", "1 = solid", "" },
			{ "menu_hint", "Controls", "Menus", "How-to hint after loading", "Shown for 8 seconds", "" },
			{ "mark_tap", "Controls", "Menus", "Tap both sticks to mark", "Saves a moment for feedback", "" },
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
