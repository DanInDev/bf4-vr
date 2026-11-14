#pragma once

#include <span>
#include <string>
#include <string_view>

// How each live setting is presented to players: its page, section, a plain-language name, one
// short line under it, and readable names for its choices. Shared by the in-VR panel and the
// launcher, so both show the same thing. Engine-free; tested by tests/settings_panel_test.cpp.
namespace bfvr::settings
{
	struct presentation
	{
		std::string_view name;    // the setting (ini key)
		std::string_view page;    // View, Body, Weapon, HUD, Controls
		std::string_view section; // heading within the page
		std::string_view label;   // a short name, sentence case
		std::string_view summary; // one short line under the name
		std::string_view choices; // "value=Label|value=Label" in display order; empty: the values as they are
		bool advanced{};          // shown on the Advanced page, under its page's name
		bool startup{};           // read when the game starts (settings::startup_definitions): launcher only
	};

	// In display order within each page and section.
	std::span<const presentation> catalog();
	const presentation* presentation_of(std::string_view name);
	// The pages in order: Start-up (launcher only), View, Body, Weapon, HUD, Controls, Skeleton (development), Advanced.
	std::span<const std::string_view> catalog_pages();
	// A choice value's readable name ("pair" -> "Both eyes per frame"); the value itself when unknown.
	std::string choice_label(std::string_view name, std::string_view value);
}
