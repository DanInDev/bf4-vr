#include "common/settings_store.hpp"

#include <windows.h>

#include <fstream>
#include <iterator>
#include <sstream>

#include "common/config.hpp"
#include "common/log.hpp"
#include "input/button_map.hpp"
#include "weapon/crate_guns.hpp"

namespace bfvr::settings
{
	namespace
	{
		// Live tunables. Start-up-only keys (render_size, xr_runtime, stereo, hud_layer, research) stay
		// in config: they cannot change while the game runs.
		const definition definitions[] = {
			{ "gun_scale", kind::real, 0.25f, 4.0f, 0.05f, "1", "", "apparent gun and arms size, 1 = life size on the controllers" },
			{ "view_crop_h", kind::real, 0.4f, 1.0f, 0.02f, "1", "", "rendered part of the headset FOV, horizontal" },
			{ "view_crop_v", kind::real, 0.4f, 1.0f, 0.02f, "1", "", "rendered part of the headset FOV, vertical" },
			{ "view_crop_bottom", kind::real, 0.0f, 30.0f, 1.0f, "0", "", "degrees cut off the bottom of the view (the top stays): the lens shows less below than above, so the image gets sharper; raise until the bottom edge shows, then back off" },
			{ "foveation", kind::choice, 0, 0, 0, "light", "off|light|medium|strong", "fixed foveated rendering (NVIDIA): the edge of each eye's image is shaded at a lower rate, the lens blurs it anyway; more frame rate" },
			{ "vignette", kind::choice, 0, 0, 0, "off", "off|turning|moving|both", "comfort vignette: darken the edge of the view while the game turns or moves you (not your head)" },
			{ "vignette_strength", kind::real, 0.1f, 1.0f, 0.05f, "0.7", "", "comfort vignette: how far it closes in at full turn or speed" },
			{ "ipd_scale", kind::real, 0.5f, 2.0f, 0.05f, "1", "", "eye distance multiplier (world scale), 1 = the runtime's IPD" },
			{ "hud_distance", kind::real, 0.3f, 20.0f, 0.1f, "1.5", "", "HUD layer distance, metres" },
			{ "hud_width", kind::real, 0.2f, 10.0f, 0.05f, "1.3", "", "HUD layer width, metres" },
			{ "hud_mode", kind::choice, 0, 0, 0, "body", "head|body", "HUD head-locked, or projected (markers on targets) with minimap and ammo on the body" },
			{ "hud_marker_distance", kind::real, 2.0f, 50.0f, 1.0f, "10", "", "hud_mode = body: distance of the projected HUD, metres" },
			{ "hud_watch_width", kind::real, 0.05f, 0.3f, 0.01f, "0.12", "", "hud_mode = body: minimap width on the forearm, metres" },
			{ "hud_watch_pitch", kind::real, 0.0f, 60.0f, 5.0f, "25", "", "minimap watch glance: the head looks at least this many degrees below the horizon (0 = any)" },
			{ "hud_watch_reach", kind::real, 0.3f, 0.9f, 0.05f, "0.5", "", "minimap watch glance: the inner wrist at most this far from the eyes, metres" },
			{ "hud_watch_facing", kind::real, 20.0f, 80.0f, 5.0f, "40", "", "minimap watch glance: the inner wrist faces the eyes within this many degrees" },
			{ "hud_watch_dwell", kind::real, 0.1f, 1.0f, 0.05f, "0.25", "", "minimap watch glance: seconds the glance holds before the map shows" },
			{ "hud_ammo_width", kind::real, 0.05f, 0.3f, 0.01f, "0.12", "", "hud_mode = body: width of the health, ammo and grenade counters at the gun, metres" },
			{ "hud_ammo_gesture", kind::choice, 0, 0, 0, "magcheck", "magcheck|side|both", "hud_mode = body: how the gun shows health, ammo and grenades: rolled upside down with the magazine looked at, its side turned to you and held a second, or both" },
			{ "hud_magcheck_roll", kind::real, 90.0f, 180.0f, 5.0f, "120", "", "mag check: degrees the gun is rolled from upright at least" },
			{ "hud_magazine_check", kind::boolean, 0, 0, 0, "1", "", "a magazine in the off hand (from the gun or the pouch) looked at shows its rounds beside it" },
			{ "hud_magcheck_hold", kind::real, 0.1f, 1.5f, 0.05f, "0.4", "", "mag check: seconds the gun is held steady before the counters show" },
			{ "hud_show_main", kind::boolean, 0, 0, 0, "1", "", "HUD layer: everything above the minimap and ammo (markers, objective text, prompts)" },
			{ "hud_show_minimap", kind::boolean, 0, 0, 0, "1", "", "HUD: minimap" },
			{ "hud_show_ammo", kind::boolean, 0, 0, 0, "1", "", "HUD: health, ammo and grenade counters" },
			{ "hud_show_bottom", kind::boolean, 0, 0, 0, "0", "", "HUD: the weapon cross and stance icons at the bottom (off by default: in VR the guns are in your hands and on your body; headset 2026-11-15)" },
			{ "stereo_render", kind::choice, 0, 0, 0, "pair", "aer|pair", "both eyes from one game frame, or alternate eyes" },
			{ "scene_aim", kind::choice, 0, 0, 0, "game", "game|head|controller", "in a scripted scene where the game holds the weapon (the Baku car's grenade launcher): the game's own aiming with the right stick and left trigger, the shot where you look, or along the controller" },
			{ "cutscene_view", kind::choice, 0, 0, 0, "screen", "screen|look", "while the game moves the camera (cutscenes, the death fall): a flat screen, or 3D with your head looking around in the scene" },
			{ "stream_view", kind::boolean, 0, 0, 0, "1", "", "the game window shows a steadied, level 16:9 picture of the left eye for recording (OBS Window Capture); off: the raw eye image" },
			{ "stream_fov", kind::real, 60.0f, 110.0f, 5.0f, "80", "", "stream view: degrees across; narrower leaves more room to steady a fast turn" },
			{ "stream_smoothing", kind::real, 0.0f, 1.0f, 0.05f, "0.3", "", "stream view: seconds the camera takes to settle while the head is held still (faster turns are followed closer); 0 = no steadying" },
			{ "stream_hud", kind::boolean, 0, 0, 0, "1", "", "stream view: the HUD layer (crosshair, markers, objective text) drawn into the picture" },
			{ "cutscene_hands", kind::choice, 0, 0, 0, "game", "game|controllers", "in cutscenes (the game moving the camera itself): the arms, gun and fingers as the game animates them (game), or following your controllers as in play (controllers)" },
			{ "shoulder_offset", kind::vec3, -0.5f, 0.5f, 0.0f, "0 0 0", "", "shoulders relative to the head, metres (x right, y up, z back)" },
			{ "shoulder_reach", kind::real, 0.0f, 0.3f, 0.01f, "0.12", "", "a shoulder moves up to this far towards a hand out of reach before the arm stretches, metres" },
			{ "arm_ik", kind::boolean, 0, 0, 0, "1", "", "first-person arms follow the VR controllers" },
			{ "arm_stretch", kind::real, 0.0f, 0.6f, 0.05f, "0.25", "", "an arm still out of reach (after arm_length and shoulder_reach) may get this much longer (pipe arms: reach > 1 = out of reach)" },
			{ "right_stick", kind::choice, 0, 0, 0, "turn", "look|turn", "right stick aims, or turns and changes stance" },
			{ "turn", kind::choice, 0, 0, 0, "snap", "snap|smooth|off", "right stick turn style (right_stick = turn)" },
			{ "snap_turn_degrees", kind::integer, 5, 90, 5, "30", "", "angle per snap turn" },
			{ "dominant_hand", kind::choice, 0, 0, 0, "right", "right|left", "gun hand: left mirrors the pad and swaps the hand roles" },
			{ "reload", kind::choice, 0, 0, 0, "physical", "button|physical", "reload: the game's button, or by hand (AK: pull the magazine, AR: B releases it; chest pouch)" },
			{ "physical_reload_all", kind::boolean, 0, 0, 0, "0", "", "the hand reload for every gun, also belt-fed LMGs and the revolver, which it treats as box magazines (0: those keep the game's own reload until their own hand reloads exist)" },
			{ "revolver_by_hand", kind::boolean, 0, 0, 0, "1", "", "the MP-412 REX revolver by hand: B breaks it open (the cases fall out), a speedloader from the pouch at the open cylinder, a flick of the gun up or a push on the barrel shuts it; 0: the game's own reload (B)" },
			{ "belts_by_hand", kind::boolean, 0, 0, 0, "1", "", "belt-fed LMGs (M249, M240, MG4, Pecheneg, Type 88): swing the lid up by its back end, B with the hand under the box takes it off, a new box from the pouch, the belt end from the box laid on the feed tray, the lid pushed shut; 0: the game's own reload (B)" },
			{ "shells_by_hand", kind::boolean, 0, 0, 0, "1", "", "tube shotguns (870, M1014, SPAS-12, QBS-09, UTAS): take a shell from the pouch and push it into the loading port under the gun, one at a time, then pump (or rack) to chamber; 0: the game's own reload (B)" },
			{ "shell_across", kind::boolean, 0, 0, 0, "1", "", "a shotgun shell in the off hand lies across the closed fingers; 0: as it sits in the gun (it stuck out through the fingers)" },
			{ "pump_at_rest", kind::boolean, 0, 0, 0, "1", "", "a pump shotgun's fore-end stays forward after a shot (the hand pumps it); 0: the game's own pump animation plays after every shot as well" },
			{ "lid_hand_lock", kind::boolean, 0, 0, 0, "1", "", "belt-fed guns: the hand that lifts the feed cover is held on it while it swings, so you see you have it; 0: the hand follows the controller" },
			{ "handle_both_sides", kind::boolean, 0, 0, 0, "1", "", "a charging handle that works from both sides on the real gun (FAMAS, AK-12, P90, G36C, QBB-95) can be racked from either side; 0: only from its drawn side" },
			{ "bolt_steps", kind::boolean, 0, 0, 0, "1", "", "bolt-action rifles (SRS, FY-JS, Scout, SV-98): work the bolt in its four real steps, lift it, draw it back, push it forward, turn it down (the trigger works only with it down); 0: grab and pull back once" },
			{ "manual_cycle", kind::boolean, 0, 0, 0, "1", "", "bolt actions (SRS, FY-JS, Scout, SV-98) and the Hawk's pump: after each shot work the bolt or the fore-end by hand (grab it, pull back, let go) before the next; 0: the game cycles them by itself" },
			{ "gun_points", kind::boolean, 0, 0, 0, "1", "", "each gun's own charging handle, magazine grab point and the way its magazine comes out, measured from the game's own reload animations (0: one point per AR/AK variant, every magazine straight down, as before)" },
			{ "grips_to_game", kind::boolean, 0, 0, 0, "0", "", "controller grips also reach the game as LB/RB (grenade, fire in some layouts); off: they are the mod's" },
			{ "x_menu", kind::boolean, 0, 0, 0, "1", "", "left X opens the game's menu (Start); the right B reloads" },
			{ "trigger_press", kind::real, 0.2f, 0.9f, 0.05f, "0.55", "", "how far a trigger is pulled before the game sees it (0..1); released 0.1 below; a finger resting on the trigger reads 0.25-0.5" },
			{ "ads_trigger", kind::boolean, 0, 0, 0, "0", "", "left trigger = the game's aim down sights (zooms the view)" },
			{ "hud_show_crosshair", kind::boolean, 0, 0, 0, "0", "", "HUD: the game's crosshair in the middle" },
			{ "hud_show_hit_markers", kind::boolean, 0, 0, 0, "0", "", "HUD: the hit marker when your shots hit (off by default: with the body HUD it showed far below the view, #51)" },
			{ "hud_show_damage", kind::boolean, 0, 0, 0, "0", "", "HUD: the red arcs showing where damage comes from (off by default: drawn on the body HUD they follow the head, not the shooter; headset 2026-11-14)" },
			{ "hud_show_objectives", kind::boolean, 0, 0, 0, "1", "", "HUD: objective markers and hints" },
			{ "hud_show_prompts", kind::boolean, 0, 0, 0, "1", "", "HUD: interaction prompts (pick up, open)" },
			{ "hud_show_score", kind::boolean, 0, 0, 0, "0", "", "HUD: score pop-ups and awards (off by default: with the body HUD they showed far below the view, #51)" },
			{ "hud_show_markers", kind::boolean, 0, 0, 0, "1", "", "HUD: markers in the world (squad names and triangles)" },
			{ "body_calibration", kind::choice, 0, 0, 0, "first", "first|always|off", "body calibration (T-pose, both triggers held two seconds) after a level loads: until done once, once per level (not again when a death reloads it), or never; it sets body_height, body_span, arm_length and shoulder_width" },
			{ "body_height", kind::real, 0.0f, 2.2f, 0.01f, "0", "", "your height from the calibration, metres (0 = not calibrated: the next level asks); scales the pouches, holsters and shoulders (1.78 = as laid out)" },
			{ "body_span", kind::real, 0.0f, 2.5f, 0.01f, "0", "", "your arm span, grip to grip, from the calibration, metres (information)" },
			{ "body_calibrate_now", kind::boolean, 0, 0, 0, "0", "", "switch on to start the T-pose calibration at once; it switches itself off" },
			{ "shoulder_drop", kind::real, 0.0f, 0.45f, 0.01f, "0", "", "your shoulder joints below your eyes, metres (0 = from body_height: 0.22 at 1.78 m)" },
			{ "shoulder_back", kind::real, -0.15f, 0.15f, 0.01f, "0", "", "the drawn shoulders moved back towards you (+) or forward (-), metres (headset 2026-11-14: they sat a little ahead)" },
			{ "chest_depth", kind::real, 0.0f, 0.30f, 0.01f, "0", "", "the chest pouches and the knife in front of your eyes, metres (0 = from body_height: 0.10 at 1.78 m)" },
			{ "hip_drop", kind::real, 0.0f, 1.10f, 0.01f, "0", "", "the hip holsters below your eyes, metres (0 = from body_height: 0.75 at 1.78 m)" },
			{ "arm_length", kind::real, 0.8f, 1.5f, 0.05f, "1.15", "", "first-person arms stretched by this (reach)" },
			{ "hide_legs", kind::boolean, 0, 0, 0, "1", "", "first-person legs hidden (they sit oddly under a tracked head)" },
			{ "soldier_radius", kind::real, 0.0f, 0.35f, 0.05f, "0", "", "the soldier's collision radius in metres, 0 = the game's 0.35; smaller lets you stand closer to walls and lean out of windows; applied when the soldier spawns (a level's first spawn included); a change in a level applies at the next respawn or checkpoint restart (#53)" },
			{ "torso_hands", kind::boolean, 0, 0, 0, "0", "", "the torso also leans towards two hands held together in front (off: it turns only when the head turns past ~40 degrees, or with the stick)" },
			{ "torso_follow", kind::boolean, 0, 0, 0, "1", "", "the shoulders turn with your real body (the head past ~40 degrees), not only with the stick" },
			{ "aim_follows_torso", kind::boolean, 0, 0, 0, "1", "", "the game's own aim faces your drawn body as it turns in the room (the knife, C4, squad order and muzzle flash go that way); off: only the stick turns it" },
			{ "gun_hand_pose", kind::choice, 0, 0, 0, "neutral", "animated|neutral", "the gun hand while the off hand carries the gun, or holding the hand grenade or the C4 detonator: the gun grip as you hold the controller (neutral), or the game's animated hold turned by the controller (animated); launchers such as the M320 keep their own hold" },
			{ "off_hand_pose", kind::choice, 0, 0, 0, "neutral", "animated|neutral", "the free off hand's wrist: the gun hand's grip mirrored, as you hold the controller, a held magazine upright in the fist (neutral), or the game's handguard hold turned by the controller (animated)" },
			{ "magazine_by_gun", kind::boolean, 0, 0, 0, "1", "", "a held magazine sits as deep in the hand as the game's own hand takes that gun's magazine (a short one nearer its top, a long one deeper); 0 = every magazine as the SCAR-H's (AR) or AK-12's (AK)" },
			{ "magazine_grip", kind::choice, 0, 0, 0, "auto", "auto|wrap|cup", "a held magazine: wrapped in the fist, or standing on the upturned palm with the fingers closing on it (auto: cup once the palm faces up)" },
			{ "flash_at_gun", kind::boolean, 0, 0, 0, "1", "", "the muzzle flash, its smoke and sparks drawn at the gun in your hand (the game draws them where its own flat gun would be)" },
			{ "light_at_gun", kind::boolean, 0, 0, 0, "1", "", "the weapon's flashlight and laser light shine where the gun in your hand points (the game turned them with your head)" },
			{ "hand_collision", kind::boolean, 0, 0, 0, "1", "", "the free hand is drawn on the gun's surface, never inside its receiver or handguard; a fast slap through the gun ends on the side it came from" },
			{ "pistol_two_hands", kind::boolean, 0, 0, 0, "1", "", "pistols and the revolver: close the off hand round the gun hand to hold them with both hands (steadier aim); 0: one-handed only" },
			{ "support_lock", kind::boolean, 0, 0, 0, "1", "", "two-handed: the off hand stays on the handguard where it grabbed until you let go (off: it slides along with the controller within the range)" },
			{ "foregrip_hold", kind::boolean, 0, 0, 0, "1", "", "two-handed, a gun with a foregrip (vertical, angled, potato): the off hand holds the grip where the game's own hand does, with its grip pose, instead of sliding along the handguard" },
			{ "support_slide_back", kind::real, 0.0f, 0.2f, 0.01f, "0.06", "", "two-handed: the off hand may hold the gun up to this far behind the game's own handguard spot, metres (towards the magazine)" },
			{ "support_slide_forward", kind::real, 0.0f, 0.3f, 0.01f, "0.08", "", "two-handed: the off hand may hold the gun up to this far ahead of the game's own handguard spot, metres (towards the muzzle)" },
			{ "rack_over_out", kind::real, 0.0f, 0.12f, 0.005f, "0.005", "", "rack_grip fist, overhand: the fist this far out from the charging handle's knob on the handle's side, metres (it holds the knob at its inner edge, outside the gun)" },
			{ "rack_over_up", kind::real, -0.05f, 0.08f, 0.005f, "0.005", "", "rack_grip fist, overhand: the fist this far above the knob, metres" },
			{ "rack_under_out", kind::real, 0.0f, 0.12f, 0.005f, "0.005", "", "rack_grip fist, underhand: the fist this far out from the knob on the handle's side, metres" },
			{ "rack_under_down", kind::real, -0.05f, 0.10f, 0.005f, "0.005", "", "rack_grip fist, underhand: the fist this far below the knob, metres" },
			{ "rack_grip", kind::choice, 0, 0, 0, "fist", "fist|auto|pinch|hook", "the charging handle in a closed fist, overhand when grabbed beside or above it, underhand from the larger zone below, the hand locked on the knob (fist); between the thumb and the side of the index (pinch), or inside the curled index (hook); auto: the pinch with the palm down on it, hooked in the index and middle with the palm up under it" },
			{ "shoulder_width", kind::real, -0.1f, 0.2f, 0.01f, "0", "", "first-person shoulders moved outwards by this, metres per side" },
			{ "recoil", kind::real, 0.0f, 3.0f, 0.1f, "1", "", "the drawn gun kicks per shot by the gun's own recoil data (muzzle up and sideways, back along the barrel; softer two-handed), 0 = off; the shots follow the kick" },
			{ "one_hand_recoil", kind::real, 0.0f, 1.0f, 0.1f, "1", "", "a rifle, shotgun, LMG or sniper rifle fired without the support hand: ~5x the climb of a two-handed shot, ~2.5x the push back, thrown about shot by shot, slower to settle (#60); pistols are not affected; 0 = the plain one-handed kick" },
			{ "melee_gesture", kind::boolean, 0, 0, 0, "1", "", "a quick forward thrust of the gun hand is the game's melee (knife stab)" },
			{ "knife_pitch", kind::real, -180.0f, 180.0f, 5.0f, "145", "", "held knife: degrees the blade is raised to lie along the controller ray (lower = blade tilts down)" },
			{ "knife_yaw", kind::real, -180.0f, 180.0f, 5.0f, "-15", "", "degrees the held knife is turned about the controller's up (+ = to the right)" },
			{ "knife_roll", kind::real, -180.0f, 180.0f, 5.0f, "170", "", "degrees the held knife is rolled about the controller ray (+ = clockwise seen from behind)" },
			{ "weapon_pickup", kind::boolean, 0, 0, 0, "1", "", "grip a weapon lying on the ground with either hand (or reach down over it) to take it: the game's pickup (R held while the hand squeezes)" },
			{ "use_reach", kind::boolean, 0, 0, 0, "1", "", "reach out ahead with the off hand (away from the gun) and squeeze its grip: the game's use (pick up, doors, prompts)" },
			{ "knife_holster_turn", kind::real, -180.0f, 180.0f, 15.0f, "90", "", "degrees the knife on the chest (blade down, handle up) is turned about its blade: 90 = edge to your left, -90 = to your right, 0 = ahead" },
			{ "slot_models_ahead", kind::real, 0.0f, 0.3f, 0.01f, "0.08", "", "the grenade and the knife at their slots are drawn this far (metres) ahead of where you grab them, on your chest instead of inside it (the grab zones stay)" },
			{ "slot_models_at_start", kind::boolean, 0, 0, 0, "1", "", "at level start the grenade and the knife are selected for a moment, so the game's own grenade stands at the pouch and its knife in the holster from the start (otherwise only after you first took each)" },
			{ "sling_show_primary", kind::boolean, 0, 0, 0, "1", "", "while the pistol or a gadget is out, your rifle hangs on its sling in 3D (a copy of the game's own rifle, taken once while it is in hand)" },
			{ "sling", kind::boolean, 0, 0, 0, "1", "", "with free_hands: a gun let go by both hands hangs on its sling at your belly (follows you, grip it there) instead of dropping to the floor" },
			{ "free_hands_release", kind::real, 0.0f, 0.5f, 0.05f, "0", "", "free hands: the gun hand's grip open this long (seconds) before the gun drops or goes to the off hand; 0 = at once (raise it if a loose grip drops the gun by accident)" },
			{ "free_hands", kind::boolean, 0, 0, 0, "1", "", "hold the gun with the gun-hand grip: let go and it drops to the floor below your hand (no firing); reach down and grip to pick it up" },
			{ "action_aim", kind::boolean, 0, 0, 0, "1", "", "the squad order and a melee strike go where you look, a C4 throw along your off hand: the game's aim turns there for the moment of the action (off: along your body, as the game's aim is otherwise)" },
			{ "c4_trigger", kind::boolean, 0, 0, 0, "1", "", "with the C4 out, the off-hand trigger throws a C4 (the game's aim button); the gun-hand trigger on the detonator sets them off" },
			{ "squad_signal", kind::boolean, 0, 0, 0, "1", "", "squad order (Q): raise the off hand to eye level or above, a little ahead of your head (a fist over the head), and squeeze the grip" },
			{ "grenade", kind::choice, 0, 0, 0, "physical", "button|physical", "grenades: the game's button (LB), or by hand (belt, pin, throw)" },
			{ "grenade_throw_scale", kind::real, 0.5f, 4.0f, 0.1f, "1.5", "", "physical grenades: launch speed = hand speed times this" },
			{ "grenade_throw_aim", kind::boolean, 0, 0, 0, "1", "", "a thrown grenade goes along your hand's motion: the game's aim turns with the throw for that moment and it starts at your hand (#85; off: the old way, which a stick or snap turn and looking up or down sent elsewhere)" },
			{ "reload_sounds", kind::boolean, 0, 0, 0, "1", "", "sounds for magazines, chambering, grenade pouch and pin (a WAV in bfvr_sounds replaces one)" },
			{ "magazine_reach", kind::real, 0.08f, 0.25f, 0.01f, "0.13", "", "physical reload: the off hand grabs or stows a magazine this close to the zone from the low chest to the off-side hip, metres" },
			{ "magazine_pull_out", kind::boolean, 0, 0, 0, "1", "", "the P90 and AK 5C have no magazine release to press: pull the magazine out by hand (B does not drop it); 0: B drops it as on the other guns" },
			{ "drop_discards_ammo", kind::boolean, 0, 0, 0, "1", "", "physical reload: a dropped magazine loses its rounds (off: they go back to the reserve)" },
			{ "pose_edit", kind::choice, 0, 0, 0, "off", "off|relaxed|reach|fist|cup|palm|magazine_ak|magazine_ar|hook|pinch|underhand|rack_over|rack_under", "pose editor (development): the grip whose finger tweaks the sliders below set, live wherever a hand shows it; choosing one loads its saved tweaks (bfvr_poses.ini)" },
			{ "pose_show", kind::choice, 0, 0, 0, "off", "off|left|right", "pose editor: hold that hand in the edited grip whatever it is doing, to shape it with the other hand on the panel (cup and palm show their open shape)" },
			{ "pose_file", kind::choice, 0, 0, 0, "keep", "keep|save|load", "pose editor: save writes every grip's tweaks to bfvr_poses.ini beside bfvr.ini, load reads it back (undoing unsaved changes); goes back to keep" },
			{ "pose_thumb_bend", kind::real, -60.0f, 60.0f, 2.0f, "0", "", "pose editor: degrees added to the thumb's bend at each joint, + closes" },
			{ "pose_thumb_side", kind::real, -30.0f, 30.0f, 2.0f, "0", "", "pose editor: the thumb turned sideways at its first joint, degrees, + towards the thumb" },
			{ "pose_index_bend", kind::real, -60.0f, 60.0f, 2.0f, "0", "", "pose editor: degrees added to the index's bend at each joint, + closes" },
			{ "pose_index_side", kind::real, -30.0f, 30.0f, 2.0f, "0", "", "pose editor: the index turned sideways at its first joint, degrees, + towards the thumb" },
			{ "pose_middle_bend", kind::real, -60.0f, 60.0f, 2.0f, "0", "", "pose editor: degrees added to the middle's bend at each joint, + closes" },
			{ "pose_middle_side", kind::real, -30.0f, 30.0f, 2.0f, "0", "", "pose editor: the middle turned sideways at its first joint, degrees, + towards the thumb" },
			{ "pose_ring_bend", kind::real, -60.0f, 60.0f, 2.0f, "0", "", "pose editor: degrees added to the ring's bend at each joint, + closes" },
			{ "pose_ring_side", kind::real, -30.0f, 30.0f, 2.0f, "0", "", "pose editor: the ring turned sideways at its first joint, degrees, + towards the thumb" },
			{ "pose_pinky_bend", kind::real, -60.0f, 60.0f, 2.0f, "0", "", "pose editor: degrees added to the pinky's bend at each joint, + closes" },
			{ "pose_pinky_side", kind::real, -30.0f, 30.0f, 2.0f, "0", "", "pose editor: the pinky turned sideways at its first joint, degrees, + towards the thumb" },
			{ "pose_thumb_across", kind::real, -60.0f, 60.0f, 2.0f, "0", "", "pose editor: the thumb's base (Thumb1) turned about its z, degrees: + folds it over the palm" },
			{ "pose_thumb_out", kind::real, -60.0f, 60.0f, 2.0f, "0", "", "pose editor: the thumb's base (Thumb1) turned about its y, degrees: + opens it away from the index" },
			{ "wall_stop", kind::boolean, 0, 0, 0, "1", "", "the drawn gun stops at walls: pulled back along its barrel so the muzzle stays on the near side (the game's own ray cast, #63); never stuck: past 30 cm it lets the barrel through, and it never holds your hand or the soldier back" },
			{ "grip_lock", kind::boolean, 0, 0, 0, "1", "", "physical reload: a magazine brought near the magwell locks onto its rail (upright, in and out along it) and the drawn off hand locks onto the magazine with the game's grip until it seats or leaves; seated, the hand stays on it while you grip; racking, the hand keeps the turn it took the handle with (off: the magazine zips in from 10 cm, the hand follows the controller)" },
			{ "magazine_flight", kind::boolean, 0, 0, 0, "1", "", "a magazine you let go or toss flies on with your hand's speed, tumbles, bounces and tips onto a side on the level's real ground (the game's own ray cast, #61), lies 30 s and goes only while you are not looking (off: it just drops away); the AK quick reload knocks the old one out (#47)" },
			{ "accessory_toggle", kind::choice, 0, 0, 0, "trigger", "trigger|touch|off", "accessory toggle (laser, light, magnifier: D-pad up): trigger = holding the gun two-handed, press the off-hand trigger; touch = off hand on the optic or front rail + grip" },
			{ "vr_panel", kind::boolean, 0, 0, 0, "1", "", "hold the left menu button to open this panel (a short press stays the game's Start)" },
			{ "stick_menu", kind::boolean, 0, 0, 0, "1", "", "hold both thumbstick clicks ~0.5 s to open or close this panel (SteamVR can take the left menu button)" },
			{ "mark_tap", kind::boolean, 0, 0, 0, "1", "", "tap both thumbstick clicks briefly to mark a moment for feedback (state and screenshot saved to bfvr_marks)" },
			{ "panel_opacity", kind::real, 0.2f, 1.0f, 0.05f, "0.6", "", "how solid this panel's backdrop is (1 = opaque); the pointer beam and dot show where it points" },
			{ "menu_hint", kind::boolean, 0, 0, 0, "1", "", "after a level loads, show how to open this panel for 8 s" },
			{ "haptics", kind::boolean, 0, 0, 0, "1", "", "controller vibration: the game's rumble (firing, explosions, hits) and taps for grabs and gestures" },
			{ "haptics_strength", kind::real, 0.0f, 1.0f, 0.05f, "0.8", "", "controller vibration strength" },
			{ "haptics_game", kind::boolean, 0, 0, 0, "1", "", "haptics: the game's rumble (firing on the gun hand, both hands for explosions and hits)" },
			{ "haptics_gestures", kind::boolean, 0, 0, 0, "1", "", "haptics: taps for holsters, magazines, grenades, the two-handed grab, use, menu clicks" },
			{ "physical_pad", kind::boolean, 0, 0, 0, "1", "", "a real gamepad on the first slot also reaches the game (off: only the VR controllers; a stuck trigger there fires the gun)" },
			{ "grab_debug", kind::boolean, 0, 0, 0, "0", "", "draw every grab zone (magazine, handle, pouch, handguard, holsters) as a ring as large as its reach; green while a hand is inside" },
			{ "zone_edit", kind::choice, 0, 0, 0, "off", "off|handle|magazine|lid", "grab-zone tuning: the zone of the gun in hand that zone_right/up/forward/size set, live (the grab rings show meanwhile); choosing one, or another gun, puts its saved values on the sliders (bfvr_zones.ini)" },
			{ "zone_right", kind::real, -15.0f, 15.0f, 0.5f, "0", "", "grab-zone tuning: the zone moved this many centimetres to the gun's right, seen from behind it (- left)" },
			{ "zone_up", kind::real, -15.0f, 15.0f, 0.5f, "0", "", "grab-zone tuning: the zone moved this many centimetres up (- down)" },
			{ "zone_forward", kind::real, -15.0f, 15.0f, 0.5f, "0", "", "grab-zone tuning: the zone moved this many centimetres forward along the barrel (- back)" },
			{ "zone_size", kind::real, 0.3f, 3.0f, 0.1f, "1", "", "grab-zone tuning: the zone's reach times this (1 = as built)" },
			{ "zone_file", kind::choice, 0, 0, 0, "keep", "keep|save|load|reset", "grab-zone tuning: save writes every gun's tuned zones to bfvr_zones.ini beside bfvr.ini, load reads it back (undoing unsaved changes), reset puts the edited zone of the gun in hand back as built; goes back to keep" },
			{ "zone_tunes", kind::boolean, 0, 0, 0, "1", "", "the grab zones tuned per gun (bfvr_zones.ini) move and size the zones (off: every gun as built, to compare)" },
			{ "menu_input", kind::choice, 0, 0, 0, "pointer", "pointer|stick|pad", "menus: point with a controller and pull the trigger, move a cursor with either thumbstick and click with a trigger (works whatever the controllers point at), or the pad only" },
			{ "death_release", kind::boolean, 0, 0, 0, "1", "", "when you die, a trigger, grip, button or stick still held waits for you to let go of it once after the respawn (#73: dying sometimes left the gun firing)" },
			{ "focus_release", kind::boolean, 0, 0, 0, "1", "", "after a system menu (SteamVR dashboard, Quest menu) had the controllers, a trigger, grip, button or stick still pressed stays away from the game until you let go of it once (#72)" },
			{ "focus_freeze", kind::boolean, 0, 0, 0, "1", "", "also treat a controller stream that stopped (both poses identical for 0.5 s; suspected, not yet seen, for Steam Link while the Quest menu is open) as a system menu; off if triggers or sticks ever cut out for a moment in normal play (#72)" },
			{ "menu_grip_back", kind::boolean, 0, 0, 0, "1", "", "while a menu is up over the level (a crate, the pause menu), pressing the off-hand grip again backs out of it (Escape): close a crate the way you opened it (#34)" },
			{ "aim", kind::choice, 0, 0, 0, "controller", "head|controller", "research: what aims the gun: the gun-hand controller, or the view (the game's own; tools/sight_probe.py needs it with gun_pose = animated)" },
			{ "gun_pose", kind::choice, 0, 0, 0, "aim", "aim|animated", "research: where the drawn gun points: along the controller ray, or the game's hip pose turned by the controller" },
			{ "reticle", kind::boolean, 0, 0, 0, "1", "", "a dot on what the barrel points at, where the shots go" },
			{ "reticle_size", kind::real, 0.3f, 3.0f, 0.1f, "1", "", "aim reticle size, degrees" },
			{ "sight_ads", kind::boolean, 0, 0, 0, "1", "", "hold the gun's sight to your eye and the game aims down it (the optic's lens clears, ADS accuracy); not for a clear holo while hip_accuracy is 1" },
			{ "sight_dot", kind::boolean, 0, 0, 0, "1", "", "guns with an optic: a red dot at infinity along the sight line, seen only by an eye that looks through the lens, aiming or not (the game's own reticle follows the view's centre, not the sight)" },
			{ "pin_volume", kind::real, 0.0f, 1.0f, 0.05f, "0.35", "", "loudness of the grenade pin pull, 0 = silent, 1 = full (#82; 0.35 is about a third of the old ping)" },
			{ "crate_primary", kind::choice, 0, 0, 0, "keep", weapon::crate::values, "own crate (#80): pick a gun and it goes into the primary slot and into your hand about a second after you stop stepping, no game menu needed (keep: no change)" },
			{ "crate_secondary", kind::choice, 0, 0, 0, "keep", weapon::crate::values, "own crate (#80): the same for the secondary slot" },
			{ "unlock_all_weapons", kind::boolean, 0, 0, 0, "1", "", "every gun in the campaign weapon crate can be picked (the game's own all-unlocked switch, held while a campaign level runs; memory only, nothing is saved or sent)" },
			{ "hip_accuracy", kind::real, 0.0f, 1.0f, 0.1f, "1", "", "hip-fire spread of your weapons: 1 = as tight as aiming down sights (sights work without the game's aim), 0 = the game's 3-7 degree hip cone" },
			{ "sight_dot_size", kind::real, 0.1f, 2.0f, 0.05f, "0.6", "", "sight dot size, degrees (the glow; the bright core is about half)" },
			{ "sight_dot_reach", kind::real, 1.0f, 6.0f, 0.5f, "2", "", "how far off the sight line an eye still sees the red dot, cm (the lens is about 1.5; more forgives an unsteady hold)" },
			{ "sight_zoom_mesh", kind::boolean, 0, 0, 0, "1", "", "a sight held to your eye shows the game's own aiming view of the gun (the optic's clear lens and its reticle) without the game aiming: no slower walk, no zoom" },
			{ "sight_reticle", kind::boolean, 0, 0, 0, "1", "", "while a sight is at your eye, the game's own reticle stays on the sight line when your head turns, the same in both eyes (it is drawn at the centre of the view otherwise and leaves the lens)" },
			{ "sight_dot_eyes", kind::choice, 0, 0, 0, "sighting", "sighting|both", "who sees the red dot: only the eye behind the sight (like a real one), or both eyes (one layer for both; a check for headsets that do not show single-eye layers)" },
			{ "sight_dot_test", kind::boolean, 0, 0, 0, "0", "", "test: the red dot shown to both eyes wherever the gun is held (checks that the headset shows the dot layer at all)" },
			{ "scope", kind::boolean, 0, 0, 0, "1", "", "magnifying sights (the AK-12's flip magnifier): while aiming, the lens shows the world magnified (one more render)" },
			{ "sight_lens_cone", kind::real, 0.0f, 45.0f, 1.0f, "8", "", "real sight lens: the game's aiming model of the optic (clear lens, reticle; open at the top) shows only while an eye looks into the lens from behind within this many degrees of the sight line; elsewhere the full optic shows (0 = always the aiming model)" },
			{ "sight_lens", kind::boolean, 0, 0, 0, "1", "", "red dots and holos drawn as a real sight (the BF3 single-player mod's lens): the game's reticle placed by the angle through the glass, so it keeps its size at any distance, turns with the gun and stays on the target; the optic's clear glass at any distance" },
			{ "sight_lens_fov", kind::real, 0.0f, 70.0f, 1.0f, "0", "", "sight_lens: the aiming view the reticle art is sized for, degrees; 0 (below 10) = the reticle's own zoom level from the game's data (red dots and holos 40: the EOTech's ring 1.1 degrees, as the real one; an ACOG 17.2; a flipped magnifier keeps its red dot's, so its ring grows with the power in the glass); 10 or more overrides it, larger = a larger reticle" },
			{ "scope_glass", kind::boolean, 0, 0, 0, "1", "", "scopes and flipped magnifiers show the world magnified on the optic's own glass (Great Destroyer's lens: the same way in, its angle divided by the optic's power, dark past its field stop and as the eye leaves the line, shut 2 cm off it), from the scope picture (needs scope on); scopes and magnifiers then go without the game's aim; 0 = the flat scope picture" },
			{ "sight_lens_brightness", kind::real, 0.5f, 20.0f, 0.5f, "4", "", "sight_lens: the reticle's brightness" },
			{ "scope_in_frame", kind::boolean, 0, 0, 0, "1", "", "the magnified picture is drawn into the game's own image inside the optic's glass (it then sits on the optic in both eyes); 0 = a separate headset layer as before" },
			{ "scope_zoom", kind::real, 0.5f, 4.0f, 0.1f, "1", "", "scope magnification times the game's own (1 = the game's: the magnifier 1.7x)" },
			{ "aim_convergence", kind::real, 2.0f, 100.0f, 1.0f, "25", "", "shots meet the barrel line this far away when the barrel points at nothing (the sky), metres" },
			{ "move_direction", kind::choice, 0, 0, 0, "head", "head|gun|body", "left stick forward walks where your head faces, where the gun points, or along the body (the game's own way)" },
			{ "drive_controls", kind::choice, 0, 0, 0, "wheel", "stick|wheel", "driving a car: the left stick drives (forward / back, steer), or the grips hold a steering wheel with the right trigger as throttle and the left as brake / reverse" },
			{ "wheel_lock_degrees", kind::real, 45.0f, 360.0f, 15.0f, "90", "", "with drive_controls = wheel: how far the wheel turns for full steering, degrees" },
			{ "sprint_steer", kind::boolean, 0, 0, 0, "1", "", "with move_direction = head: a sprint (left stick click, stick forward) or a forward jump turns the soldier, not the view, towards where you go, so the game sprints and vaults (#52)" },
			{ "neutral_pitch", kind::boolean, 0, 0, 0, "0", "", "hold the game's aim pitch at 0: only the head pitches the view (level horizon)" },
			{ "stance_flicks", kind::boolean, 0, 0, 0, "1", "", "right stick up/down changes stance (right_stick = turn)" },
			{ "physical_stance", kind::boolean, 0, 0, 0, "0", "", "crouch or lie down in the room and the soldier does too; off when playing seated" },
			{ "controls_preset", kind::choice, 0, 0, 0, "default", "default|jump_on_b|custom", "controller mapping: a preset sets the buttons below; changing one of them makes it custom" },
			{ "bind_a", kind::choice, 0, 0, 0, "a", std::string(input::pad_button_choices()), "what the right A button presses (pad button; a = jump)" },
			{ "bind_b", kind::choice, 0, 0, 0, "b", std::string(input::pad_button_choices()), "what the right B button presses (b = reload, magazine release)" },
			{ "bind_x", kind::choice, 0, 0, 0, "x", std::string(input::pad_button_choices()), "what the left X button presses (x = the game's menu)" },
			{ "bind_y", kind::choice, 0, 0, 0, "dpad_down", std::string(input::pad_button_choices()), "what the left Y button presses (dpad_down = fire mode; weapons change at the holsters)" },
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
