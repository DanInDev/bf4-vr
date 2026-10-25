#pragma once

#include <string>

namespace bfvr::stereo
{
	// Research toward true per-eye rendering (re/notes/stereo-rendering.md). Result 2026-10-25: rendering the module twice crashes the render thread (a sub-renderer
	// callback nulls a per-frame pointer the second pass dereferences); kept for reference, see eye_pass.hpp. Hooks the world render
	// module's render method (0x1408BEC40, vtable 0x141D24258 slot 3), which draws the whole 3D world
	// (G-buffer, shadows, lighting) once per frame on the render thread.
	//   passes 1 = normal, 2 = the world is rendered twice per frame;
	//   shift_m != 0 moves the camera sideways for the second pass only;
	//   target 0 = the module's own view copy (+0x550), 1 = GameRenderer view params ([0x142672378]+0x60).
	// The final image shows the second pass, so a visible shift proves that a view change between
	// passes reaches the GPU.
	void install_world_pass_hook();
	std::string set_world_pass(int passes, float shift_m, int target);

	// Research: the game-thread view setup 0x140848B00 (GameRenderer*, count, views) builds N views,
	// each with its own viewport (+0xEC0) and targets. count 2 duplicates the game's single view,
	// moves the copy sideways and gives it viewport <viewport>.
	std::string set_view_count(int count, float shift_m, int viewport);
	// research_views = 2 in bfvr.ini: two views from the first frame (research_view_shift,
	// research_view_viewport configure the second one).
	void install_view_experiment();
}
