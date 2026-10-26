#pragma once

#include <filesystem>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace bfvr::launcher::ini
{
	// "key = value" lines, '#' starts a comment, keys are case-insensitive and the last one wins:
	// the same rules as bfvr::config, which reads the file inside the game.
	using values = std::map<std::string, std::string>;
	using changes = std::vector<std::pair<std::string, std::string>>;

	values read(const std::filesystem::path& file);
	bool get_bool(const values& settings, const char* key, bool fallback);

	// Applies `edits` in place, keeping comments, order and every other key; keys not yet in the
	// file are appended. A file that does not exist yet starts as a copy of `seed`, if that exists.
	bool write(const std::filesystem::path& file, const changes& edits, const std::filesystem::path& seed);
}
