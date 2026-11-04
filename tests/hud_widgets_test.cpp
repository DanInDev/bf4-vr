// Checks the HUD widget table (src/ui/hud_widget_table): every class maps to an element, each element
// has a boolean live setting with the table's default, vtables are unique, and the hidden mask.
#include <cstdio>
#include <set>
#include <vector>

#include "common/settings_store.hpp"
#include "ui/hud_widget_table.hpp"

using namespace bfvr;
using namespace bfvr::ui::hud_widgets;

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
}

int main()
{
	const int count = static_cast<int>(elements().size());
	expect(count > 0 && count <= 32, "elements fit the mask");

	std::set<std::uint64_t> vtables;
	std::vector<int> classes_per_element(static_cast<std::size_t>(count));
	for (const widget_class& entry : classes())
	{
		expect(entry.element >= 0 && entry.element < count, "class maps to an element");
		expect(vtables.insert(entry.vtable).second, "vtable listed once");
		expect(entry.vtable >= 0x140000000 && entry.vtable < 0x150000000, "vtable inside the game image");
		expect(element_of(entry.vtable) == entry.element, "lookup finds the class");
		if (entry.element >= 0 && entry.element < count)
		{
			++classes_per_element[static_cast<std::size_t>(entry.element)];
		}
	}
	for (int n : classes_per_element)
	{
		expect(n > 0, "every element hides at least one class");
	}
	expect(element_of(0x141D1BBF0) == -1, "UIWidgetEntity itself is not touched");
	expect(element_of(0) == -1, "null vtable");

	settings::load_global(); // no bfvr.ini beside the test: every setting at its default
	const settings::registry& store = settings::global();
	for (const element& e : elements())
	{
		const auto definition = store.definition_of(e.setting);
		expect(definition && definition->type == settings::kind::boolean, "element setting is a boolean");
		if (definition)
		{
			expect(store.boolean(e.setting, !e.shown_by_default) == e.shown_by_default, "registry default matches the table");
		}
	}

	bool shown[32]{};
	for (int i = 0; i < count; ++i)
	{
		shown[i] = true;
	}
	expect(hidden_mask({ shown, static_cast<std::size_t>(count) }) == 0, "all shown: nothing hidden");
	shown[0] = false;
	shown[count - 1] = false;
	expect(hidden_mask({ shown, static_cast<std::size_t>(count) }) == (1u | (1u << (count - 1))), "hidden bits by element index");

	if (failures == 0)
	{
		std::printf("hud_widgets: all passed\n");
	}
	return failures == 0 ? 0 : 1;
}
