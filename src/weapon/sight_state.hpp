#pragma once

#include <string>

#include "weapon/optics.hpp"

// The current weapon's sight as the game holds it (#39), readable at the hip: which zoom level the next
// aim uses (the AK-12's flip magnifier switches it 1 <-> 2) and that level's SightType. BF4 x64 only.
namespace bfvr::weapon
{
	enum class sight_type : int
	{
		none = 0,      // SightType_None: iron sights, red dots, holos, scopes
		canted = 1,    // SightType_Canted
		magnifier = 2, // SightType_Magnifier: the flip magnifier behind a red dot
	};

	struct sight_state
	{
		bool valid{};
		char weapon[48]{};    // unlock name ("U_AK12")
		int levels{};         // zoom levels including level 0 (the hip)
		int selected{};       // the level the next aim uses (aiming simulation +0x1F8)
		int current{};        // the level in use now (0 at the hip; +0x1E8)
		sight_type selected_type{ sight_type::none };
		// The gun's attachments from its unlock set (weapons component +0x120 + 0xC0 * slot: a tree of
		// unlock assets, re/notes/weapon.md): which optics it carries, and the optic's unlock name.
		bool attachments_known{};
		gun_optics optics;
		char optic[48]{};
	};

	// Reads the game (any thread; a dozen ReadProcessMemory calls).
	sight_state read_sight_state();
	// Pipe "sightstate".
	std::string sight_state_report();
}
