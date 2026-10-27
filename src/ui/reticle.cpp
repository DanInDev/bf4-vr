#include "ui/reticle.hpp"

#include <algorithm>
#include <cmath>

#include "body/arm_ik.hpp"

namespace bfvr::ui
{
	void reticle_texture(int size, std::vector<std::uint32_t>& pixels)
	{
		pixels.assign(static_cast<std::size_t>(size) * static_cast<std::size_t>(size), 0u);
		const float half = 0.5f * static_cast<float>(size);
		for (int y = 0; y < size; ++y)
		{
			for (int x = 0; x < size; ++x)
			{
				const float dx = (static_cast<float>(x) + 0.5f - half) / half;
				const float dy = (static_cast<float>(y) + 0.5f - half) / half;
				const float r = std::sqrt(dx * dx + dy * dy);
				// Dot to 0.45, dark ring to 0.75, one-pixel-ish soft edges.
				const float edge = 1.5f / half;
				const float dot = std::clamp((0.45f - r) / edge + 0.5f, 0.0f, 1.0f);
				const float ring = std::clamp((0.75f - r) / edge + 0.5f, 0.0f, 1.0f);
				const float alpha = std::max(dot, ring * 0.8f);
				const float light = dot; // premultiplied: colour = brightness * alpha of the dot part
				const auto a = static_cast<std::uint32_t>(std::lround(alpha * 255.0f));
				const auto g = static_cast<std::uint32_t>(std::lround(light * 255.0f));
				const auto rb = static_cast<std::uint32_t>(std::lround(light * 80.0f));
				pixels[static_cast<std::size_t>(y) * static_cast<std::size_t>(size) + static_cast<std::size_t>(x)] = (a << 24) | (rb << 16) | (g << 8) | rb;
			}
		}
	}

	reticle_quad place_reticle(const float (&point)[3], const float (&eye)[3], float angular_size)
	{
		using body::ik::vec3;
		const vec3 at{ point[0], point[1], point[2] };
		const vec3 to_eye = vec3{ eye[0], eye[1], eye[2] } - at;
		const float distance = body::ik::length(to_eye);
		reticle_quad quad;
		quad.pose.position[0] = point[0];
		quad.pose.position[1] = point[1];
		quad.pose.position[2] = point[2];
		if (distance < 1e-4f)
		{
			return quad;
		}
		// A round dot: the roll of the shortest rotation does not matter.
		const body::ik::quat facing = body::ik::from_to({ 0, 0, 1 }, to_eye * (1.0f / distance));
		quad.pose.orientation[0] = facing.x;
		quad.pose.orientation[1] = facing.y;
		quad.pose.orientation[2] = facing.z;
		quad.pose.orientation[3] = facing.w;
		quad.size = 2.0f * distance * std::tan(0.5f * angular_size);
		return quad;
	}
}
