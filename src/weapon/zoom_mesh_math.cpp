#include "weapon/zoom_mesh.hpp"

namespace bfvr::weapon
{
	float zoom_mesh_factor(float original, bool wanted)
	{
		return wanted ? 0.0f : original;
	}
}
