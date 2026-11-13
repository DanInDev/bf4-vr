#pragma once

#include <string>

// sight_zoom_mesh (#39): every first-person weapon and attachment has a second mesh for zoom (the optic's
// clear lens and the game's own reticle), shown past WeaponStateData.ZoomMeshTransitionFactor of the
// game's aim. With that factor at 0 the game shows it at the hip. While a sight is held to an eye the
// factor of the player's weapons is 0; the data's values are kept and written back afterwards. Nothing
// here knows a weapon or an optic by name (re/notes/weapon.md "The zoom mesh at the hip").
namespace bfvr::weapon
{
	// Engine-free: the factor to write. wanted: a sight is at an eye and the setting is on.
	float zoom_mesh_factor(float original, bool wanted);

	void set_zoom_mesh(bool on);
	// sight_lens (BF3-style lens): the zoom mesh is shown whenever a weapon is held, not only at the eye, so
	// its glass and reticle quad are drawn at any distance (the lens shader in stereo/eye_pass draws them).
	void set_zoom_mesh_always(bool always);
	// weapon/two_handed, each update: an eye is behind the gun's sight (latched).
	void set_sight_at_eye(bool at_eye);
	// Present thread, once per frame.
	void update_zoom_mesh();
	// Pipe "zoommesh [0..1|off]": state; research: hold the factor at a value whatever the eye does.
	std::string zoom_mesh_command(const std::string& argument);
}
