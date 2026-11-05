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
	void set_entry_points(draw_indexed_fn draw_indexed, set_constant_buffers1_fn vs_set_constant_buffers1, read_block_fn read_block);

	// Render thread, around each per-view render: 0 = first eye, 1 = second eye, 2 = scope.
	void begin_render(int eye);
	void end_render();

	// Render thread: the game's own VS constant bindings (before any eye-2 redirection) and each indexed draw.
	void on_vs_bindings(UINT start, UINT count, ID3D11Buffer* const* buffers, const UINT* first, const UINT* number);
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
	std::string report();

	// Research (pipe meshcopy <index buffer hex> <dx dy dz> [entries]): after every draw from that index
	// buffer with a bone palette, the same draw again with palette entries 0..entries-1 moved by d
	// (palette space, metres); "meshcopy off".
	std::string set_research_copy(const std::string& spec);
}
