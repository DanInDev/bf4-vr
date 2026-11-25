#pragma once

#include <d3d11.h>
#include <dxgi.h>

#include <cstdint>
#include <string>

#include "stereo/tracking.hpp"

namespace bfvr::stereo
{
	// Pair rendering (stereo_render = pair, the default): both eyes from one game frame
	// (docs/tasks/per-eye-rendering.md, approach 2; started as research, hence the mode flags).
	// Wraps the world renderer's per-view render 0x140CE7EA0 (render thread; G-buffer, shadows,
	// lighting, tonemap into the back buffer) and calls it twice per frame for view slot 0, the
	// second time with the camera moved sideways. The frame-level work around it (sub-renderer
	// begin/end callbacks, sync points) still runs once: repeating it crashes, because a callback
	// consumes a per-frame pointer (re/notes/stereo-rendering.md).
	// After the first render the back buffer is copied into the eye texture.
	//   mode 0 = off; flags: 1 = render twice, 2 = shifted eye last (the monitor shows it),
	//   4 = restore slot 0 from a snapshot between the eyes, 8 = eye-2 constants (implies 2; the
	//   per-view constants are baked into job-filled arenas, see constant_patch.hpp), 32 = also move
	//   every view-projection built with the game's projection (tested, did not move the gun).
	// stereo_render = pair uses 1 | 8; flag 8 also moves the first-person (gun and arms) per-view block.
	std::string set_eye_pass(int mode, float shift_m);
	// Research: during the next eye-pass frame, records the first eye's constant-buffer bindings and
	// writes <prefix>-bindings.txt, each bound buffer read back from the GPU (<prefix>-buffer-<ptr>.bin)
	// and view slot 0 (<prefix>-slot.bin).
	// second_eye: record the second eye's bindings ('R' lines = redirected to our eye buffer) and draws.
	std::string probe_eye_pass(const std::string& prefix, bool second_eye = false);
	// Research: "V 2 512" = every VS slot-2 binding of 512 bytes gets zeros in the second eye; "off".
	std::string set_eye_poison(const std::string& spec);
	// Research: indexed draws [from, to) of the second eye are skipped (0 0 = none).
	std::string set_eye_skip(unsigned from, unsigned to);
	// Pipe "eyefix [0|1]" (#39): second-eye block lookups keyed by the arena's discards in this render (1, the
	// default) or not (0, as before: copies made before a discard answered blocks written after it).
	std::string set_discard_fix(int on);
	std::string set_fresh_fix(int on); // pipe eyefresh 0|1 (#162)
	// Research (pipe eyehide): indexed draws with one of these pixel shaders (hex pointers from meshprobe,
	// up to 4) are skipped in both eyes; "off" clears.
	std::string set_eye_hide(const std::string& shaders);
	// stereo_render = pair: the camera hook sets the right eye's offset (metres along the left eye's
	// right axis) every frame.
	void set_eye_pass_shift(float metres);
	// Research (pipe eyezoom): the second eye keeps the first eye's camera but renders with this
	// vertical FOV (degrees; 0 / out of range = off), to test whether the constant patch follows a zoom.
	std::string set_eye_zoom(float degrees);
	// Pipe scopescissor 0|1 (else report): the scope render's scissor to the lens (stereo/scope_scissor).
	std::string set_scope_scissor(int on);
	// Complete pairs so far (both eyes rendered from one frame); the theater submits a pair only when
	// this advanced since its last one.
	std::uint64_t eye_pass_pairs();
	// One status line: eye_frames, eye_pairs, eye_rejected, eye_abandoned, eye_ms (last extra render).
	std::string eye_pass_report();
	// sight_reticle (#39): where the gun's sight line points in the rendered eye's view space, as x / -z and
	// y / -z; the game's reticle quad is drawn there instead of on the view's axis. valid: a sight is at an eye.
	void set_reticle_view(bool valid, float tan_x, float tan_y);
	// scope_in_frame (#39): the magnifier's picture drawn in the game's frame at its lens, masked by the game's
	// lens stencil, instead of an OpenXR quad. Camera hook: the first eye this frame renders (OpenXR LOCAL).
	void set_lens_eye(const pose& first_eye);
	// sight_lens (#39, BF3-style lens): camera hook, each view: the gun's sight line and up in the rendered
	// eye's view space (x right, y up, -z ahead), valid whenever the gun's points are fresh.
	void set_lens_axes(bool valid, const float (&along)[3], const float (&up)[3]);
	// Settings sight_lens, sight_lens_fov (an override of the view the reticle art is sized for, degrees; below 10 = each
	// zoom level's own) and
	// sight_lens_brightness.
	void set_sight_lens(bool on, float ads_fov_degrees, float brightness);
	// #139: a magnifying optic shows the world magnified on its own glass (setting scope_glass).
	void set_scope_glass(bool on);
	// #139 (owner 2026-11-24): the whole picture within this many metres of the sight line (setting scope_eye_box).
	void set_scope_eye_box(float metres);
	// #139: the optic's own glass showed the picture in the last 300 ms (the scope plan then renders a wide picture).
	bool scope_glass_shown();
	// Research (pipe "glassdebug 0|1|2"): 1 the glass mask's lens quads in colour and the sight line's point magenta; 2 the
	// glass black with its reticle art pure green (the drawn reticle's size).
	std::string set_glass_debug(int mode);
	void set_scope_in_frame(bool on);
	// The picture went into the frame within the last 200 ms (the theater then leaves its quad out).
	bool scope_drawn_in_frame();
	// Pipe "scope": the in-frame lens counters.
	std::string lens_report();
	// Research (pipe lensfix <dx1> <dx2>): metres added to each eye's view x for the in-frame lens.
	std::string set_lens_fix(float dx1, float dx2);
	// Pipe "reticle [<tan x> <tan y>|off]": state and counters; research: hold the reticle at an offset.
	std::string reticle_command(const std::string& argument);

	// Half the vertical FOV the gun pass was last drawn with (pair rendering sees its constant block):
	// 22.5 degrees at the hip, narrower while the game aims down the sights; fallback when not seen
	// for 0.5 s (alternate-eye rendering, menus).
	float foreground_half_fov(float fallback);
	// Present thread: tracks the back buffer the first eye is copied from.
	void eye_pass_on_present(IDXGISwapChain* swap_chain);
	// The first eye's image of the latest frame (back-buffer format and size), or null.
	// eye 1: the second eye's image copied at the same point of the frame (before the post/UI modules).
	ID3D11Texture2D* eye_pass_image(int eye = 0);
	// The world view's projection this frame (RenderView +0x320, 4x4 row-major, as the game built it); false
	// before the first pair frame. Both eyes use it (they differ by the camera's sideways move only).
	bool world_projection(float (&matrix)[16]);
}
