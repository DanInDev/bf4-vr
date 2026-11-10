#include "weapon/optics.hpp"

#include <cctype>
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
		};
		// Matched on the part after the weapon's name, without case or separators ("PK-AS" == "pkas").
		// Reflex sights first: "pkas" must not fall to the magnified "pka".
		constexpr entry table[] = {
			{ "magnifier", optic_kind::magnifier, 1.73f },
			{ "pkas", optic_kind::reflex, 1.0f },
			{ "eotech", optic_kind::reflex, 1.0f },
			{ "holo", optic_kind::reflex, 1.0f },
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
				return { e.kind, e.magnification };
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
}
