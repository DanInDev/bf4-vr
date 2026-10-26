#include "launcher/ini_file.hpp"

#include <windows.h>

#include <algorithm>
#include <cctype>
#include <fstream>

namespace bfvr::launcher::ini
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

		std::vector<std::string> read_lines(const std::filesystem::path& file)
		{
			std::vector<std::string> lines;
			std::ifstream stream(file, std::ios::binary);
			std::string line;
			while (std::getline(stream, line))
			{
				if (!line.empty() && line.back() == '\r')
				{
					line.pop_back();
				}
				lines.push_back(line);
			}
			return lines;
		}

		// The key of a "key = value" line, lower case; empty for comments and other lines.
		std::string key_of(const std::string& line)
		{
			const std::string content = line.substr(0, line.find('#'));
			const auto equals = content.find('=');
			return equals == std::string::npos ? std::string() : lower(trim(content.substr(0, equals)));
		}
	}

	values read(const std::filesystem::path& file)
	{
		values result;
		for (const auto& line : read_lines(file))
		{
			const std::string key = key_of(line);
			if (!key.empty())
			{
				const std::string content = line.substr(0, line.find('#'));
				result[key] = trim(content.substr(content.find('=') + 1));
			}
		}
		return result;
	}

	bool get_bool(const values& settings, const char* key, bool fallback)
	{
		const auto found = settings.find(key);
		if (found == settings.end())
		{
			return fallback;
		}
		const std::string value = lower(found->second);
		return !(value == "0" || value == "false" || value == "off" || value == "no");
	}

	bool write(const std::filesystem::path& file, const changes& edits, const std::filesystem::path& seed)
	{
		std::error_code error;
		std::vector<std::string> lines = read_lines(std::filesystem::exists(file, error) ? file : seed);
		std::vector<bool> applied(edits.size());

		for (auto& line : lines)
		{
			const std::string key = key_of(line);
			for (std::size_t i = 0; i < edits.size() && !key.empty(); ++i)
			{
				if (key == lower(edits[i].first))
				{
					const auto comment = line.find('#');
					const std::string trailing = comment == std::string::npos ? std::string() : " " + line.substr(comment);
					line = edits[i].first + " = " + edits[i].second + trailing;
					applied[i] = true;
				}
			}
		}
		for (std::size_t i = 0; i < edits.size(); ++i)
		{
			if (!applied[i])
			{
				lines.push_back(edits[i].first + " = " + edits[i].second);
			}
		}

		// Written beside the target and swapped in, so a failed write never leaves half a file.
		auto temporary = file;
		temporary += L".tmp";
		{
			std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
			for (const auto& line : lines)
			{
				stream << line << "\r\n";
			}
			if (!stream.flush())
			{
				return false;
			}
		}
		if (!MoveFileExW(temporary.c_str(), file.c_str(), MOVEFILE_REPLACE_EXISTING))
		{
			std::filesystem::remove(temporary, error);
			return false;
		}
		return true;
	}
}
