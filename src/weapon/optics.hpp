#pragma once

// Optic attachments by unlock name (#39). A gun's unlocks (weapon/sight_state) name its attachments,
// "U_AK12_PK-AS", "U_SCAR-H_EOTech", "U_AK12_Magnifier"; the suffix tells the kind of sight. Engine-free;
// tests/optics_test.cpp. Seen live: PK-AS, EOTech, Magnifier; the other names are BF4's optic list as
// known, not yet seen in this campaign.
namespace bfvr::weapon
{
	enum class optic_kind : int
	{
		none = 0,      // not an optic (a grip, barrel, laser, camo) or unknown
		reflex = 1,    // a red dot or holographic sight: 1x, the mod's collimated dot
		magnified = 2, // a magnified scope: the lens picture, no dot of ours
		magnifier = 3, // the flip magnifier behind a red dot
	};

	struct optic_info
	{
		optic_kind kind{ optic_kind::none };
		float magnification{ 1.0f }; // nominal (magnified scopes; the magnifier's measured 1.73)
	};

	// One unlock name; the weapon part before the optic suffix does not matter.
	optic_info classify_optic(const char* unlock_name);

	// What a gun's unlock set adds up to.
	struct gun_optics
	{
		bool reflex{};
		bool magnifier{};
		bool magnified{};
		float magnification{ 1.0f }; // the magnified scope's nominal zoom
	};
	void add_optic(gun_optics& gun, const char* unlock_name);
}
