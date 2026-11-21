#pragma once

// Foregrip attachments by unlock name (#91). A gun's unlock set (weapon/sight_state) names its foregrip,
// "U_SCAR-H_Foregrip_Angled", "U_AK12_Foregrip_Folding"; the suffix tells the kind. With one mounted the game's
// own left hand holds that grip at one fixed spot with its own grip pose, so the off hand stays there instead of
// sliding along the handguard. Engine-free; tests/foregrip_test.cpp.
namespace bfvr::weapon
{
	enum class foregrip_kind : int
	{
		none = 0,     // no foregrip (a handguard hold), or a bipod
		vertical = 1, // a post under the barrel: vertical, folding, ergo; the fist round the post
		angled = 2,   // the angled grip: the palm on its face, the thumb over
		stubby = 3,   // potato or stubby: the fist round the ball
	};

	// One unlock name; the weapon part before the attachment does not matter. Case-insensitive.
	foregrip_kind classify_foregrip(const char* unlock_name);

	const char* foregrip_name(foregrip_kind kind);
}
