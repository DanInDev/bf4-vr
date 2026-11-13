#pragma once

#include <string>

#include "physics/ground_cache.hpp"

// The game's own physics ray cast (#59, re/notes/physics.md): the level's collision between two world
// points, cast on the game thread (the camera hook's per-frame tick), a few rays a frame. Feeds the ground
// cache that things let go of land on, and the pipe's raycast research command. BF4 only; off when the
// function's bytes or the world are not as expected.
namespace bfvr::physics
{
	// The level's ground (world metres), shared by everything that lands on it.
	ground_cache& level_ground();

	// Game thread, once per frame: casts the queued rays.
	void ray_cast_on_game_thread();

	// Pipe: "raycast <x y z> <x y z>" (world), "raycast down [metres]" and "raycast ahead [metres]" from the
	// head, "raycast stats".
	std::string raycast_command(const std::string& argument);
}
