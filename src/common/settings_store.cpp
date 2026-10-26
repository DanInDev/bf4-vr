#include "common/settings_store.hpp"

#include <windows.h>

#include <fstream>
#include <iterator>
#include <sstream>

#include "common/config.hpp"
#include "common/log.hpp"

namespace bfvr::settings
{
	namespace
	{
		// Live tunables. Start-up-only keys (render_size, xr_runtime, stereo, hud_layer, research) stay
		// in config: they cannot change while the game runs.
		const definition definitions[] = {
			{ "gun_scale", kind::real, 0.25f, 4.0f, 0.05f, "1", "", "apparent gun and arms size, 1 = the game's" },
			{ "view_crop_h", kind::real, 0.4f, 1.0f, 0.02f, "1", "", "rendered part of the headset FOV, horizontal" },
			{ "view_crop_v", kind::real, 0.4f, 1.0f, 0.02f, "1", "", "rendered part of the headset FOV, vertical" },
			{ "ipd_scale", kind::real, 0.5f, 2.0f, 0.05f, "1", "", "eye distance multiplier (world scale), 1 = the runtime's IPD" },
			{ "hud_distance", kind::real, 0.3f, 20.0f, 0.1f, "1.5", "", "HUD layer distance, metres" },
			{ "hud_width", kind::real, 0.2f, 10.0f, 0.05f, "1.3", "", "HUD layer width, metres" },
			{ "hud_mode", kind::choice, 0, 0, 0, "body", "head|body", "HUD head-locked, or projected (markers on targets) with minimap and ammo on the body" },
			{ "hud_marker_distance", kind::real, 2.0f, 50.0f, 1.0f, "10", "", "hud_mode = body: distance of the projected HUD, metres" },
			{ "hud_watch_width", kind::real, 0.05f, 0.3f, 0.01f, "0.12", "", "hud_mode = body: minimap width on the forearm, metres" },
			{ "hud_ammo_width", kind::real, 0.05f, 0.3f, 0.01f, "0.1", "", "hud_mode = body: ammo counter width beside the gun, metres" },
			{ "hud_show_main", kind::boolean, 0, 0, 0, "1", "", "HUD: crosshair, markers, objective text" },
			{ "hud_show_minimap", kind::boolean, 0, 0, 0, "1", "", "HUD: minimap" },
			{ "hud_show_ammo", kind::boolean, 0, 0, 0, "1", "", "HUD: ammo and grenade counter" },
			{ "hud_show_bottom", kind::boolean, 0, 0, 0, "1", "", "HUD: weapon and stance icons at the bottom" },
			{ "stereo_render", kind::choice, 0, 0, 0, "aer", "aer|pair", "alternate eyes, or both eyes from one game frame" },
			{ "shoulder_offset", kind::vec3, -0.3f, 0.3f, 0.0f, "0 0 0", "", "shoulders relative to the head, metres (x right, y up, z back)" },
			{ "arm_ik", kind::boolean, 0, 0, 0, "1", "", "first-person arms follow the VR controllers" },
			{ "right_stick", kind::choice, 0, 0, 0, "turn", "look|turn", "right stick aims, or turns and changes stance" },
			{ "turn", kind::choice, 0, 0, 0, "snap", "snap|smooth|off", "right stick turn style (right_stick = turn)" },
			{ "snap_turn_degrees", kind::integer, 5, 90, 5, "30", "", "angle per snap turn" },
			{ "dominant_hand", kind::choice, 0, 0, 0, "right", "right|left", "gun hand: left mirrors the pad and swaps the hand roles" },
			{ "reload", kind::choice, 0, 0, 0, "physical", "button|physical", "reload: the game's button, or by hand (AK: pull the magazine, AR: B releases it; chest pouch)" },
			{ "grips_to_game", kind::boolean, 0, 0, 0, "0", "", "controller grips also reach the game as LB/RB (grenade, fire in some layouts); off: they are the mod's" },
			{ "x_menu", kind::boolean, 0, 0, 0, "1", "", "left X opens the game's menu (Start); the right B reloads" },
			{ "ads_trigger", kind::boolean, 0, 0, 0, "0", "", "left trigger = the game's aim down sights (zooms the view)" },
			{ "hud_show_crosshair", kind::boolean, 0, 0, 0, "0", "", "HUD: the game's crosshair in the middle" },
			{ "arm_length", kind::real, 0.8f, 1.5f, 0.05f, "1", "", "first-person arms stretched by this (reach)" },
			{ "shoulder_width", kind::real, -0.1f, 0.2f, 0.01f, "0", "", "first-person shoulders moved outwards by this, metres per side" },
			{ "holsters", kind::boolean, 0, 0, 0, "1", "", "weapon holsters on the body: gun hand + grip over the shoulder (primary), at the hip (secondary), chest (knife), belt (gadgets)" },
			{ "squad_signal", kind::boolean, 0, 0, 0, "1", "", "squad order (Q): raise the off hand ahead above your eyes and squeeze the grip" },
			{ "grenade", kind::choice, 0, 0, 0, "physical", "button|physical", "grenades: the game's button (LB), or by hand (belt, pin, throw)" },
			{ "grenade_throw_scale", kind::real, 0.5f, 4.0f, 0.1f, "1.5", "", "physical grenades: launch speed = hand speed times this" },
			{ "drop_discards_ammo", kind::boolean, 0, 0, 0, "1", "", "physical reload: a dropped magazine loses its rounds (off: they go back to the reserve)" },
			{ "accessory_touch", kind::boolean, 0, 0, 0, "1", "", "off hand on the optic or front rail + grip toggles it (magnifier, zoom, light, laser: D-pad up)" },
			{ "menu_input", kind::choice, 0, 0, 0, "pointer", "pointer|pad", "menus: point with a controller and pull the trigger, or the pad only" },
			{ "aim", kind::choice, 0, 0, 0, "head", "head|controller", "what aims the gun: the view (game default) or the right controller" },
			{ "neutral_pitch", kind::boolean, 0, 0, 0, "0", "", "hold the game's aim pitch at 0: only the head pitches the view (level horizon)" },
			{ "stance_flicks", kind::boolean, 0, 0, 0, "1", "", "right stick up/down changes stance (right_stick = turn)" },
		};

