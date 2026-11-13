#include "ui/vector_art.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <string>

namespace bfvr::ui
{
	// Generated from assets/ui/soldier-tpose.svg (CMakeLists.txt).
	extern const char soldier_tpose_svg[];

	namespace
	{
		constexpr int curve_steps = 10;

		// The value of name="..." inside one element, or empty.
		std::string_view attribute(std::string_view element, std::string_view name)
		{
			const std::string key = " " + std::string(name) + "=\"";
			const std::size_t at = element.find(key);
			if (at == std::string_view::npos)
			{
				return {};
			}
			const std::size_t begin = at + key.size();
			const std::size_t end = element.find('"', begin);
			return end == std::string_view::npos ? std::string_view{} : element.substr(begin, end - begin);
		}

		struct path_reader
		{
			std::string_view d;
			std::size_t at{};

			void skip()
			{
				while (at < d.size() && (std::isspace(static_cast<unsigned char>(d[at])) || d[at] == ','))
				{
					++at;
				}
			}
			bool command(char& out)
			{
				skip();
				if (at < d.size() && std::isalpha(static_cast<unsigned char>(d[at])))
				{
					out = d[at++];
					return true;
				}
				return false;
			}
			bool number(float& out)
			{
				skip();
				if (at >= d.size())
				{
					return false;
				}
				const std::string text(d.substr(at, std::min<std::size_t>(32, d.size() - at)));
				char* end = nullptr;
				out = std::strtof(text.c_str(), &end);
				if (end == text.c_str())
				{
					return false;
				}
				at += static_cast<std::size_t>(end - text.c_str());
				return true;
			}
		};

		// Flattened outline of one path; false when it uses something outside the subset.
		bool read_path(std::string_view d, std::vector<float>& xy)
		{
			path_reader reader{ d };
			char op = 0;
			float x = 0.0f, y = 0.0f;
			while (true)
			{
				reader.skip();
				if (reader.at >= d.size())
				{
					break;
				}
				// A new command letter, or more numbers for the last one (an implicit repeat).
				reader.command(op);
				switch (op)
				{
				case 'M':
				case 'L':
					if (!reader.number(x) || !reader.number(y))
					{
						return false;
					}
					xy.push_back(x);
					xy.push_back(y);
					if (op == 'M')
					{
						op = 'L'; // further pairs after M are lines
					}
					break;
				case 'C':
				case 'Q':
				{
					float p[6]{};
					const int count = op == 'C' ? 6 : 4;
					for (int i = 0; i < count; ++i)
					{
						if (!reader.number(p[i]))
						{
							return false;
						}
					}
					for (int step = 1; step <= curve_steps; ++step)
					{
						const float t = static_cast<float>(step) / curve_steps, u = 1.0f - t;
						if (op == 'C')
						{
							xy.push_back(u * u * u * x + 3 * u * u * t * p[0] + 3 * u * t * t * p[2] + t * t * t * p[4]);
							xy.push_back(u * u * u * y + 3 * u * u * t * p[1] + 3 * u * t * t * p[3] + t * t * t * p[5]);
						}
						else
						{
							xy.push_back(u * u * x + 2 * u * t * p[0] + t * t * p[2]);
							xy.push_back(u * u * y + 2 * u * t * p[1] + t * t * p[3]);
						}
					}
					x = p[count - 2];
					y = p[count - 1];
					break;
				}
				case 'Z':
					return xy.size() >= 6;
				default:
					return false;
				}
			}
			return xy.size() >= 6;
		}
	}

	vector_art parse_vector_art(std::string_view svg)
	{
		vector_art art;
		const std::size_t root = svg.find("<svg");
		if (root == std::string_view::npos)
		{
			return art;
		}
		const std::string_view box = attribute(svg.substr(root, svg.find('>', root) - root), "viewBox");
		float values[4]{};
		path_reader reader{ box };
		for (float& value : values)
		{
			if (!reader.number(value))
			{
				return art;
			}
		}
		art.width = values[2];
		art.height = values[3];
		for (std::size_t at = svg.find("<path", root); at != std::string_view::npos; at = svg.find("<path", at + 5))
		{
			const std::size_t end = svg.find('>', at);
			if (end == std::string_view::npos)
			{
				break;
			}
			const std::string_view element = svg.substr(at, end - at);
			const std::string_view fill = attribute(element, "fill");
			vector_shape shape;
			if (fill == "#000000")
			{
				shape.cut = true;
			}
			else if (fill != "#ffffff")
			{
				continue;
			}
			if (read_path(attribute(element, "d"), shape.xy))
			{
				art.shapes.push_back(std::move(shape));
			}
		}
		return art;
	}

	const vector_art& soldier_tpose()
	{
		static const vector_art art = parse_vector_art(soldier_tpose_svg);
		return art;
	}
}
