#pragma once

#include <d3d11.h>

#include <cstdint>
#include <string>

#include "stereo/scope.hpp"

namespace bfvr::stereo
{
	// Render thread, right after the scope render tonemapped into the back buffer: copies the lens
	// circle (crop of the height, aspect = the render's tan(h)/tan(v)) out of the middle and draws it
	// round, premultiplied, with a dot at the centre (the sight line) into the scope image.
	// The context's pipeline state is restored. False when the image could not be made.
	bool compose_scope(ID3D11DeviceContext* context, ID3D11Texture2D* back_buffer, float aspect, const scope_request& request);

	struct scope_image
	{
		ID3D11Texture2D* texture{}; // owned here; valid until the device goes away
		std::uint64_t frame{};      // moves on with every new image
		pose quad;                  // where the lens was when this image was planned
		float quad_size{};
		int eye{};                  // the eye behind the sight
		float lens_tan{};           // #139: the world angle (tangent) from the picture's centre to its disc edge
		float magnification{};      // #139: the power it was rendered at (the disc spans lens_tan * magnification of view)
		std::uint64_t at{};         // GetTickCount64 of the image
	};
	scope_image latest_scope_image();

	std::string scope_compose_report();
}
