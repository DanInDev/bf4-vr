#include "weapon/foregrip.hpp"

#include <cctype>
#include <string>

namespace bfvr::weapon
{
	foregrip_kind classify_foregrip(const char* unlock_name)
	{
		if (!unlock_name)
		{
			return foregrip_kind::none;
		}
		std::string name;
		for (const char* c = unlock_name; *c; ++c)
		{
			name += static_cast<char>(std::tolower(static_cast<unsigned char>(*c)));
		}
		// No weapon name holds "grip"; a bipod is not held by the off hand.
		if (name.find("grip") == std::string::npos && name.find("ergo") == std::string::npos)
		{
			return foregrip_kind::none;
		}
		if (name.find("bipod") != std::string::npos)
		{
			return foregrip_kind::none;
		}
		if (name.find("angled") != std::string::npos)
		{
			return foregrip_kind::angled;
		}
		if (name.find("potato") != std::string::npos || name.find("stubby") != std::string::npos)
		{
			return foregrip_kind::stubby;
		}
		return foregrip_kind::vertical;
	}

	const char* foregrip_name(foregrip_kind kind)
	{
		switch (kind)
		{
		case foregrip_kind::vertical: return "vertical";
		case foregrip_kind::angled: return "angled";
		case foregrip_kind::stubby: return "stubby";
		default: return "none";
		}
	}
}
