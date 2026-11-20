#include "weapon/profiles.hpp"

#include "weapon/gun_traits.hpp"

#include <algorithm>

namespace bfvr::weapon
{
	bool handle_point_from_model(std::string_view unlock_name)
	{
		static constexpr std::string_view names[] = { "U_AK5C", "U_UMP45", "U_AEK971", "U_SG553LB", "U_SteyrAug", "U_M16A4", "U_M416", "U_MK11",
			"U_QBZ951", "U_M240", "U_M249" };
		for (const std::string_view name : names)
		{
			if (name == unlock_name)
			{
				return true;
			}
		}
		return false;
	}

	bool bolt_geometry_for(std::string_view unlock_name, float (&pivot)[2], float& lift_degrees, float& travel)
	{
		struct bolt { std::string_view name; float pivot_y; float lift; };
		// Monitor 2026-11-20 (game's own cycle after a shot, Wep_Slide local): the turn about its -z, then 10.3 cm back.
		static constexpr bolt bolts[] = {
			{ "U_SRS", 0.006f, 56.4f },
			{ "U_FY-JS", 0.035f, 56.4f },
			{ "U_SV98", 0.050f, 56.4f },
			{ "U_Scout", 0.034f, 73.7f },
		};
		for (const bolt& b : bolts)
		{
			if (b.name == unlock_name)
			{
				pivot[0] = 0.0f;
				pivot[1] = b.pivot_y;
				lift_degrees = b.lift;
				travel = 0.103f;
				return true;
			}
		}
		return false;
	}

