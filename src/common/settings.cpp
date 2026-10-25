#include "common/settings.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <cstdio>

namespace bfvr::settings
{
	namespace
	{
		std::string_view trim(std::string_view text)
		{
			const auto first = text.find_first_not_of(" \t\r\n");
			if (first == std::string_view::npos)
			{
				return {};
			}
			const auto last = text.find_last_not_of(" \t\r\n");
			return text.substr(first, last - first + 1);
		}

		std::string lower(std::string_view text)
		{
			std::string result(text);
			std::transform(result.begin(), result.end(), result.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			return result;
		}

		std::vector<std::string_view> split(std::string_view text, char separator)
		{
			std::vector<std::string_view> parts;
			std::size_t start = 0;
			while (start <= text.size())
			{
				const auto end = std::min(text.find(separator, start), text.size());
				parts.push_back(text.substr(start, end - start));
				start = end + 1;
			}
			return parts;
		}

		std::vector<std::string_view> tokens(std::string_view text)
		{
			std::vector<std::string_view> result;
			std::size_t i = 0;
			while (i < text.size())
			{
				while (i < text.size() && (std::isspace(static_cast<unsigned char>(text[i])) || text[i] == ','))
				{
					++i;
				}
				const std::size_t start = i;
				while (i < text.size() && !std::isspace(static_cast<unsigned char>(text[i])) && text[i] != ',')
				{
					++i;
				}
				if (i > start)
				{
					result.push_back(text.substr(start, i - start));
				}
			}
			return result;
		}

		std::optional<float> to_float(std::string_view text)
		{
			if (!text.empty() && text.front() == '+')
			{
				text.remove_prefix(1);
			}
			float number{};
			const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), number);
			if (error != std::errc{} || end != text.data() + text.size() || !std::isfinite(number))
			{
				return std::nullopt;
			}
			return number;
		}

