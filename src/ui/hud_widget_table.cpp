#include "ui/hud_widget_table.hpp"

#include <iterator>

namespace bfvr::ui::hud_widgets
{
	namespace
	{
		enum : int { crosshair, hit_markers, damage, objectives, prompts, score };

		constexpr element element_table[] = {
			{ "hud_show_crosshair", false },
			{ "hud_show_hit_markers", true },
			{ "hud_show_damage", true },
			{ "hud_show_objectives", true },
			{ "hud_show_prompts", true },
			{ "hud_show_score", true },
		};

		// Vtables from tools/typeinfo_dump.py; each one's slot 30 is base_is_visible in the image
		// (checked again before the hook is installed).
		constexpr widget_class class_table[] = {
			{ 0x141C27240, "UICrosshairWidget", crosshair },
			{ 0x141C1E1D0, "UIHitIndicatorWidget", hit_markers },
			{ 0x141C1B730, "UIReceiveDamageIndicatorWidget", damage },
			{ 0x141C189E0, "UIObjectiveWidget", objectives },
			{ 0x141C18670, "UIObjectiveHintWidget", objectives },
			{ 0x141C1F210, "UIInteractionWidget", prompts },
			{ 0x141C1BF00, "UIScoringWidget", score },
			{ 0x141C15670, "UIAwardPresentationWidget", score },
		};
	}

	std::span<const element> elements()
	{
		return element_table;
	}

	std::span<const widget_class> classes()
	{
		return class_table;
	}

	int element_of(std::uint64_t vtable)
	{
		for (const widget_class& entry : class_table)
		{
			if (entry.vtable == vtable)
			{
				return entry.element;
			}
		}
		return -1;
	}

	std::uint32_t hidden_mask(std::span<const bool> shown)
	{
		std::uint32_t mask = 0;
		for (std::size_t i = 0; i < shown.size() && i < std::size(element_table); ++i)
		{
			if (!shown[i])
			{
				mask |= 1u << i;
			}
		}
		return mask;
	}
}
