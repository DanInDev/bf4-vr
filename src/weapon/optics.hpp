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
		// The lens is see-through without the game's aim (the SCAR-H's EOTech, live); the AK-12's PK-AS
		// is black at the hip. Unknown optics count as not clear.
		bool clear_at_hip{};
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
		bool reflex_clear{};          // the red dot or holo is see-through at the hip
	};
	// sight_ads: does this gun need the game's aim to see through its sight? Not for a clear holo alone
	// (the game then draws no reticle of its own and walks at full speed); yes for everything else.
	bool needs_game_aim(const gun_optics& gun);
	void add_optic(gun_optics& gun, const char* unlock_name);
}