		std::optional<int> to_int(std::string_view text)
		{
			if (!text.empty() && text.front() == '+')
			{
				text.remove_prefix(1);
			}
			int number{};
			const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), number);
			if (error != std::errc{} || end != text.data() + text.size())
			{
				return std::nullopt;
			}
			return number;
		}

		std::string format_float(float number)
		{
			char buffer[32];
			std::snprintf(buffer, sizeof(buffer), "%.6g", number == 0.0f ? 0.0f : number);
			return buffer;
		}

		const char* kind_name(kind type)
		{
			switch (type)
			{
			case kind::boolean: return "bool";
			case kind::integer: return "int";
			case kind::real: return "float";
			case kind::choice: return "choice";
			case kind::vec3: return "vec3";
			}
			return "?";
		}

		int components(kind type)
		{
			return type == kind::vec3 ? 3 : 1;
		}

		bool numeric(kind type)
		{
			return type == kind::integer || type == kind::real || type == kind::vec3;
		}

		void canonicalise(const definition& setting, value& result)
		{
			switch (setting.type)
			{
			case kind::boolean:
				result.text = result.number[0] != 0 ? "1" : "0";
				break;
			case kind::integer:
				result.text = std::to_string(static_cast<int>(result.number[0]));
				break;
			case kind::real:
				result.text = format_float(result.number[0]);
				break;
			case kind::choice:
				result.text = std::string(split(setting.choices, '|')[static_cast<std::size_t>(result.number[0])]);
				break;
			case kind::vec3:
				result.text = format_float(result.number[0]) + " " + format_float(result.number[1]) + " " + format_float(result.number[2]);
				break;
			}
		}

		bool in_range(const definition& setting, const value& candidate)
		{
			if (!numeric(setting.type))
			{
				return true;
			}
			for (int i = 0; i < components(setting.type); ++i)
			{
				if (candidate.number[i] < setting.min || candidate.number[i] > setting.max)
				{
					return false;
				}
			}
			return true;
		}

		void clamp(const definition& setting, value& candidate)
		{
			for (int i = 0; numeric(setting.type) && i < components(setting.type); ++i)
			{
				candidate.number[i] = std::clamp(candidate.number[i], setting.min, setting.max);
			}
			canonicalise(setting, candidate);
		}

		std::string range_text(const definition& setting)
		{
			if (setting.type == kind::choice)
			{
				return setting.choices;
			}
			if (setting.type == kind::boolean)
			{
				return "0|1";
			}
			return format_float(setting.min) + ".." + format_float(setting.max);
		}

		float step_size(const definition& setting)
		{
			const float size = setting.step > 0 ? setting.step : (setting.max - setting.min) / 20.0f;
			return setting.type == kind::integer ? std::max(1.0f, std::round(size)) : size;
		}
	}

	std::optional<value> parse(const definition& setting, std::string_view text)
	{
		text = trim(text);
		value result;
		switch (setting.type)
		{
		case kind::boolean:
		{
			const std::string word = lower(text);
			if (word == "1" || word == "true" || word == "on" || word == "yes")
			{
				result.number[0] = 1;
			}
			else if (word == "0" || word == "false" || word == "off" || word == "no")
			{
				result.number[0] = 0;
			}
			else
			{
				return std::nullopt;
			}
			break;
		}
		case kind::integer:
		{
			const auto number = to_int(text);
			if (!number)
			{
				return std::nullopt;
			}
			result.number[0] = static_cast<float>(*number);
			break;
		}
		case kind::real:
		{
			const auto number = to_float(text);
			if (!number)
			{
				return std::nullopt;
			}
			result.number[0] = *number;
			break;
		}
		case kind::choice:
		{
			const std::string word = lower(text);
			const auto options = split(setting.choices, '|');
			const auto found = std::find_if(options.begin(), options.end(), [&](std::string_view option) { return lower(option) == word; });
			if (word.empty() || found == options.end())
			{
				return std::nullopt;
			}
			result.number[0] = static_cast<float>(found - options.begin());
			break;
		}
		case kind::vec3:
		{
			const auto parts = tokens(text);
			if (parts.size() != 3)
			{
				return std::nullopt;
			}
			for (int i = 0; i < 3; ++i)
			{
				const auto number = to_float(parts[static_cast<std::size_t>(i)]);
				if (!number)
				{
					return std::nullopt;
				}
				result.number[i] = *number;
			}
			break;
		}
		}
		canonicalise(setting, result);
		return result;
	}

	registry::entry* registry::find(std::string_view name)
	{
		const std::string key = lower(trim(name));
		const auto found = std::find_if(entries_.begin(), entries_.end(), [&](const entry& item) { return item.setting.name == key; });
		return found != entries_.end() ? &*found : nullptr;
	}

	const registry::entry* registry::find(std::string_view name) const
	{
		return const_cast<registry*>(this)->find(name);
	}

	bool registry::add(const definition& setting)
	{
		std::lock_guard lock(mutex_);
		if (setting.name.empty() || setting.name != lower(setting.name) || setting.name.find_first_of(" \t=#") != std::string::npos || find(setting.name))
		{
			return false;
		}
		if ((numeric(setting.type) && !(setting.min <= setting.max)) || (setting.type == kind::choice && setting.choices.empty()))
		{
			return false;
		}
		const auto initial = parse(setting, setting.fallback);
		if (!initial || !in_range(setting, *initial))
		{
			return false;
		}
		entries_.push_back({ setting, *initial, false });
		++revision_;
		return true;
	}

	std::vector<std::string> registry::load(const lookup_fn& lookup)
	{
		std::lock_guard lock(mutex_);
		std::vector<std::string> warnings;
		for (entry& item : entries_)
		{
			item.changed = false;
			item.current = *parse(item.setting, item.setting.fallback);
			const auto text = lookup(item.setting.name);
			if (!text)
			{
				continue;
			}
			auto loaded = parse(item.setting, *text);
			if (!loaded)
			{
				warnings.push_back(item.setting.name + ": '" + *text + "' is not a valid " + kind_name(item.setting.type) + " (" + range_text(item.setting) + "), using " + item.current.text);
				continue;
			}
			if (!in_range(item.setting, *loaded))
			{
				clamp(item.setting, *loaded);
				warnings.push_back(item.setting.name + ": '" + *text + "' is outside " + range_text(item.setting) + ", using " + loaded->text);
			}
			item.current = *loaded;
		}
		++revision_;
		return warnings;
	}

	bool registry::set(std::string_view name, std::string_view text, std::string* error)
	{
		std::lock_guard lock(mutex_);
		entry* item = find(name);
		const auto fail = [&](const std::string& why)
		{
			if (error)
			{
				*error = why;
			}
			return false;
		};
		if (!item)
		{
			return fail("unknown setting '" + std::string(trim(name)) + "'");
		}
		const auto parsed = parse(item->setting, text);
		if (!parsed)
		{
			return fail(item->setting.name + " takes a " + kind_name(item->setting.type) + " (" + range_text(item->setting) + ")");
		}
		if (!in_range(item->setting, *parsed))
		{
			return fail(item->setting.name + " must be within " + range_text(item->setting));
		}
		if (parsed->text != item->current.text)
		{
			++revision_;
		}
		item->current = *parsed;
		item->changed = true;
		return true;
	}

	bool registry::step(std::string_view name, int steps, std::string* error)
	{
		std::lock_guard lock(mutex_);
		entry* item = find(name);
		if (!item || item->setting.type == kind::vec3)
		{
			if (error)
			{
				*error = item ? item->setting.name + " is a vec3: use set" : "unknown setting '" + std::string(trim(name)) + "'";
			}
			return false;
		}
		value next = item->current;
		switch (item->setting.type)
		{
		case kind::boolean:
			if (steps % 2 != 0)
			{
				next.number[0] = next.number[0] != 0 ? 0.0f : 1.0f;
			}
			break;
		case kind::choice:
		{
			const int count = static_cast<int>(split(item->setting.choices, '|').size());
			next.number[0] = static_cast<float>(((static_cast<int>(next.number[0]) + steps) % count + count) % count);
			break;
		}
		default:
			next.number[0] += static_cast<float>(steps) * step_size(item->setting);
			if (item->setting.type == kind::integer)
			{
				next.number[0] = std::round(next.number[0]);
			}
			break;
		}
		clamp(item->setting, next);
		if (next.text != item->current.text)
		{
			++revision_;
		}
		item->current = next;
		item->changed = true;
		return true;
	}

	bool registry::reset(std::string_view name)
	{
		std::lock_guard lock(mutex_);
		entry* item = find(name);
		if (!item)
		{
			return false;
		}
		const value initial = *parse(item->setting, item->setting.fallback);
		if (initial.text != item->current.text)
		{
			++revision_;
		}
		item->current = initial;
		item->changed = true;
		return true;
	}

	std::optional<value> registry::get(std::string_view name) const
	{
		std::lock_guard lock(mutex_);
		const entry* item = find(name);
		return item ? std::optional<value>(item->current) : std::nullopt;
	}

	bool registry::boolean(std::string_view name, bool fallback) const
	{
		std::lock_guard lock(mutex_);
		const entry* item = find(name);
		return item && item->setting.type == kind::boolean ? item->current.number[0] != 0 : fallback;
	}

	int registry::integer(std::string_view name, int fallback) const
	{
		std::lock_guard lock(mutex_);
		const entry* item = find(name);
		return item && item->setting.type == kind::integer ? static_cast<int>(item->current.number[0]) : fallback;
	}

	float registry::real(std::string_view name, float fallback) const
	{
		std::lock_guard lock(mutex_);
		const entry* item = find(name);
		return item && item->setting.type == kind::real ? item->current.number[0] : fallback;
	}

	std::string registry::text(std::string_view name, std::string_view fallback) const
	{
		std::lock_guard lock(mutex_);
		const entry* item = find(name);
		return item ? item->current.text : std::string(fallback);
	}

	std::uint64_t registry::revision() const
	{
		std::lock_guard lock(mutex_);
		return revision_;
	}

	std::vector<std::string> registry::names() const
	{
		std::lock_guard lock(mutex_);
		std::vector<std::string> result;
		for (const entry& item : entries_)
		{
			result.push_back(item.setting.name);
		}
		return result;
	}

	bool registry::changed() const
	{
		std::lock_guard lock(mutex_);
		return std::any_of(entries_.begin(), entries_.end(), [](const entry& item) { return item.changed; });
	}

	std::string registry::describe(const entry& item) const
	{
		std::string line = item.setting.name + " = " + item.current.text + "  [" + kind_name(item.setting.type) + " " + range_text(item.setting)
			+ ", default " + parse(item.setting, item.setting.fallback)->text + "]";
		if (item.changed)
		{
			line += " *";
		}
		if (!item.setting.help.empty())
		{
			line += "  " + item.setting.help;
		}
		return line;
	}

	std::string registry::command(std::string_view line)
	{
		line = trim(line);
		const auto verb_end = std::min(line.find_first_of(" \t"), line.size());
		const std::string verb = lower(line.substr(0, verb_end));
		std::string_view rest = trim(line.substr(verb_end));
		const auto name_end = std::min(rest.find_first_of(" \t"), rest.size());
		const std::string_view name = rest.substr(0, name_end);
		const std::string_view argument = trim(rest.substr(name_end));

		const auto describe_one = [&](std::string_view key, const char* prefix) -> std::string
		{
			std::lock_guard lock(mutex_);
			const entry* item = find(key);
			return item ? prefix + describe(*item) : "error: unknown setting '" + std::string(key) + "'";
		};

		if (verb == "list")
		{
			std::lock_guard lock(mutex_);
			std::string text;
			for (const entry& item : entries_)
			{
				text += describe(item) + "\n";
			}
			return text.empty() ? "no settings\n" : text;
		}
		if (verb == "get" && !name.empty())
		{
			return describe_one(name, "");
		}
		std::string error;
		if (verb == "set" && !name.empty() && !argument.empty())
		{
			return set(name, argument, &error) ? describe_one(name, "ok ") : "error: " + error;
		}
		if (verb == "step" && !name.empty())
		{
			const auto steps = argument.empty() ? std::optional<int>(1) : to_int(argument);
			if (!steps)
			{
				return "error: step takes a whole number of steps";
			}
			return step(name, *steps, &error) ? describe_one(name, "ok ") : "error: " + error;
		}
		if (verb == "reset" && !name.empty())
		{
			return reset(name) ? describe_one(name, "ok ") : "error: unknown setting '" + std::string(name) + "'";
		}
		return "error: usage: list | get <name> | set <name> <value> | step <name> [n] | reset <name>";
	}

	std::string registry::merge_ini(std::string_view existing) const
	{
		std::lock_guard lock(mutex_);
		const std::string newline = existing.find("\r\n") != std::string_view::npos ? "\r\n" : "\n";
		const bool ends_with_newline = existing.empty() || existing.back() == '\n';

		std::vector<std::string> lines;
		for (std::string_view raw : split(existing, '\n'))
		{
			if (!raw.empty() && raw.back() == '\r')
			{
				raw.remove_suffix(1);
			}
			lines.emplace_back(raw);
		}
		if (ends_with_newline && !lines.empty())
		{
			lines.pop_back();
		}

		std::vector<bool> written(entries_.size());
		for (std::string& line : lines)
		{
			const auto hash = line.find('#');
			const std::string_view content = std::string_view(line).substr(0, hash);
			const auto equals = content.find('=');
			if (equals == std::string_view::npos)
			{
				continue;
			}
			const std::string key = lower(trim(content.substr(0, equals)));
			for (std::size_t i = 0; i < entries_.size(); ++i)
			{
				if (entries_[i].changed && entries_[i].setting.name == key)
				{
					const std::string indent = line.substr(0, line.find_first_not_of(" \t"));
					const std::string comment = hash != std::string::npos ? " " + line.substr(hash) : std::string{};
					line = indent + key + " = " + entries_[i].current.text + comment;
					written[i] = true;
				}
			}
		}
		for (std::size_t i = 0; i < entries_.size(); ++i)
		{
			if (entries_[i].changed && !written[i])
			{
				lines.push_back(entries_[i].setting.name + " = " + entries_[i].current.text);
			}
		}

		std::string result;
		for (std::size_t i = 0; i < lines.size(); ++i)
		{
			result += lines[i];
			if (i + 1 < lines.size() || ends_with_newline)
			{
				result += newline;
			}
		}
		return result;
	}

	void registry::mark_saved()
	{
		std::lock_guard lock(mutex_);
		for (entry& item : entries_)
		{
			item.changed = false;
		}
	}
}
