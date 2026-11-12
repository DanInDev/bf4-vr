#pragma once

#include <string_view>
#include <vector>

// Our own vector art (assets/ui/*.svg) for the panels (#50): reads the subset those files keep to,
// <path fill="#ffffff|#000000" d="..."> with absolute M, L, C, Q and Z, curves flattened to lines, in
// document order. White is the shape, black a cut line. Engine-free; tested by
// tests/calibration_screen_test.cpp.
namespace bfvr::ui
{
	struct vector_shape
	{
		bool cut{};             // fill #000000: drawn in the backdrop colour
		std::vector<float> xy;  // x0, y0, x1, y1, ... in viewBox units
	};

	struct vector_art
	{
		float width{};  // the viewBox
		float height{};
		std::vector<vector_shape> shapes;
		bool empty() const { return shapes.empty(); }
	};

	vector_art parse_vector_art(std::string_view svg);

	// assets/ui/soldier-tpose.svg, compiled in (CMakeLists.txt); parsed once.
	const vector_art& soldier_tpose();
}
