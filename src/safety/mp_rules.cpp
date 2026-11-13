#include "safety/mp_rules.hpp"

#include <cctype>
#include <cstring>
#include <initializer_list>

namespace bfvr::safety
{
	namespace
	{
		std::string lower(std::string text)
		{
			for (char& c : text)
			{
				c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
			}
			return text;
		}

		bool starts_with(const std::string& text, const char* prefix)
		{
			return text.rfind(prefix, 0) == 0;
		}
	}

	game built_game()
	{
		return std::strcmp(BFVR_GAME_NAME, "bf3") == 0 ? game::bf3 : game::bf4;
	}

	const char* command_line_refusal(game target, const std::string& command_line)
	{
		const std::string line = lower(command_line);
		// Battlelog's requests to join, claim or match a multiplayer or co-op game (both games).
		for (const char* marker : { "state_connecttogameid", "state_claimreservation", "state_connecttouserid", "state_matchmakecoop",
				"state_createcooppeer", "-webmode mp", "-webmode coop" })
		{
			if (line.find(marker) != std::string::npos)
			{
				return "multiplayer or co-op command line";
			}
		}
		if (target == game::bf3)
		{
			// BF3 1147186: the campaign is started as "-webMode SP ... levelmode=\"sp\"" (bf3-vr re/notes/launch.md).
			const auto web_mode = line.find("-webmode sp");
			const auto after_mode = web_mode == std::string::npos ? web_mode : web_mode + std::strlen("-webmode sp");
			if (web_mode == std::string::npos || (after_mode < line.size() && line[after_mode] != ' '))
			{
				return "command line does not ask for single player (-webMode SP)";
			}
			// levelmode=\"sp\" inside the quoted request parameters, or levelmode="sp".
			if (const auto mode = line.find("levelmode="); mode != std::string::npos)
			{
				auto value = mode + std::strlen("levelmode=");
				while (value < line.size() && (line[value] == '\\' || line[value] == '"'))
				{
					++value;
				}
				const bool single = line.compare(value, 2, "sp") == 0
					&& (value + 2 >= line.size() || line[value + 2] == '\\' || line[value + 2] == '"' || line[value + 2] == ' ');
				if (!single)
				{
					return "level mode is not single player";
				}
			}
		}
		return nullptr;
	}

	level_kind judge_level(game target, const std::string& path)
	{
		const std::string level = lower(path);
		if (level.empty())
		{
			return level_kind::none;
		}
		if (level == "levels/frontend/frontend" || (target == game::bf3 && level == "levels/web_loading/web_loading"))
		{
			return level_kind::front_end;
		}
		// BF4: Levels/SP/SP_Tanker/... ; BF3: Levels/SP_Earthquake/SP_Earthquake.
		if (starts_with(level, target == game::bf3 ? "levels/sp_" : "levels/sp/"))
		{
			return level_kind::campaign;
		}
		return level_kind::forbidden;
	}
}
