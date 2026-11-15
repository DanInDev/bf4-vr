#include "body/pose_tweaks.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace bfvr::body::poses
{
	namespace
	{
		constexpr float radians = 3.14159265f / 180.0f;

		// The built-in tweaks, in the bfvr_poses.ini format. Paste a saved file's grip lines here to make
		// them everyone's default.
		constexpr std::string_view builtin = R"(
)";

		constexpr std::string_view names[grip_count] = {
			"relaxed", "reach", "fist", "cup", "palm", "magazine_ak", "magazine_ar", "hook", "pinch", "underhand",
		};

		float limit(float value, float bound)
		{
			return std::isfinite(value) ? std::clamp(value, -bound, bound) : 0.0f;
		}

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

		// Two numbers separated by blanks, nothing else.
		bool pair_of(std::string_view text, float& a, float& b)
		{
			const std::string copy(trim(text));
			const char* at = copy.c_str();
			char* end = nullptr;
			a = std::strtof(at, &end);
			if (end == at)
			{
				return false;
			}
			at = end;
			b = std::strtof(at, &end);
			if (end == at)
			{
				return false;
			}
			return trim(std::string_view(end)).empty() && std::isfinite(a) && std::isfinite(b);
		}

		ik::quat about(float x, float y, float z, float degrees)
		{
			const float half = 0.5f * degrees * radians;
			const float s = std::sin(half);
			return { x * s, y * s, z * s, std::cos(half) };
		}

		float step_of(float seconds, float time_constant)
		{
			return time_constant > 0.0f ? 1.0f - std::exp(-std::max(seconds, 0.0f) / time_constant) : 1.0f;
		}
	}

	std::string_view name_of(grip which)
	{
		const int index = static_cast<int>(which);
		return index >= 0 && index < grip_count ? names[index] : std::string_view("none");
	}

	grip grip_named(std::string_view name)
	{
		for (int index = 0; index < grip_count; ++index)
		{
			if (names[index] == name)
			{
				return static_cast<grip>(index);
			}
		}
		return grip::none;
	}

	bool tweak::empty() const
	{
		for (const finger_tweak& finger : fingers)
		{
			if (finger.bend != 0.0f || finger.side != 0.0f)
			{
				return false;
			}
		}
		return thumb_across == 0.0f && thumb_out == 0.0f;
	}

	tweak clamped(const tweak& value)
	{
		tweak out;
		for (int finger = 0; finger < fingers::count; ++finger)
		{
			out.fingers[finger].bend = limit(value.fingers[finger].bend, bend_limit);
			out.fingers[finger].side = limit(value.fingers[finger].side, side_limit);
		}
		out.thumb_across = limit(value.thumb_across, thumb_base_limit);
		out.thumb_out = limit(value.thumb_out, thumb_base_limit);
		return out;
	}

	std::vector<std::string> parse(std::string_view text, table& into)
	{
		std::vector<std::string> warnings;
		int line_number = 0;
		while (!text.empty())
		{
			++line_number;
			const auto newline = text.find('\n');
			std::string_view line = text.substr(0, newline);
			text = newline == std::string_view::npos ? std::string_view() : text.substr(newline + 1);
			const auto comment = line.find_first_of("#;");
			line = trim(line.substr(0, comment));
			if (line.empty())
			{
				continue;
			}
			const auto equals = line.find('=');
			const std::string where = "line " + std::to_string(line_number) + ": ";
			if (equals == std::string_view::npos)
			{
				warnings.push_back(where + "no '='");
				continue;
			}
			const std::string_view name = trim(line.substr(0, equals));
			const grip which = grip_named(name);
			if (which == grip::none)
			{
				warnings.push_back(where + "unknown grip '" + std::string(name) + "'");
				continue;
			}
			std::string_view rest = line.substr(equals + 1);
			float values[fingers::count + 1][2]{};
			int groups = 0;
			bool ok = true;
			while (ok)
			{
				const auto comma = rest.find(',');
				if (groups > fingers::count || !pair_of(rest.substr(0, comma), values[groups][0], values[groups][1]))
				{
					ok = false;
					break;
				}
				++groups;
				if (comma == std::string_view::npos)
				{
					break;
				}
				rest = rest.substr(comma + 1);
			}
			if (!ok || groups != fingers::count + 1)
			{
				warnings.push_back(where + std::string(name) + ": needs six pairs of degrees (thumb, index, middle, ring, pinky: bend side; thumb base: across out)");
				continue;
			}
			tweak value;
			for (int finger = 0; finger < fingers::count; ++finger)
			{
				value.fingers[finger] = { values[finger][0], values[finger][1] };
			}
			value.thumb_across = values[fingers::count][0];
			value.thumb_out = values[fingers::count][1];
			into[static_cast<int>(which)] = clamped(value);
		}
		return warnings;
	}

	std::string format(const table& values)
	{
		std::string text =
			"# bfvr_poses.ini: finger tweaks per grip, degrees, on top of what the grip draws (pose editor, panel page Skeleton).\n"
			"# <grip> = <thumb bend side>, <index>, <middle>, <ring>, <pinky>, <thumb base: across out>\n"
			"# bend: + closes (each bending joint); side: + towards the thumb; across: + folds the thumb over the palm; out: + opens it away from the index.\n";
		char line[256];
		for (int index = 0; index < grip_count; ++index)
		{
			const tweak& t = values[index];
			std::snprintf(line, sizeof(line), "%s = %g %g, %g %g, %g %g, %g %g, %g %g, %g %g\n", names[index].data(), t.fingers[0].bend, t.fingers[0].side,
				t.fingers[1].bend, t.fingers[1].side, t.fingers[2].bend, t.fingers[2].side, t.fingers[3].bend, t.fingers[3].side, t.fingers[4].bend,
				t.fingers[4].side, t.thumb_across, t.thumb_out);
			text += line;
		}
		return text;
	}

	std::string_view builtin_text()
	{
		return builtin;
	}

	const table& defaults()
	{
		static const table values = [] {
			table parsed{};
			parse(builtin, parsed);
			return parsed;
		}();
		return values;
	}

	ik::quat tweaked(ik::quat rotation, fingers::finger which, int joint, const tweak& value, bool right)
	{
		if (which < 0 || which >= fingers::count || joint < 0 || joint >= fingers::joints)
		{
			return rotation;
		}
		// The thumb's third joint (Thumb4, the tip end) does not bend.
		const bool bends = which != fingers::thumb || joint < 2;
		const finger_tweak& finger = value.fingers[which];
		const float bend = bends ? finger.bend : 0.0f;
		const float side = joint == 0 ? (right ? -finger.side : finger.side) : 0.0f;
		if (bend == 0.0f && side == 0.0f)
		{
			return rotation;
		}
		// rotation = swing * twist(z): the side turn goes between them (before the bend, about the
		// joint's own y), the bend is added to the twist.
		const float current = fingers::bend_of(rotation);
		const ik::quat swing = rotation * ik::quat{ 0.0f, 0.0f, std::sin(-0.5f * current), std::cos(-0.5f * current) };
		const float total = current + bend * radians;
		return ik::normalize(swing * about(0, 1, 0, side) * ik::quat{ 0.0f, 0.0f, std::sin(0.5f * total), std::cos(0.5f * total) });
	}

	ik::quat tweaked_thumb_base(ik::quat rotation, const tweak& value, bool right)
	{
		if (value.thumb_across == 0.0f && value.thumb_out == 0.0f)
		{
			return rotation;
		}
		// Mirrored hands: a rotation about z keeps its sign, one about y flips (fingers::mirrored_hand).
		return ik::normalize(rotation * about(0, 0, 1, value.thumb_across) * about(0, 1, 0, right ? -value.thumb_out : value.thumb_out));
	}

	void ease(tweak& current, const tweak& target, float seconds, float time_constant)
	{
		const float step = step_of(seconds, time_constant);
		const auto move = [step](float& from, float to) {
			from += (to - from) * step;
			if (std::fabs(to - from) < 0.01f)
			{
				from = to;
			}
		};
		for (int finger = 0; finger < fingers::count; ++finger)
		{
			move(current.fingers[finger].bend, target.fingers[finger].bend);
			move(current.fingers[finger].side, target.fingers[finger].side);
		}
		move(current.thumb_across, target.thumb_across);
		move(current.thumb_out, target.thumb_out);
	}
}
