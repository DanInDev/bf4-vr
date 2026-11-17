#pragma once

#include <map>
#include <string>
#include <string_view>
#include <vector>

// Per-gun tuning of the reload grab zones (owner 2026-11-17: "x, y, z and size for every gun, to get the feeling
// just right"): each zone of a gun moved along the gun and scaled, set on the in-VR panel (Weapon > Tune grab zones)
// with the gun in hand and kept in bfvr_zones.ini beside bfvr.ini. Engine-free; tests/zone_tuning_test.cpp.
namespace bfvr::weapon::zone_tuning
{
	enum class zone
	{
		handle,   // the charging handle or bolt (a pump gun: the fore-end it pumps)
		magazine, // where the seated magazine is grabbed (a tube gun: the loading port)
		lid,      // a belt-fed gun's lid, a revolver's frame
		count,
	};
	constexpr int zone_count = static_cast<int>(zone::count);
	const char* name_of(zone which);
	zone zone_named(std::string_view name); // zone::count when unknown

	// Seen from behind the gun: centimetres to the right, up and forward along the barrel; size multiplies the
	// zone's reach (1 = as built).
	struct tune
	{
		float right{};
		float up{};
		float forward{};
		float size{ 1.0f };
	};
	constexpr float max_shift_cm = 15.0f;
	constexpr float min_size = 0.3f;
	constexpr float max_size = 3.0f;
	tune clamped(tune value);
	bool is_default(const tune& value);

	// In the gun frame (Wep_Root: +x the gun's left, +y up, +z forward), metres.
	struct shift
	{
		float x{}, y{}, z{};
	};
	shift in_gun_frame(const tune& value);

	struct gun_tunes
	{
		tune zones[zone_count];
	};
	using book = std::map<std::string, gun_tunes>; // by the gun's unlock name ("U_AK12")

	// bfvr_zones.ini: one line per tuned zone, "<gun> <zone> <right> <up> <forward> <size>", '#' comments. Lines it
	// cannot use are skipped with a warning each; the rest is read.
	std::vector<std::string> parse(std::string_view text, book& out);
	// Only the zones that differ from the default, guns in name order.
	std::string format(const book& tunes);
}
