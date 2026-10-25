#include "common/config.hpp"

#include <windows.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <map>
#include <mutex>

namespace bfvr::config
{
	namespace
	{
		std::string trim(const std::string& text)
		{
			const auto first = text.find_first_not_of(" \t\r\n");
			if (first == std::string::npos)
			{
				return {};
			}
			const auto last = text.find_last_not_of(" \t\r\n");
			return text.substr(first, last - first + 1);
		}

		std::string lower(std::string text)
		{
			std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			return text;
		}

		const std::map<std::string, std::string>& file_values()
		{
			static std::map<std::string, std::string> values;
			static std::once_flag loaded;
			std::call_once(loaded, []
			{
				wchar_t path[MAX_PATH]{};
				const DWORD length = GetModuleFileNameW(nullptr, path, MAX_PATH);
				wchar_t* separator = length && length < MAX_PATH ? wcsrchr(path, L'\\') : nullptr;
				if (!separator || wcscpy_s(separator + 1, MAX_PATH - (separator + 1 - path), L"bfvr.ini") != 0)
				{
					return;
				}

				std::ifstream file(path);
				std::string line;
				while (std::getline(file, line))
				{
					line = trim(line.substr(0, line.find('#')));
					const auto equals = line.find('=');
					if (equals != std::string::npos)
					{
						values[lower(trim(line.substr(0, equals)))] = trim(line.substr(equals + 1));
					}
				}
			});
			return values;
		}
	}

	std::string get(const char* key, const char* fallback)
	{
		std::string variable = "BFVR_" + std::string(key);
		std::transform(variable.begin(), variable.end(), variable.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });

		char buffer[1024]{};
		std::size_t length{};
		if (getenv_s(&length, buffer, variable.c_str()) == 0 && length > 1)
		{
			return buffer;
		}

		const auto& values = file_values();
		const auto found = values.find(lower(key));
		return found != values.end() ? found->second : fallback;
	}

	bool get_bool(const char* key, bool fallback)
	{
		const std::string value = lower(get(key, fallback ? "1" : "0"));
		return !(value == "0" || value == "false" || value == "off" || value == "no");
	}

	bool research_enabled()
	{
#if defined(NDEBUG)
		static const bool enabled = get_bool("research", false);
#else
		static const bool enabled = get_bool("research", true);
#endif
		return enabled;
	}
}
