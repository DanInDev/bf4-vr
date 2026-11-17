#include "weapon/gun_traits.hpp"

namespace bfvr::weapon
{
	namespace
	{
		// The 51 crate guns (docs/weapons.md order). Sources: the real guns, docs/tasks/all-weapons.md (families), the game's
		// data (#81: ReloadType single = tube, magazine without a tactical reload at 100-200 rounds = belt, fire logic
		// bolt = pump or bolt action). verified = false where the hold-open or the release is the common case, not checked.
		constexpr gun_traits crate[] = {
			// Sidearm
			{ "U_MP412Rex", feed::cylinder, release::lever, false, false, cycle::double_action, true },
			// PDW
			{ "U_CBJ-MS", feed::box, release::button, true, false, cycle::none, false },
			{ "U_Scorpion", feed::box, release::button, true, false, cycle::none, true }, // CZ-3A1: bolt catch
			{ "U_JS2", feed::box, release::button, true, false, cycle::none, false },
			{ "U_MX4", feed::box, release::button, true, false, cycle::none, false },     // magazine through the grip
			{ "U_P90", feed::top, release::button, false, true, cycle::none, true },
			{ "U_MagpulPDR", feed::box, release::button, true, true, cycle::none, true },
			{ "U_PP2000", feed::box, release::button, true, false, cycle::none, false },  // magazine in the grip
			{ "U_UMP45", feed::box, release::button, false, false, cycle::none, true },   // HK: no hold-open, paddle
			// Shotgun
			{ "U_870", feed::tube, release::none, false, false, cycle::pump, true },
			{ "U_HAWK", feed::box, release::button, false, false, cycle::pump, true },    // pump with a box magazine
			{ "U_M1014", feed::tube, release::none, false, false, cycle::none, true },
			{ "U_QBS09_SP", feed::tube, release::none, false, false, cycle::none, true },
			{ "U_SAIGA_20K", feed::rock, release::none, false, false, cycle::none, true }, // AK action
			{ "U_SPAS12", feed::tube, release::none, false, false, cycle::pump, true },
			{ "U_USAS-12_SP", feed::drum, release::button, true, false, cycle::none, false },
			{ "U_UTAS", feed::tube, release::none, false, true, cycle::pump, true },
			// Carbine
			{ "U_A91", feed::rock, release::none, false, true, cycle::none, true },        // AK magazines, bullpup
			{ "U_GalilACE52", feed::rock, release::none, false, false, cycle::none, false },
			{ "U_AK5C", feed::box, release::button, true, false, cycle::none, false },
			{ "U_G36C", feed::box, release::button, true, false, cycle::none, true },     // paddle release
			{ "U_M4A1", feed::box, release::button, true, false, cycle::none, true },
			{ "U_SG553LB", feed::box, release::button, true, false, cycle::none, true },
			// Assault rifle
			{ "U_AEK971", feed::rock, release::none, false, false, cycle::none, true },
			{ "U_AK12", feed::rock, release::none, false, false, cycle::none, true },
			{ "U_SteyrAug", feed::box, release::button, true, true, cycle::none, true },
			{ "U_CZ805", feed::box, release::button, true, false, cycle::none, true },
			{ "U_FAMAS", feed::box, release::button, false, true, cycle::none, false },
			{ "U_M16A4", feed::box, release::button, true, false, cycle::none, true },
			{ "U_M416", feed::box, release::button, true, false, cycle::none, true },
			{ "U_QBZ951", feed::box, release::button, false, true, cycle::none, false },
			{ "U_SAR21", feed::box, release::button, true, true, cycle::none, true },
			{ "U_SCAR-H", feed::box, release::button, true, false, cycle::none, true },
			// LMG
			{ "U_M240", feed::belt, release::lever, false, false, cycle::none, true },
			{ "U_M249", feed::belt, release::lever, false, false, cycle::none, true },
			{ "U_MG4", feed::belt, release::lever, false, false, cycle::none, true },
			{ "U_Pecheneg", feed::belt, release::lever, false, false, cycle::none, true },
			{ "U_QBB95", feed::drum, release::button, false, true, cycle::none, false },
			{ "U_Type88", feed::belt, release::lever, false, false, cycle::none, true },
			{ "U_Ultimax", feed::box, release::button, false, false, cycle::none, true },  // open bolt: no hold-open
			// DMR
			{ "U_MK11", feed::box, release::button, true, false, cycle::none, true },
			{ "U_M39EBR", feed::rock, release::none, true, false, cycle::none, true },     // M14: rock-in, hold-open
			{ "U_QBU88", feed::box, release::button, true, true, cycle::none, true },
			{ "U_RFB_SP", feed::rock, release::none, true, true, cycle::none, true },     // FAL magazines, bullpup
			{ "U_SCAR-HSV", feed::box, release::button, true, false, cycle::none, true },
			{ "U_SVD12", feed::rock, release::none, true, false, cycle::none, true },
			// Sniper
			{ "U_SRS", feed::box, release::button, false, true, cycle::bolt, true },
			{ "U_FY-JS", feed::box, release::button, false, false, cycle::bolt, true },
			{ "U_M82A3_SP", feed::box, release::button, true, false, cycle::none, true },
			{ "U_Scout", feed::box, release::button, false, false, cycle::bolt, true },
			{ "U_SV98", feed::box, release::button, false, false, cycle::bolt, true },
		};

		// Rock-in guns outside the crate that already had the AK reload (the old ak_family list).
		constexpr gun_traits other_rock[] = {
			{ "U_AKU12", feed::rock, release::none, false, false, cycle::none, true },
			{ "U_AK74M", feed::rock, release::none, false, false, cycle::none, true },
			{ "U_AN94", feed::rock, release::none, false, false, cycle::none, true },
			{ "U_RPK12", feed::rock, release::none, false, false, cycle::none, true },
			{ "U_RPK-74M", feed::rock, release::none, false, false, cycle::none, true },
			{ "U_RPK74M", feed::rock, release::none, false, false, cycle::none, true },
			{ "U_GROZA-1", feed::rock, release::none, false, true, cycle::none, true },
		};
	}

	gun_traits traits_for(std::string_view unlock_name)
	{
		for (const gun_traits& gun : crate)
		{
			if (gun.unlock == unlock_name)
			{
				return gun;
			}
		}
		for (const gun_traits& gun : other_rock)
		{
			if (gun.unlock == unlock_name)
			{
				return gun;
			}
		}
		gun_traits fallback;
		fallback.unlock = unlock_name;
		return fallback;
	}

	bool physical_reload_covers(const gun_traits& traits)
	{
		return traits.feed == feed::box || traits.feed == feed::rock || traits.feed == feed::top || traits.feed == feed::drum
			|| traits.feed == feed::tube  // shell by shell (reload_profile tube, 2026-11-17)
			|| traits.feed == feed::belt; // lid, box, belt (reload_profile belt, 2026-11-17)
	}

	bool cycles_by_hand(const gun_traits& traits)
	{
		return traits.cycle == cycle::bolt || traits.cycle == cycle::pump;
	}

	std::size_t crate_gun_count()
	{
		return sizeof(crate) / sizeof(crate[0]);
	}

	std::string_view crate_gun_at(std::size_t index)
	{
		return index < crate_gun_count() ? crate[index].unlock : std::string_view{};
	}
}
