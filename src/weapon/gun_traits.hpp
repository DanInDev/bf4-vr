#pragma once

#include <cstddef>
#include <string_view>

// docs/tasks/all-weapons.md step 1: the five properties that describe how a gun loads and cycles, per crate gun, from
// the real guns checked against the game's data (docs/weapons.md, #81). The reload state machine reads feed, release
// and hold_open; zones will read bullpup; the trigger will read cycle. Engine-free; tested by tests/gun_traits_test.cpp.
namespace bfvr::weapon
{
	enum class feed
	{
		box,      // a box magazine pushed straight in
		rock,     // hooked at the front and rocked back (AK, M14, FAL, SVD)
		top,      // laid on top (P90)
		drum,     // a drum or a large box used like one
		belt,     // belt-fed: lid, box, belt (family H)
		tube,     // shell by shell into a tube (family F)
		cylinder, // a revolver (family J)
	};

	enum class release
	{
		button,   // a button or paddle frees the magazine
		none,     // the hand rocks it out (AK pattern)
		lever,    // a lid or a latch (belt-fed, revolver)
	};

	enum class cycle
	{
		none,          // semi or automatic: nothing between shots
		pump,          // the fore-end back and forward after each shot
		bolt,          // a bolt action after each shot
		double_action, // a revolver's long trigger
	};

	struct gun_traits
	{
		std::string_view unlock;
		feed feed{ feed::box };
		release release{ release::button };
		bool hold_open{ true }; // the last shot locks the bolt back; the bolt catch closes it
		bool bullpup{};
		cycle cycle{ cycle::none };
		bool verified{};        // false: hold-open or release taken as the common case, not yet checked against the model
	};

	// The crate gun's traits; any other gun gets a box magazine with a button and hold-open (today's AR behaviour).
	gun_traits traits_for(std::string_view unlock_name);

	// The physical magazine reload (state machine AR/AK) covers box, rock-in, top and drum feeds. Belt, tube and revolver
	// guns keep the game's own reload until their families are built (the AR reload treated them as box magazines).
	bool physical_reload_covers(const gun_traits& traits);

	// The crate table, for tests and tools.
	std::size_t crate_gun_count();
	std::string_view crate_gun_at(std::size_t index);
}
