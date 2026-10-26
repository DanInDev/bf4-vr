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
			{ "stereo_render", kind::choice, 0, 0, 0, "aer", "aer|pair", "alternate eyes, or both eyes from one game frame" },
			{ "arm_ik", kind::boolean, 0, 0, 0, "1", "", "first-person arms follow the VR controllers" },
			{ "right_stick", kind::choice, 0, 0, 0, "look", "look|turn", "right stick aims, or turns and changes stance" },
			{ "turn", kind::choice, 0, 0, 0, "snap", "snap|smooth|off", "right stick turn style (right_stick = turn)" },
			{ "snap_turn_degrees", kind::integer, 5, 90, 5, "30", "", "angle per snap turn" },
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