		registry& instance()
		{
			static registry store;
			return store;
		}
	}

	registry& global()
	{
		return instance();
	}

	void load_global()
	{
		registry& store = instance();
		for (const definition& setting : definitions)
		{
			if (!store.add(setting))
			{
				log::write("settings: definition of %s rejected", setting.name.c_str());
			}
		}
		// A sentinel tells "missing" from "empty" (config::get has no presence query).
		constexpr const char* missing = "\x01";
		const auto warnings = store.load([&](const std::string& name) -> std::optional<std::string>
		{
			std::string text = config::get(name.c_str(), missing);
			return text == missing ? std::nullopt : std::optional<std::string>(std::move(text));
		});
		for (const std::string& warning : warnings)
		{
			log::write("settings: %s", warning.c_str());
		}
		log::write("settings: %zu live settings loaded", store.names().size());
	}

	std::string save_global()
	{
		const std::wstring path = config::ini_path();
		if (path.empty())
		{
			return "error: no bfvr.ini path";
		}
		std::string existing;
		{
			std::ifstream file(path, std::ios::binary);
			existing.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
		}
		const std::string merged = instance().merge_ini(existing);
		const std::wstring temporary = path + L".tmp";
		{
			std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
			file.write(merged.data(), static_cast<std::streamsize>(merged.size()));
			if (!file)
			{
				return "error: cannot write bfvr.ini.tmp";
			}
		}
		if (!MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING))
		{
			return "error: cannot replace bfvr.ini (" + std::to_string(GetLastError()) + ")";
		}
		instance().mark_saved();
		return {};
	}

	std::string pipe_command(std::string_view line)
	{
		if (line == "save")
		{
			const std::string error = save_global();
			return error.empty() ? "saved" : error;
		}
		return instance().command(line.empty() ? std::string_view("list") : line);
	}
}
