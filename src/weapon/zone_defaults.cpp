#include "weapon/zone_defaults.hpp"

namespace bfvr::weapon::zone_tuning
{
	// #152: the grab zones as tuned in the headset, shipped with the mod. Written by tools/zones_to_defaults.py from
	// the owner's bfvr_zones.ini (same format); a player's own bfvr_zones.ini overrides these zone by zone.
	std::string_view built_in_defaults()
	{
		return R"zones(# Built-in grab-zone tunes (tools/zones_to_defaults.py). "<gun> <zone> <right> <up> <forward> <size>".
)zones";
	}

	book with_overrides(const book& defaults, const book& overrides)
	{
		book merged = defaults;
		for (const auto& [gun, tunes] : overrides)
		{
			gun_tunes& target = merged[gun];
			for (int index = 0; index < zone_count; ++index)
			{
				if (!is_default(tunes.zones[index]))
				{
					target.zones[index] = tunes.zones[index];
				}
			}
		}
		return merged;
	}
}
