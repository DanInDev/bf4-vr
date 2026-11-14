#pragma once

#include <string>

// #63, setting wall_stop: the drawn gun stops at walls (weapon/wall_stop), from the game's own ray cast.
namespace bfvr::weapon
{
	void set_wall_stop(bool enabled);
	// Present thread, once per frame: asks for the barrel ray and pushes the drawn gun back (body/arm_pose).
	void update_wall_stop();
	// Pipe "wallstop": the last answer and push.
	std::string wall_stop_report();
}
