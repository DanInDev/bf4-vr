#pragma once

#include <cstdint>
#include <vector>

#include "ui/reticle.hpp"

// The grenade in the off hand while it is held (physical grenades, docs/tasks/grenades.md): the game
// draws nothing until the throw, so the compositor shows an M67 as a quad at the palm. A shaded sphere
// looks the same from every side, so the quad faces the eye; its roll keeps the fuse pointing out of
// the fist. Engine-free; tested by tests/held_grenade_test.cpp.
namespace bfvr::ui
{
	// The quad's side in metres (the M67 body is 6.4 cm across, the fuse adds ~3 cm).
	constexpr float held_grenade_quad_size = 0.11f;
	// The body's centre sits this far below the quad's centre (metres, along the quad's up axis).
	constexpr float held_grenade_body_drop = 0.0085f;

	// Premultiplied BGRA, row 0 at the top (the fuse end). pin: the safety pin's ring still on the
	// fuse; without it the lever stays (armed, still in the hand).
	void held_grenade_texture(int size, bool pin, std::vector<std::uint32_t>& pixels);

	// grenade: the grenade body's centre; fuse: the direction the fuse points (any length); eye: the
	// viewer. The quad faces the eye with its up axis along the fuse as seen from there.
	reticle_quad place_held_grenade(const float (&grenade)[3], const float (&fuse)[3], const float (&eye)[3]);
}
