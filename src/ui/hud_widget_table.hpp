#pragma once

#include <cstdint>
#include <span>

// Which of the game's HUD widget classes belong to which hud_show_<element> setting (#28). BF4 build
// 179547: every class here inherits UIWidgetEntity's isVisible (vtable slot 30, 0x140875D30), which the
// UI calls once per frame for each widget and which skips the widget when it returns false
// (re/notes/hud.md). Engine-free; tested by tests/hud_widgets_test.cpp.
namespace bfvr::ui::hud_widgets
{
	struct element
	{
		const char* setting;   // boolean live setting, true = shown
		bool shown_by_default;
	};

	struct widget_class
	{
		std::uint64_t vtable; // primary vtable of the runtime class (typeinfo dump)
		const char* name;
		int element;           // index into elements()
	};

	std::span<const element> elements();
	std::span<const widget_class> classes();
	std::span<const widget_class> screens(); // vtable = the owning screen's

	// The element a widget with this vtable belongs to, -1 for widgets the settings do not touch.
	int element_of(std::uint64_t vtable);

	// The element every widget of a screen with this vtable belongs to, -1 if none.
	int element_of_screen(std::uint64_t screen_vtable);

	// Bits of the elements that are matched by screen (worth the extra pointer read only then).
	std::uint32_t screen_elements();

	// The widget's owning screen (UIWidgetEntity; base isVisible dereferences it unconditionally).
	constexpr std::uint64_t widget_screen_offset = 0xA0;

	// Bit i set = element i hidden, from the per-element shown flags (same order as elements()).
	std::uint32_t hidden_mask(std::span<const bool> shown);

	constexpr std::uint64_t is_visible_slot = 30;
	constexpr std::uint64_t base_is_visible = 0x140875D30;
}
