#include "weapon/optics.hpp"

#include <cctype>
#include <cmath>
#include <cstring>

namespace bfvr::weapon
{
	namespace
	{
		struct entry
		{
			const char* suffix;
			optic_kind kind;
			float magnification;
			bool clear_at_hip = false;
		};
		// Matched on the part after the weapon's name, without case or separators ("PK-AS" == "pkas").
		// Reflex sights first: "pkas" must not fall to the magnified "pka".
		constexpr entry table[] = {
			{ "magnifier", optic_kind::magnifier, 1.73f },
			{ "pkas", optic_kind::reflex, 1.0f },
			{ "eotech", optic_kind::reflex, 1.0f, true },
			{ "holo", optic_kind::reflex, 1.0f, true },
			{ "kobra", optic_kind::reflex, 1.0f },
			{ "rds", optic_kind::reflex, 1.0f },
			{ "reflex", optic_kind::reflex, 1.0f },
			{ "coyote", optic_kind::reflex, 1.0f },
			{ "hd33", optic_kind::reflex, 1.0f },
			{ "mini", optic_kind::reflex, 1.0f },
			{ "acog", optic_kind::magnified, 4.0f },
			{ "pso", optic_kind::magnified, 4.0f },
			{ "pka", optic_kind::magnified, 3.4f },
			{ "prisma", optic_kind::magnified, 3.4f },
			{ "m145", optic_kind::magnified, 3.4f },
			{ "jgm4", optic_kind::magnified, 4.0f },
			{ "hunter", optic_kind::magnified, 6.0f },
			{ "ballistic", optic_kind::magnified, 14.0f },
			{ "scope", optic_kind::magnified, 6.0f },
		};

		// The name after the last underscore, lowercased, letters and digits only.
		void suffix_of(const char* name, char (&out)[32])
		{
			const char* last = std::strrchr(name, '_');
			const char* from = last ? last + 1 : name;
			std::size_t n = 0;
			for (const char* c = from; *c && n + 1 < sizeof(out); ++c)
			{
				if (std::isalnum(static_cast<unsigned char>(*c)))
				{
					out[n++] = static_cast<char>(std::tolower(static_cast<unsigned char>(*c)));
				}
			}
			out[n] = 0;
		}
	}

	optic_info classify_optic(const char* unlock_name)
	{
		if (!unlock_name || !unlock_name[0])
		{
			return {};
		}
		char suffix[32]{};
		suffix_of(unlock_name, suffix);
		for (const entry& e : table)
		{
			if (std::strncmp(suffix, e.suffix, std::strlen(e.suffix)) == 0)
			{
				return { e.kind, e.magnification, e.clear_at_hip };
			}
		}
		return {};
	}

	void add_optic(gun_optics& gun, const char* unlock_name)
	{
		const optic_info optic = classify_optic(unlock_name);
		switch (optic.kind)
		{
		case optic_kind::reflex:
			gun.reflex = true;
			gun.reflex_clear = optic.clear_at_hip;
			break;
		case optic_kind::magnifier:
			gun.magnifier = true;
			break;
		case optic_kind::magnified:
			gun.magnified = true;
			gun.magnification = optic.magnification;
			break;
		default:
			break;
		}
	}

	bool needs_game_aim(const gun_optics& gun, bool glass)
	{
		if (!glass)
		{
			return !(gun.reflex && gun.reflex_clear && !gun.magnifier && !gun.magnified);
		}
		const bool irons = !gun.reflex && !gun.magnified && !gun.magnifier;
		return irons || (gun.reflex && !gun.reflex_clear);
	}

	float optic_power(float hip_fov_degrees, float level_fov_degrees)
	{
		constexpr float half_degree = 3.14159265f / 360.0f;
		if (!(hip_fov_degrees > 1.0f && hip_fov_degrees < 170.0f && level_fov_degrees > 0.5f && level_fov_degrees < 170.0f))
		{
			return 0.0f;
		}
		return std::tan(hip_fov_degrees * half_degree) / std::tan(level_fov_degrees * half_degree);
	}

	float level_power(const float* level_fov, int levels, int capacity, int selected)
	{
		if (!level_fov || selected <= 0 || selected >= levels || selected >= capacity)
		{
			return 0.0f;
		}
		const float power = optic_power(level_fov[0], level_fov[selected]);
		return power > 0.0f && power < min_optic_power ? 1.0f : power;
	}
}
