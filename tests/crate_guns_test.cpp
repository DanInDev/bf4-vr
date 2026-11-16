// Checks the own crate's gun list and its change watch (src/weapon/crate_guns, #80): 51 guns plus "keep", the panel's
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

	weapon::crate::change_watch watch(1000);
	expect(watch.update("U_M416", 0, false).empty(), "the first value seen (the ini's) gives nothing");
	expect(watch.update("U_M416", 5000, false).empty(), "unchanged: nothing");
	expect(watch.update("U_AK12", 6000, false).empty(), "a new gun: not before it has settled");
	expect(watch.update("U_AK12", 7000, false) == "U_AK12", "settled for 1 s: give it");
	expect(watch.update("U_AK12", 9000, false).empty(), "once");
	expect(watch.update("keep", 10000, false).empty() && watch.update("keep", 12000, false).empty(), "keep gives nothing");
	expect(watch.update("U_AK12", 13000, false).empty() && watch.update("U_AK12", 14000, false) == "U_AK12", "the same gun again after keep: give it again");

	// Stepping on the panel: Keep -> A-91 -> ACE 52 -> AK 5C, 200 ms apart, then it rests.
	weapon::crate::change_watch steps(1000);
	steps.update("keep", 0, false);
	int given = 0;
	std::string last;
	std::uint64_t t = 100;
	for (const char* gun : { "U_A91", "U_GalilACE52", "U_AK5C" })
	{
		const std::string out = steps.update(gun, t, false);
		given += out.empty() ? 0 : 1;
		t += 200;
	}
	for (; t < 3000; t += 100)
	{
		const std::string out = steps.update("U_AK5C", t, false);
		if (!out.empty())
		{
			++given;
			last = out;
		}
	}
	expect(given == 1 && last == "U_AK5C", "stepping past guns gives only the one it rests on");

	// A give still running: the newest choice waits for it and then wins.
	weapon::crate::change_watch busy(1000);
	busy.update("keep", 0, false);
	busy.update("U_M416", 0, false);
	expect(busy.update("U_M416", 1500, false) == "U_M416", "first pick given");
	busy.update("U_P90", 1600, true);
	expect(busy.update("U_P90", 3000, true).empty(), "the next waits while the first give runs");
	expect(busy.update("U_P90", 3100, false) == "U_P90", "and is given once it is done (latest wins)");
	if (failures == 0)
	{
		std::printf("crate_guns: all checks passed\n");
	}
	return failures == 0 ? 0 : 1;
}
