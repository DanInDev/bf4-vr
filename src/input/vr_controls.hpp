#pragma once

#include <cstdint>
#include <string>

#include "input/gamepad.hpp"

namespace bfvr::input
{
	// Game side of the right-stick mapping (issue #22, right_stick.hpp). With right_stick = turn in
	// bfvr.ini the right stick no longer aims: horizontal turns the view (aimer yaw, re/notes/aim.md),
	// vertical flicks change stance through the pad's crouch/jump buttons, chosen from the soldier's
	// real stance (ClientSoldierEntity +0x4F0: 0 stand, 1 crouch, 2 prone).
	void install_vr_controls();
	// turn: snap | smooth | off.
	void configure_turn(const std::string& turn, int snap_degrees, bool stance_flicks);

	// XInput thread, every poll: the pad the game sees (stick replaced, stance presses added).
	gamepad_state for_game(const gamepad_state& pad);

	// Controller aim (#12): aim = controller. The camera hook renders from a level body yaw (camera
	// yaw convention, atan2(look.x, look.z)); snap turns change it; the aimer follows the controller.
	// The right stick then always turns (and flicks stance), whatever right_stick says.
	void set_controller_aim(bool enabled);
	bool controller_aim_active();
	// Game thread: the body yaw, taken from the game's camera yaw the first time.
	float body_camera_yaw(float game_yaw);
	// Game thread: points the aimer along a world direction (bullets go there).
	void aim_along(const float (&direction)[3]);
	// The aimer's yaw and pitch (radians; camera yaw = -aimer yaw, pitch up positive); false when no
	// aimer can be read.
	bool aim_angles(float& yaw, float& pitch);
	// The local soldier's health (0 = dead); false when it cannot be read.
	bool soldier_health(float& health);
	// Physical stance (#33): press the game's stance buttons, one step at a time, until the soldier is
	// in this stance (0 stand, 1 crouch, 2 prone); gives up after 3 s. A stick flick afterwards is left alone.
	void request_stance(int stance);
	// The soldier's stance (0, 1, 2), -1 when unreadable.
	int game_stance();
	// A stance press (crouch B / jump A) is being held for the game right now: the pad filters after
	// for_game must let its B through.
	bool stance_press_active();
	// Controller aim's shot direction in OpenXR LOCAL (weapon/two_handed: the barrel line, converged);
	// without one the camera hook uses the gun hand's aim ray.
	void set_aim_direction(bool valid, const float (&direction)[3]);
	bool aim_direction(float (&direction)[3]);
	// The gun's sight line in OpenXR LOCAL (weapon/two_handed): where shots start with controller aim
	// (weapon/shot_origin). shot_line is false when none was set in the last 250 ms.
	void set_shot_line(bool valid, const float (&start)[3], const float (&direction)[3]);
	bool shot_line(float (&start)[3], float (&direction)[3]);
	// The rendered body's camera yaw under controller aim (atan2(look.x, look.z)); false when unknown.
	bool body_yaw_now(float& yaw);
	// The local soldier's current ClientWeapon (ClientSoldierWeapon +0x49A8), 0 when unreadable; BF4 only.
	std::uintptr_t current_client_weapon();

	// The game walks relative to its own camera (the body, before the head pose: measured on the
	// monitor in both aim modes), so a player who turns in the room would walk the old way. The left
	// stick is turned by the head's (or the gun hand's) yaw in LOCAL space: move_direction =
	// head | gun | body (body: the game's own behaviour).
	enum class move_reference
	{
		body,
		head,
		gun,
	};
	void set_move_reference(move_reference reference);

	// Holds the aimer pitch at 0 every frame (the head alone pitches the view, #5).
	void set_neutral_pitch(bool enabled);

	// Game thread (view-params copy): adds the turn collected since the last call to the aimer yaw.
	void apply_view_turn();

	// Pipe: "rightstick look|turn" switches live; no argument reports.
	std::string set_right_stick_mode(const std::string& mode);
	// grips_to_game: the controller grips reach the game as LB/RB; ads_trigger: the left trigger as
	// LT (aim down sights). Both off by default (the grips are the mod's). x_opens_menu: left X is
	// Start (the game's menu).
	void set_game_buttons(bool grips, bool ads, bool x_opens_menu);
	// An eye is behind the gun's sight (weapon/two_handed, sight_ads): the game gets LT held, its aim
	// down sights (the optic's lens clears, the reticle and ADS accuracy).
	void set_sight_aim(bool aiming);
	// The sight aim's LT, applied after the filters that keep the off-hand trigger from the game (the
	// two-handed hold zeroed it: headset 2026-11-11, the game never aimed while both hands held the gun).
	gamepad_state with_sight_aim(const gamepad_state& pad);
	// The pad as the game finally gets it (after every filter): what game_aiming answers from.
	void note_game_pad(const gamepad_state& pad);
	// The game is getting its aim button (LT) now: it aims down the sights (or is getting there).
	bool game_aiming();
	// The held item uses the left trigger for its own action (the C4's throw), not zoom: it reaches
	// the game even with ads_trigger off.
	void set_item_left_trigger(bool passes);
}
