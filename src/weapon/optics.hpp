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
	// (the game then draws no reticle of its own and walks at full speed); yes for everything else. #139: with the
	// scope glass on (`glass`) a scope or magnifier shows its zoom in its own glass, so it needs no game aim either
	// (the game's aim with a high-power scope hides the gun and draws its flat sniper overlay); a red dot or holo that
	// is black at the hip and iron sights still do.
	bool needs_game_aim(const gun_optics& gun, bool glass = false);
	void add_optic(gun_optics& gun, const char* unlock_name);

	// #138: an optic's magnification from the game's own zoom data (no name table): the hip's vertical FOV
	// against the zoom level's, tan(hip / 2) / tan(level / 2), degrees. 0 when either is not a usable FOV.
	// Live: magnifier 27 -> 1.73, ACOG 17.2 -> 2.74, SRS rifle scope 8.7 -> 5.44, red dot 40 -> 1.13 (hip 45).
	float optic_power(float hip_fov_degrees, float level_fov_degrees);
	// Below this the zoom is the game's ADS nudge of a red dot or holo (45 -> 40 degrees): a 1x sight.
	constexpr float min_optic_power = 1.25f;
	// The selected zoom level's power from the levels' FOVs (degrees, level 0 = the hip): optic_power of that level,
	// 1 for a 1x sight (below min_optic_power), 0 when the level is out of range (0, past the levels or the array)
	// or a FOV is unusable (the caller falls back to the learned zoom or the name table).
	float level_power(const float* level_fov, int levels, int capacity, int selected);
}
