#pragma once

#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// Live-tunable settings (docs/tasks/vr-settings.md): one registry that knows each value's type,
// range and default, loads it from bfvr.ini, answers the pipe and the in-VR panel, and writes
// changed values back to the ini. Engine-free; tested by tests/settings_test.cpp.
namespace bfvr::settings
{
	enum class kind
	{
		boolean,
		integer,
		real,
		choice, // one of definition::choices, stored as its text and its index in number[0]
		vec3,   // three reals, each within [min, max]
	};

	struct definition
	{
		std::string name;      // ini key, lower case
		kind type{ kind::real };
		float min{};           // integer, real, vec3
		float max{};
		float step{};          // integer, real: amount per step() (0 = a twentieth of the range)
		std::string fallback;  // default, as ini text
		std::string choices;   // choice: "aer|pair"
		std::string help;      // one line for list and the panel
	};

	struct value
	{
		float number[3]{};     // boolean 0/1, integer, real, choice index, vec3
		std::string text;      // canonical ini text
	};

	// Thread-safe; values are read per frame by game-side code and written by the pipe/panel.
	class registry
	{
	public:
		// false (nothing added) when the name is taken or the definition or its default is invalid.
		bool add(const definition& setting);

		// Takes each setting's text from lookup (e.g. config::get), or keeps the default when it
		// returns nothing. Out-of-range numbers are clamped, unparsable text keeps the default;
		// each case adds a warning. Clears the changed marks.
		using lookup_fn = std::function<std::optional<std::string>(const std::string& name)>;
		std::vector<std::string> load(const lookup_fn& lookup);

		// Rejects unknown names, unparsable text and out-of-range numbers (error says why).
		bool set(std::string_view name, std::string_view text, std::string* error = nullptr);
		// Panel buttons: booleans toggle, choices cycle, numbers move by steps * step, clamped.
		// Not defined for vec3.
		bool step(std::string_view name, int steps, std::string* error = nullptr);
		bool reset(std::string_view name);

		std::optional<value> get(std::string_view name) const;
		// Typed reads; fallback when the name is unknown or of another kind.
		bool boolean(std::string_view name, bool fallback = false) const;
		int integer(std::string_view name, int fallback = 0) const;
		float real(std::string_view name, float fallback = 0) const;
		std::string text(std::string_view name, std::string_view fallback = {}) const;

		// Increments on every change that alters a value; consumers re-read when it moved.
		std::uint64_t revision() const;
		std::vector<std::string> names() const;
		bool changed() const;

		// Pipe protocol, one line in, text out:
		//   list | get <name> | set <name> <value> | step <name> <+n|-n> | reset <name>
		// Errors start with "error:".
		std::string command(std::string_view line);

		// The ini text with every setting changed since load() written back: existing
		// "key = value" lines are rewritten in place (trailing comment kept), missing keys are
		// appended. All other lines, comments and line endings are kept.
		std::string merge_ini(std::string_view existing) const;
		// Call after the merged text was written.
		void mark_saved();

	private:
		struct entry
		{
			definition setting;
			value current;
			bool changed{};
		};

		entry* find(std::string_view name);
		const entry* find(std::string_view name) const;
		std::string describe(const entry& item) const;

		mutable std::mutex mutex_;
		std::vector<entry> entries_;
		std::uint64_t revision_{};
	};

	// Parses text as a value of setting's kind without range checks; nullopt when it does not parse.
	std::optional<value> parse(const definition& setting, std::string_view text);
}
