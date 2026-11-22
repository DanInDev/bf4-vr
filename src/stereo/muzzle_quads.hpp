#pragma once

#include <d3d11.h>

#include <string>

// #65: the muzzle flash at the drawn gun. BF4 draws the flash (and the smoke and sparks with it) as particle quads in
// the world at its own, flat gun's muzzle (re/notes/weapon.md "The muzzle flash"). For a moment after each shot, the
// particle batches are copied on the graphics card and a compute shader moves the quads near the game's barrel line
// onto the drawn gun (stereo/muzzle_move), then the batch is drawn from the copy. After The Great Destroyer's
// src/effects.cpp (DrawQuadsMoved, its quad shader, QuadBatch; reuse allowed by its maintainer).
namespace bfvr::stereo::muzzle_quads
{
	using draw_indexed_fn = void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, UINT, UINT, INT);

	// From eye_pass's DrawIndexed hook, before the game's draw: true when the batch was drawn here (moved), so the
	// hook must not draw it again. Cheap when nothing was fired lately.
	bool draw(ID3D11DeviceContext* context, UINT count, UINT start, INT base, draw_indexed_fn original);

	// From eye_pass's DrawIndexedInstanced hook (research): during "flash survey", instanced draws are counted by kind
	// (index count, the instance stream's stride) with how near their first instances' positions come to the muzzle.
	void note_instanced(ID3D11DeviceContext* context, UINT count, UINT instances, UINT first_instance);
	// The same for Draw / DrawInstanced (positions read from vertex stream 0) and the indirect draws (counted only).
	void note_plain(ID3D11DeviceContext* context, UINT vertices, UINT instances, UINT start_vertex, UINT first_instance);
	void note_indirect(ID3D11DeviceContext* context, bool indexed);

	// From eye_pass around each eye's run of the game's view render (#144): within one pass a particle buffer is copied
	// and moved once, and its later batches draw from that copy; outside a pass every batch copies as before.
	void begin_pass();
	void end_pass();

	// Live setting flash_at_gun.
	void set_on(bool on);

	// Pipe "flash": the state; "flash survey <ms>": for that long, every indexed draw of quads from a dynamic vertex
	// buffer is counted by its kind (stride, buffer size, blending) with how near its first quads come to the game's
	// muzzle (a read-back stall: research only); "flash stride <bytes>": the particle vertex stride moved.
	std::string pipe_command(const std::string& argument);
}
