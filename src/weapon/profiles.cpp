#include "weapon/profiles.hpp"

#include <algorithm>

namespace bfvr::weapon
{
	namespace
	{
		// No release button on these: the magazine is rocked out by hand (unlock names, BF4 179547).
		constexpr std::string_view ak_family[] = {
			"U_AK12", "U_AKU12", "U_AK74M", "U_AEK971", "U_AN94", "U_RPK12", "U_RPK-74M", "U_RPK74M", "U_GROZA-1", "U_SVD12",
			"U_Saiga12",
		};
	}

	bool handle_point_for(std::string_view unlock_name, float (&point)[3])
	{
		struct measured
		{
			std::string_view name;
			float point[3];
		};
		// None verified yet (docs/tasks/hand-animation.md A1/A1b): add a gun once its knob is matched with
		// `reload handle x y z` in a side view. The empty entry keeps the array non-empty.
		static constexpr measured table[] = {
			{ "", { 0, 0, 0 } },
		};
		for (const measured& gun : table)
		{
			if (!gun.name.empty() && gun.name == unlock_name)
			{
				std::copy(std::begin(gun.point), std::end(gun.point), point);
				return true;
			}
		}
		return false;
	}

	reload_profile profile_for(std::string_view unlock_name, int game_capacity)
	{
		reload_profile profile;
		const bool ak = std::find(std::begin(ak_family), std::end(ak_family), unlock_name) != std::end(ak_family);
		profile.kind = ak ? variant::ak : variant::ar;
		profile.capacity = std::max(game_capacity - 1, 1);
		profile.flick = ak;
		return profile;
	}
}
