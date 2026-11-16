// Checks the own crate's gun list and its change watch (src/weapon/crate_guns, #34): 51 guns plus "keep", the panel's
// choices in the same order, the settings using exactly these values, and a give only on a real change to a gun.
#include <cstdio>
#include <string>
#include <vector>

#include "common/setting_catalog.hpp"
#include "common/settings_store.hpp"
#include "weapon/crate_guns.hpp"

using namespace bfvr;

namespace
{
	int failures = 0;

	void expect(bool condition, const char* what)
	{
		if (!condition)
		{
			std::printf("FAIL: %s\n", what);
			++failures;
		}
	}

	std::vector<std::string> split(const std::string& text)
	{
		std::vector<std::string> parts;
		std::size_t start = 0;
		for (std::size_t bar; (bar = text.find('|', start)) != std::string::npos; start = bar + 1)
		{
			parts.push_back(text.substr(start, bar - start));
		}
		parts.push_back(text.substr(start));
		return parts;
	}
}

int main()
{
	settings::load_global();
	const auto values = split(weapon::crate::values);
	const auto choices = split(weapon::crate::choices);
	expect(values.size() == weapon::crate::gun_count + 1, "51 guns and keep");
	expect(values.front() == "keep", "keep first (the default)");
	expect(choices.size() == values.size(), "a readable name for each value");
	for (std::size_t i = 0; i < values.size() && i < choices.size(); ++i)
	{
		if (choices[i].rfind(values[i] + "=", 0) != 0)
		{
			std::printf("FAIL: choice %zu is %s, value %s\n", i, choices[i].c_str(), values[i].c_str());
			++failures;
		}
		if (i > 0 && values[i].rfind("U_", 0) != 0)
		{
			std::printf("FAIL: %s is no unlock debug id\n", values[i].c_str());
			++failures;
		}
	}
	for (const char* name : { "crate_primary", "crate_secondary" })
	{
		const auto definition = settings::global().definition_of(name);
		expect(definition && definition->choices == weapon::crate::values, "the setting offers exactly the crate's values");
		expect(definition && definition->fallback == "keep", "and keeps the guns by default");
		const auto* shown = settings::presentation_of(name);
		expect(shown && std::string(shown->choices) == weapon::crate::choices, "the panel shows the readable names");
	}

	weapon::crate::change_watch watch;
	expect(!watch.update("U_M416"), "the first value seen (the ini's) gives nothing");
	expect(!watch.update("U_M416"), "unchanged: nothing");
	expect(watch.update("U_AK12"), "a new gun picked: give it");
	expect(!watch.update("U_AK12"), "once");
	expect(!watch.update("keep"), "keep gives nothing");
	expect(watch.update("U_AK12"), "the same gun again after keep: give it again");
	if (failures == 0)
	{
		std::printf("crate_guns: all checks passed\n");
	}
	return failures == 0 ? 0 : 1;
}
