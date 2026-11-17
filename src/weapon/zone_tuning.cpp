#include "weapon/zone_tuning.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <sstream>

namespace bfvr::weapon::zone_tuning
{
	namespace
	{
		constexpr const char* names[zone_count] = { "handle", "magazine", "lid" };

		float clamp(float value, float low, float high)
		{
			return std::isfinite(value) ? std::fmax(low, std::fmin(high, value)) : low > 0.0f ? 1.0f : 0.0f;
		}
	}

	const char* name_of(zone which)
	{
		const int index = static_cast<int>(which);
		return index >= 0 && index < zone_count ? names[index] : "?";
	}

	zone zone_named(std::string_view name)
	{
		for (int i = 0; i < zone_count; ++i)
		{
			if (name == names[i])
			{
				return static_cast<zone>(i);
			}
		}
		return zone::count;
	}

	tune clamped(tune value)
	{
		value.right = clamp(value.right, -max_shift_cm, max_shift_cm);
		value.up = clamp(value.up, -max_shift_cm, max_shift_cm);
		value.forward = clamp(value.forward, -max_shift_cm, max_shift_cm);
		value.size = clamp(value.size, min_size, max_size);
		return value;
	}

	bool is_default(const tune& value)
	{
		return std::fabs(value.right) < 1e-4f && std::fabs(value.up) < 1e-4f && std::fabs(value.forward) < 1e-4f && std::fabs(value.size - 1.0f) < 1e-4f;
	}

	shift in_gun_frame(const tune& value)
	{
		return { -value.right * 0.01f, value.up * 0.01f, value.forward * 0.01f };
	}

	std::vector<std::string> parse(std::string_view text, book& out)
	{
		std::vector<std::string> warnings;
		std::istringstream lines{ std::string(text) };
		std::string line;
		int number = 0;
		while (std::getline(lines, line))
		{
			++number;
			const std::size_t comment = line.find('#');
			if (comment != std::string::npos)
			{
				line.erase(comment);
			}
			std::istringstream fields(line);
			std::string gun, zone_name;
			if (!(fields >> gun))
			{
				continue; // blank or comment
			}
			tune value;
			std::string rest;
			if (!(fields >> zone_name >> value.right >> value.up >> value.forward >> value.size) || (fields >> rest))
			{
				warnings.push_back("line " + std::to_string(number) + ": want <gun> <zone> <right> <up> <forward> <size>");
				continue;
			}
			const zone which = zone_named(zone_name);
			if (which == zone::count)
			{
				warnings.push_back("line " + std::to_string(number) + ": unknown zone '" + zone_name + "' (handle, magazine, lid)");
				continue;
			}
			out[gun].zones[static_cast<int>(which)] = clamped(value);
		}
		return warnings;
	}

	std::string format(const book& tunes)
	{
		std::string text =
			"# bfvr_zones.ini: grab zones tuned per gun (panel Weapon > Tune grab zones).\n"
			"# <gun> <zone: handle|magazine|lid> <right cm> <up cm> <forward cm> <size, 1 = as built>\n";
		char line[160];
		for (const auto& [gun, zones] : tunes)
		{
			for (int i = 0; i < zone_count; ++i)
			{
				const tune& value = zones.zones[i];
				if (is_default(value))
				{
					continue;
				}
				std::snprintf(line, sizeof(line), "%s %s %g %g %g %g\n", gun.c_str(), names[i], value.right, value.up, value.forward, value.size);
				text += line;
			}
		}
		return text;
	}
}