	bool handle_both_sides(std::string_view unlock_name)
	{
		// Handles a hand works from either side on the real gun, as drawn (owner 2026-11-20: every gun that can): the FAMAS's
		// lever under the carry handle, the P90's two cocking handles, the G36C's handle that folds out to either side under
		// its carry handle, the QBB-95's under its carry handle; the AK-12 (2012 prototype: its handle moved to either side;
		// near the top of the gun) by the owner's word. Not: reversible handles fitted to one side at a time (SCAR-H, SG553,
		// CZ-805, MX4, CZ-3A1, SAR-21), the AUG (left only), the PDR (not confirmed). The QBZ-95-1's is on the centre line
		// already (both sides reach it without a mirror).
		static constexpr std::string_view names[] = { "U_FAMAS", "U_AK12", "U_P90", "U_G36C", "U_QBB95" };
		for (const std::string_view name : names)
		{
			if (name == unlock_name)
			{
				return true;
			}
		}
		return false;
	}

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
			// The hand on the handle in the last frame before the bolt starts back (handle_probe pull_start; at ~10 fps the
			// hand trails the bolt by 2 cm in the pull's frame). It gives the AK-12 within 1 cm and the FAMAS within 0.4.
			{ "U_P90", { 0.049f, -0.004f, 0.460f } },   // left side, forward (the AR default sat on its top magazine)
			{ "U_QBB95", { 0.028f, 0.077f, 0.310f } },
			{ "U_SVD12", { -0.027f, 0.055f, 0.537f } }, // as the AK-12's
			// Tube pumps: where the game's left hand holds the fore-end (re/dumps/tube_points, 2026-11-17): the two-handed
			// hold and the pump in one (hand_frame pump).
			{ "U_870", { -0.002f, 0.065f, 0.725f } },
			{ "U_SPAS12", { 0.007f, 0.078f, 0.721f } },
			{ "U_UTAS", { 0.003f, 0.070f, 0.696f } },
			// Tube semi-automatics: their reload closes the bolt by the release, no hand on the handle. Estimated: the
			// bolt bone that moves (QBS-09 Wep_Slide, M1014 Wep_Extra1), its handle out on the right.
			{ "U_QBS09_SP", { -0.030f, 0.121f, 0.478f } },
			{ "U_M1014", { -0.030f, 0.104f, 0.492f } },
			// Bolt actions, from a shot (the game cycles the bolt with the right hand): the bolt knob, on the right.
			{ "U_SRS", { -0.084f, 0.116f, 0.243f } },
			{ "U_FY-JS", { -0.097f, 0.097f, 0.345f } },
			{ "U_Scout", { -0.085f, 0.116f, 0.439f } },
			// Read off its shot timeline (handle_probe timeline): the right hand on the lifted knob as the bolt starts
			// back (4.2 cm), moved forward by that travel.
			{ "U_SV98", { -0.049f, 0.108f, 0.408f } }, // #96: the game's hand at full lift before the bolt comes back (was mid-stroke)
		};
		// #88, from the model (handle_point_from_model): guns whose empty reload never pulls the handle (the bolt
		// release, or no hand on it). The bolt bone at rest (tools/handle_probe.py record --all-bones, 2026-11-20:
		// Wep_Slide in Wep_Root axes, +x left) put out on the handle's side (owner's table in #88) at a receiver's
		// half width from the centreline (x 0.030 / 0.035, the belt-feds 0.045; 1.8-3.5 cm out from the bone), at the
		// bone's height plus up to 1 cm. AR T-handle (M16A4, M416, MK11, whose rigs share the bolt bone
		// (0, 0.049, 0.373)): the handle sits at the rear end of the bolt carrier, 17.3 cm behind the bolt and 1.1 cm
		// above it, on top: (0, 0.060, 0.200), the AR point, which the owner's table calls right for them ("M4 style is
		// right; the hand placement is not": the fist, #87).
		for (const measured& gun : table)
		{
			if (gun.name == unlock_name)
			{
				std::copy(std::begin(gun.point), std::end(gun.point), point);
				return true;
			}
		}
		static constexpr measured from_model[] = {
			{ "U_AK5C", { -0.030f, 0.050f, 0.457f } },    // right side, AK style; bolt (0, 0.041, 0.457)
			{ "U_UMP45", { 0.030f, 0.056f, 0.576f } },    // front left, ahead of the magwell; bolt (0, 0.056, 0.576)
			{ "U_AEK971", { -0.035f, 0.040f, 0.497f } },  // right side; bolt (-0.009, 0.037, 0.497)
			{ "U_SG553LB", { -0.035f, 0.025f, 0.482f } }, // right side; bolt (0, 0.021, 0.482)
			{ "U_SteyrAug", { 0.035f, 0.025f, 0.454f } }, // front left, as the SCAR-H; bolt (0.016, 0.025, 0.454)
			{ "U_M16A4", { 0.000f, 0.060f, 0.200f } },    // rear T-handle (the AR point; bolt (0, 0.049, 0.373))
			{ "U_M416", { 0.000f, 0.060f, 0.200f } },
			{ "U_MK11", { 0.000f, 0.060f, 0.200f } },
			{ "U_QBZ951", { 0.000f, 0.060f, 0.350f } },   // top, under the carry handle; bolt (0, 0.054, 0.350)
			{ "U_M240", { -0.045f, 0.005f, 0.447f } },    // right side; bolt (-0.027, 0.001, 0.447)
			{ "U_M249", { -0.045f, 0.025f, 0.466f } },    // right side; bolt (-0.027, 0.021, 0.466)
		};
		for (const measured& gun : from_model)
		{
			if (gun.name == unlock_name)
			{
				std::copy(std::begin(gun.point), std::end(gun.point), point);
				return true;
			}
		}
		return false;
	}

	float magazine_hold_drop(std::string_view unlock_name)
	{
		float grab[3]{};
		if (!magazine_grab_for(unlock_name, grab) || !(grab[1] < 0.0f))
		{
			return 0.0f;
		}
		return std::clamp(-grab[1], 0.02f, 0.12f);
	}

	bool magazine_grab_for(std::string_view unlock_name, float (&offset)[3])
	{
		struct measured
		{
			std::string_view name;
			float offset[3];
		};
		// tools/handle_probe.py, 2026-11-17: the game's left hand (thumb-index pinch) on the magazine while it moves with
		// it in the empty reload, relative to Wep_Mag (re/notes/weapon.md "Magazine grab points"). Three takes pooled;
		// taken: >= 4 such frames within 6 cm; the rest keep magazine_grab_drop along their way out.
		static constexpr measured table[] = {
			{ "U_Scorpion", { 0.001f, -0.117f, 0.011f } },
			{ "U_JS2", { -0.009f, -0.062f, -0.004f } },
			{ "U_P90", { 0.001f, 0.018f, -0.158f } }, // behind the magazine on top of the gun
			{ "U_MagpulPDR", { -0.010f, -0.046f, -0.001f } },
			{ "U_UMP45", { -0.010f, -0.087f, 0.036f } },
			{ "U_HAWK", { -0.011f, -0.057f, -0.007f } },
			{ "U_SAIGA_20K", { -0.016f, -0.052f, 0.011f } },
			{ "U_GalilACE52", { 0.002f, -0.043f, 0.053f } },
			{ "U_AK5C", { 0.010f, -0.093f, 0.049f } },
			{ "U_G36C", { 0.005f, -0.082f, -0.036f } },
			{ "U_M4A1", { -0.006f, -0.090f, 0.012f } },
			{ "U_SG553LB", { 0.016f, -0.060f, 0.020f } },
			{ "U_AK12", { -0.018f, -0.043f, 0.039f } },
			{ "U_SteyrAug", { -0.020f, -0.040f, -0.021f } },
			{ "U_FAMAS", { 0.000f, -0.037f, 0.002f } },
			{ "U_M16A4", { -0.004f, -0.088f, 0.016f } },
			{ "U_M416", { -0.008f, -0.089f, 0.012f } },
			{ "U_SAR21", { -0.003f, -0.135f, 0.044f } },
			{ "U_SCAR-H", { 0.030f, -0.053f, 0.022f } },
			{ "U_MK11", { 0.011f, -0.030f, -0.048f } },
			{ "U_M39EBR", { 0.008f, -0.062f, -0.007f } },
			{ "U_QBU88", { 0.000f, -0.037f, -0.006f } },
			{ "U_QBZ951", { 0.014f, -0.043f, -0.015f } },
			{ "U_RFB_SP", { 0.007f, -0.014f, -0.023f } },
			{ "U_SCAR-HSV", { 0.032f, -0.053f, 0.022f } },
			{ "U_SVD12", { -0.018f, -0.040f, 0.040f } },
			{ "U_FY-JS", { 0.012f, -0.028f, 0.038f } },
			{ "U_Scout", { 0.012f, -0.014f, 0.047f } },
			{ "U_SV98", { 0.007f, -0.017f, 0.061f } },
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

	bool magazine_way_out_for(std::string_view unlock_name, float& degrees)
	{
		struct measured
		{
			std::string_view name;
			float degrees;
		};
		// tools/handle_probe.py, 2026-11-17: the direction Wep_Mag first moves in the game's empty reload, in the gun's
		// mid-plane (its sideways part is the animation's hand). Most guns read within 10 degrees of straight down, as the
		// AK-12 (10/5) and SCAR-H (1/4), the reference, and keep the straight rail. Here only guns whose reading at 3 and
		// 6 cm out agree and lie clearly off it (re/notes/weapon.md "Magazine grab points").
		static constexpr measured table[] = {
			// Three takes each (3 cm / 6 cm out, medians).
			{ "U_GalilACE52", 17.0f },  // 17/17 (takes 11-32), rocked forward off its front hook
			{ "U_SAIGA_20K", 32.0f },   // 26/38, the AK rock
			{ "U_QBB95", 25.0f },       // 31/24, the drum tips forward out of the well
			{ "U_SAR21", 17.0f },       // 17/17 in two takes, 7 in the third
			{ "U_MagpulPDR", -12.0f },  // -12/-12 every take
			{ "U_P90", -101.0f },       // -102/-100: lifted at the back and drawn back off the top
		};
		for (const measured& gun : table)
		{
			if (gun.name == unlock_name)
			{
				degrees = gun.degrees;
				return true;
			}
		}
		return false;
	}

	float lid_degrees_for(std::string_view unlock_name)
	{
		// handle_probe --all-bones, 2026-11-17: Wep_Extra1's turn at the top of the game's empty reload (the REX: its frame,
		// Wep_Slide).
		if (unlock_name == "U_MP412Rex")
		{
			return 85.0f;
		}
		if (unlock_name == "U_Pecheneg")
		{
			return 93.7f;
		}
		if (unlock_name == "U_Type88")
		{
			return 81.7f;
		}
		return 102.6f; // M249, M240, MG4
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
		profile.pull_out = !ak && traits.release == release::none && (traits.feed == feed::box || traits.feed == feed::top);
		profile.hold = traits.hold_open ? hold_open::yes : hold_open::no;
		profile.manual_cycle = cycles_by_hand(traits) && physical_reload_covers(traits);
		profile.bolt_turn = profile.manual_cycle && traits.cycle == cycle::bolt;
		profile.tube = traits.feed == feed::tube;
		profile.belt = traits.feed == feed::belt;
		profile.revolver = traits.feed == feed::cylinder;
		if (profile.tube || profile.belt || profile.revolver)
		{
			profile.kind = variant::ar; // nothing to rock; no flick
			profile.flick = false;
		}
		return profile;
	}
}
