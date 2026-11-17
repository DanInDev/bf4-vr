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
			{ "U_SCAR-HSV", { 0.035f, 0.04f, 0.57f } }, // the same receiver
			// tools/handle_probe.py, 2026-11-17 (the same method, automated; re/notes/weapon.md "Charging handles, every crate
			// gun"): the hand riding the bolt in the game's empty reload, its pinch moved forward by the bolt's travel.
			// Its AK-12 came out at (-0.027, 0.056, 0.527), within 0.7 cm of the point above.
			{ "U_Scorpion", { 0.028f, 0.039f, 0.550f } },
			{ "U_JS2", { 0.071f, 0.111f, 0.404f } },
			{ "U_MX4", { 0.055f, 0.029f, 0.400f } },
			{ "U_MagpulPDR", { 0.029f, 0.012f, 0.487f } },
			{ "U_PP2000", { 0.003f, 0.051f, 0.511f } },
			{ "U_HAWK", { -0.003f, 0.082f, 0.708f } }, // the pump's fore-end
			{ "U_SAIGA_20K", { -0.080f, 0.025f, 0.455f } },
			{ "U_USAS-12_SP", { 0.031f, 0.000f, 0.774f } },
			{ "U_A91", { 0.046f, 0.012f, 0.362f } },
			{ "U_GalilACE52", { 0.059f, 0.006f, 0.429f } },
			{ "U_G36C", { 0.022f, 0.062f, 0.533f } },
			{ "U_FAMAS", { 0.046f, 0.024f, 0.451f } },
			{ "U_SAR21", { 0.085f, 0.068f, 0.401f } },
			{ "U_Ultimax", { 0.047f, 0.042f, 0.465f } },
			{ "U_QBU88", { -0.104f, 0.013f, 0.319f } },
			{ "U_RFB_SP", { 0.026f, -0.002f, 0.534f } },
			// handle_probe v2 (same day): only frames where the hand moves with the bolt. It reproduced the Hawk, Saiga,
			// QBU-88, Ultimax, PDW-R and AK-12 points above within 1 cm and added these.
			{ "U_CZ805", { 0.055f, -0.009f, 0.549f } },
			{ "U_M39EBR", { -0.102f, 0.013f, 0.428f } }, // the M14's operating rod handle, on the right
			// Bolt actions, from a shot (the game cycles the bolt with the right hand): the bolt knob, on the right.
			{ "U_SRS", { -0.084f, 0.116f, 0.243f } },
			{ "U_FY-JS", { -0.097f, 0.097f, 0.345f } },
			{ "U_Scout", { -0.085f, 0.116f, 0.439f } },
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

	bool magazine_grab_for(std::string_view unlock_name, float (&offset)[3])
	{
		struct measured
		{
			std::string_view name;
			float offset[3];
		};
		// tools/handle_probe.py, 2026-11-17: the game's left hand (thumb-index pinch) on the magazine while it moves with
		// it in the empty reload, relative to Wep_Mag (re/notes/weapon.md "Magazine grab points"). Taken: >= 4 such frames
		// within 6 cm; the rest keep magazine_grab_drop (8 cm straight below).
		static constexpr measured table[] = {
			{ "U_JS2", { -0.010f, -0.063f, -0.001f } },
			{ "U_UMP45", { -0.009f, -0.087f, 0.036f } },
			{ "U_HAWK", { -0.011f, -0.055f, -0.007f } },
			{ "U_SAIGA_20K", { -0.015f, -0.052f, 0.011f } },
			{ "U_AK5C", { 0.010f, -0.093f, 0.049f } },
			{ "U_G36C", { 0.009f, -0.081f, -0.011f } },
			{ "U_M4A1", { -0.008f, -0.088f, 0.016f } },
			{ "U_SG553LB", { 0.021f, -0.059f, 0.024f } },
			{ "U_AK12", { -0.016f, -0.041f, 0.039f } },
			{ "U_FAMAS", { 0.000f, -0.037f, 0.004f } },
			{ "U_M16A4", { -0.004f, -0.088f, 0.013f } },
			{ "U_M416", { -0.011f, -0.090f, 0.012f } },
			{ "U_QBZ951", { 0.013f, -0.043f, -0.015f } },
			{ "U_SCAR-H", { 0.032f, -0.052f, 0.022f } },
			{ "U_QBB95", { -0.003f, -0.001f, -0.025f } }, // the drum, taken at its middle
			{ "U_MK11", { 0.027f, -0.029f, -0.048f } },
			{ "U_M39EBR", { 0.005f, -0.065f, -0.012f } },
			{ "U_QBU88", { 0.000f, -0.037f, -0.006f } },
			{ "U_RFB_SP", { 0.007f, -0.015f, -0.023f } },
			{ "U_SCAR-HSV", { 0.033f, -0.052f, 0.021f } },
			{ "U_SVD12", { -0.012f, -0.040f, 0.040f } },
			{ "U_FY-JS", { 0.018f, -0.022f, 0.038f } },
			{ "U_Scout", { 0.014f, -0.013f, 0.048f } },
			{ "U_SV98", { 0.007f, -0.017f, 0.062f } },
		};
		for (const measured& gun : table)
		{
			if (gun.name == unlock_name)
			{
				std::copy(std::begin(gun.offset), std::end(gun.offset), offset);
				return true;
			}
		}
		return false;
	}

	bool rack_zones_for(std::string_view unlock_name, const hand_config& defaults, rack_zone_shape& shape)
	{
		shape = { defaults.rack_over_offset, defaults.rack_over_reach, defaults.rack_under_offset, defaults.rack_under_radii };
		if (unlock_name == "U_SCAR-H" || unlock_name == "U_SCAR-HSV")
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
