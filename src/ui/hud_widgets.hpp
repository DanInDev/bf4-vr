#pragma once

#include <cstdint>
#include <string>

// Hides single elements of the game's HUD at their source (#28): a hook on the widgets' shared
// isVisible (ui/hud_widget_table.hpp) answers false for the classes of hidden elements, so the game
// neither updates nor draws them, on the monitor and on the HUD layer alike. BF4 only.
namespace bfvr::ui
{
	// Waits on a thread until the DRM has decrypted the function, checks it and every listed vtable,
	// then installs the hook. Does nothing on BF3.
	void install_hud_widgets();

	// Bit i = element i of hud_widgets::elements() hidden. Cheap; called when settings change.
	void set_hud_widgets_hidden(std::uint32_t mask);

	// Pipe `hudwidgets`: hook state, mask, calls and hidden answers since the last report.
	std::string hud_widgets_report();
}
