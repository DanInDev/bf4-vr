#include "weapon/profiles.hpp"

#include "weapon/gun_traits.hpp"

#include <algorithm>

namespace bfvr::weapon
{
	bool handle_point_for(std::string_view unlock_name, float (&point)[3])
	{
		struct measured
		{
			std::string_view name;
			float point[3];
		};
		// From the game's own empty reload: where its animated left hand pinches the handle as the slide
		// starts back, moved forward by the slide's travel (re/notes/weapon.md "Charging handles"). Guns
		// whose reload never pulls the handle (SCAR-H: bolt release) cannot be measured this way.
		static constexpr measured table[] = {
			{ "U_AK12", { -0.02f, 0.06f, 0.53f } }, // right side, ahead of the magwell (2026-11-03)
			// Not measured (its reload uses the bolt release): front-left of the receiver as seen in the
			// headset (2026-11-03), ahead of its Wep_Slide (0, 0.058, 0.505), +x = left.
			{ "U_SCAR-H", { 0.035f, 0.04f, 0.57f } },
		};
		for (const measured& gun : table)
		{
			if (gun.name == unlock_name)
			{
				std::copy(std::begin(gun.point), std::end(gun.point), point);
				return true;
			}
		}
		return false;
	}

	bool rack_zones_for(std::string_view unlock_name, const hand_config& defaults, rack_zone_shape& shape)
	{
		shape = { defaults.rack_over_offset, defaults.rack_over_reach, defaults.rack_under_offset, defaults.rack_under_radii };
		if (unlock_name == "U_SCAR-H")
		{
			// Handle on the left, above the front of the handguard: the overhand zone sits higher (6 cm above the
			// knob, was 3) and tighter (7 cm), so a hand under the gun is the handguard's.
			shape.over_offset = { 0.03f, 0.06f, 0.0f };
			shape.over_reach = 0.07f;
			return true;
		}
		if (unlock_name == "U_AK12")
		{
			// Handle on the right: a larger overhand zone (12 cm) and an underhand region reaching well below the
			// gun (centre 14 cm under the knob, 14 cm out, 20 cm down, 9 cm along the barrel).
			shape.over_reach = 0.12f;
			shape.under_offset = { 0.03f, -0.14f, 0.0f };
			shape.under_radii = { 0.14f, 0.20f, 0.09f };
			return true;
		}
		return false;
	}

	bool is_break_open(std::string_view unlock_name)
	{
		return unlock_name.substr(0, 9) == "U_SP_M320";
	}

	reload_profile profile_for(std::string_view unlock_name, int game_capacity)
	{
		// From the gun's traits (weapon/gun_traits): rocked in and out by hand = the AK variant (this also fixed the Saiga,
		// whose crate unlock is U_SAIGA_20K, not the old list's U_Saiga12); the hold-open as the real gun has it.
		const gun_traits traits = traits_for(unlock_name);
		reload_profile profile;
		const bool ak = traits.feed == feed::rock && traits.release == release::none;
		profile.kind = ak ? variant::ak : variant::ar;
		profile.capacity = std::max(game_capacity - 1, 1);
		profile.flick = ak;
		profile.hold = traits.hold_open ? hold_open::yes : hold_open::no;
		return profile;
	}
}
