#pragma once

#include <d3d11_1.h>

#include <string>

// Extra meshes (#36): game-quality copies of meshes the rig cannot draw twice (a second magazine, a
// held grenade), made by re-issuing the game's own draw with a changed bone palette. Fed by the eye
// pass's draw and constant-binding hooks (render thread). docs/tasks/extra-meshes.md, re/notes/meshes.md.
namespace bfvr::stereo::extra_mesh
{
	using draw_indexed_fn = void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, UINT, UINT, INT);
	using set_constant_buffers1_fn = void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, UINT, UINT, ID3D11Buffer* const*, const UINT*, const UINT*);
	// Reads count constants (16 bytes each) of an arena block through its CPU mapping; false when the
	// buffer is no known arena or the read faults.
	using read_block_fn = bool (*)(ID3D11Buffer* buffer, UINT first, UINT count, float* out);

	// The eye pass's unhooked entry points (calls through the context would re-enter the hooks).
	void set_entry_points(draw_indexed_fn draw_indexed, set_constant_buffers1_fn vs_set_constant_buffers1, set_constant_buffers1_fn ps_set_constant_buffers1,
		read_block_fn read_block);

	// Render thread, around each per-view render: 0 = first eye, 1 = second eye, 2 = scope.
	void begin_render(int eye);
	void end_render();

	// Render thread: the game's own VS constant bindings (before any eye-2 redirection) and each indexed draw.
	void on_vs_bindings(UINT start, UINT count, ID3D11Buffer* const* buffers, const UINT* first, const UINT* number);
	void on_ps_bindings(UINT start, UINT count, ID3D11Buffer* const* buffers, const UINT* first, const UINT* number);
	void after_draw(ID3D11DeviceContext* context, UINT index_count, UINT start_index, INT base_vertex, UINT instances);

	// Research (pipe meshprobe <path>): every indexed draw of the next first-eye render with its input
	// buffers, shaders, render target and VS constant bindings; skinned draws (VS slot 0 = 3072 bytes,
	// 64 bone matrices of 3x4) with their palette.
	std::string probe(const std::string& path);

	// The magazine bone's scale while extra meshes are on: 0.2 % larger, invisible, and no other bone has it,
	// so the palette entries of its draws are the ones whose rows are this long.
	constexpr float magazine_tag = 1.002f;

	// Pose thread (body/arm_pose): a second magazine is wanted (active) at matrix (3x4 rows, palette space)
	// times the drawn one. The render thread re-issues every draw whose palette holds the tagged magazine
	// bones (rows of length magazine_tag) with those entries moved and every other entry collapsed.
	void set_magazine_copy(bool active, const float* matrix);

	// The held grenade (#36). The game draws its grenade only while the grenade is its weapon (a throw, or
	// drawn with its key). Meanwhile the pose thread puts Wep_Root at grenade_capture_position (palette space,
	// no rotation) with scale grenade_tag, which hides the game's grenade and marks its draws: the render
	// thread captures each of them whole (buffers, layout, shaders, rasterizer, the grenade's own material
	// blocks and textures; the bone palette relative to Wep_Root). While a grenade is held, every pass that
	// draws the gun's magazine (tagged, magazine_tag) draws the captured grenade after it, with its per-view
	// and per-object state from the magazine's draw and Wep_Root at the hand.
	constexpr float grenade_tag = 0.01f;
	// Meanwhile Wep_Mag gets this scale instead of magazine_tag (0.015 under the hidden root): a draw carrying
	// it is the gun, still drawn while the game switches, and that capture is thrown away.
	constexpr float capture_magazine_scale = 1.5f;
	constexpr float grenade_capture_position[3] = { 0.0f, 1.0f, 0.0f };
	// The items captured this way: the grenade (#36), and the knife (#41) for its holster. Each keeps its
	// own capture and copy; one capture runs at a time (the item whose Wep_Root is under the hidden root).
	enum class captured : int
	{
		grenade = 0,
		knife = 1,
	};
	constexpr int item_count = 2;
	// Pose thread: this item is the game's weapon, tagged under the hidden root (a capture is wanted).
	void set_item_capture(captured item, bool wanted);
	// Pose thread: draw the item's copy with its Wep_Root at matrix (3x4 rows, palette space, scale 1).
	void set_item_copy(captured item, bool active, const float* matrix);
	bool item_drawn(captured item); // a copy drawn within the last 150 ms
	bool item_captured(captured item);
	std::uint64_t item_captures(captured item);
	// Pose thread: the game's grenade is out and tagged (a capture is wanted).
	void set_grenade_capture(bool wanted);
	// Pose thread: the held grenade's Wep_Root (3x4 rows, palette space, scale 1); armed: the pin is out
	// (the palette entries of the pin and its ring are left out).
	void set_grenade_copy(bool active, const float* matrix, bool armed);
	// A grenade copy was drawn within the last 150 ms (the theater leaves out its flat stand-in).
	bool grenade_drawn();
	bool grenade_captured();
	std::uint64_t grenade_captures(); // accepted captures so far
	// Pipe "extramesh pin <hex mask>": which palette entries of the captured grenade are its pin (research).
	std::string set_grenade_pin_entries(const std::string& mask);

	std::string report();

	// Research (pipe meshcopy <index buffer hex> <dx dy dz> [entries]): after every draw from that index
	// buffer with a bone palette, the same draw again with palette entries 0..entries-1 moved by d
	// (palette space, metres); "meshcopy off".
	std::string set_research_copy(const std::string& spec);
}
