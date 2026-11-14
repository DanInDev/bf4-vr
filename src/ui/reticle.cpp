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

	void sight_dot_texture(int size, std::vector<std::uint32_t>& pixels)
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
				// Core to 0.4, then a glow fading out by the edge.
				const float edge = 1.5f / half;
				const float core = std::clamp((0.4f - r) / edge + 0.5f, 0.0f, 1.0f);
				const float glow = std::clamp(1.0f - r, 0.0f, 1.0f);
				const float alpha = std::max(core, 0.35f * glow * glow);
				// Premultiplied: red, the middle of the core nearly white so it reads on a red or dark wall
				// (headset 2026-11-11: a plain red dot was not seen in the red room).
				const float hot = std::clamp((0.22f - r) / edge + 0.5f, 0.0f, 1.0f);
				const auto a = static_cast<std::uint32_t>(std::lround(alpha * 255.0f));
				const auto red = static_cast<std::uint32_t>(std::lround(alpha * 255.0f));
				const auto green = static_cast<std::uint32_t>(std::lround(core * 70.0f + hot * 150.0f));
				const auto blue = static_cast<std::uint32_t>(std::lround(core * 50.0f + hot * 150.0f));
				pixels[static_cast<std::size_t>(y) * static_cast<std::size_t>(size) + static_cast<std::size_t>(x)] = (a << 24) | (red << 16) | (green << 8) | blue;
			}
		}
	}

	void ring_texture(int size, bool hot, std::vector<std::uint32_t>& pixels)
	{
		pixels.assign(static_cast<std::size_t>(size) * static_cast<std::size_t>(size), 0u);
		const float half = 0.5f * static_cast<float>(size);
		// Premultiplied BGRA: amber when idle, green while a hand is inside.
		const float colour[3] = { hot ? 0.2f : 1.0f, hot ? 1.0f : 0.7f, hot ? 0.3f : 0.1f }; // r, g, b
		for (int y = 0; y < size; ++y)
		{
			for (int x = 0; x < size; ++x)
			{
				const float dx = (static_cast<float>(x) + 0.5f - half) / half;
				const float dy = (static_cast<float>(y) + 0.5f - half) / half;
				const float r = std::sqrt(dx * dx + dy * dy);
				const float edge = 1.5f / half;
				// A ring at the reach (0.9..1.0) and a faint fill, so the zone reads as a disc; the ring at 30 %
				// opacity (headset 2026-11-09: the full-strength rings got in the way).
				const float ring = std::clamp((r - 0.88f) / edge + 0.5f, 0.0f, 1.0f) * std::clamp((0.98f - r) / edge + 0.5f, 0.0f, 1.0f);
				const float fill = r < 0.9f ? 0.04f : 0.0f;
				const float alpha = std::max(ring * 0.3f, fill);
				const auto a = static_cast<std::uint32_t>(std::lround(alpha * 255.0f));
				const auto red = static_cast<std::uint32_t>(std::lround(colour[0] * alpha * 255.0f));
				const auto green = static_cast<std::uint32_t>(std::lround(colour[1] * alpha * 255.0f));
				const auto blue = static_cast<std::uint32_t>(std::lround(colour[2] * alpha * 255.0f));
				pixels[static_cast<std::size_t>(y) * static_cast<std::size_t>(size) + static_cast<std::size_t>(x)] = (a << 24) | (red << 16) | (green << 8) | blue;
			}
		}
	}

	void stow_texture(int size, bool hot, std::vector<std::uint32_t>& pixels)
	{
		pixels.assign(static_cast<std::size_t>(size) * static_cast<std::size_t>(size), 0u);
		const float half = 0.5f * static_cast<float>(size);
		const float colour[3] = { hot ? 0.2f : 1.0f, hot ? 1.0f : 0.7f, hot ? 0.3f : 0.1f };
		const float edge = 1.5f / half;
		const auto round_at = [&](float dx, float dy, float centre_x)
		{
			// One round, standing: a body 0.09 wide from -0.22 to 0.14, a pointed tip up to -0.32 (y down).
			const float x = std::fabs(dx - centre_x);
			if (dy >= -0.22f && dy <= 0.14f)
			{
				return std::clamp((0.045f - x) / edge + 0.5f, 0.0f, 1.0f);
			}
			if (dy < -0.22f && dy >= -0.32f)
			{
				const float width = 0.045f * (dy + 0.32f) / 0.10f;
				return std::clamp((width - x) / edge + 0.5f, 0.0f, 1.0f);
			}
			return 0.0f;
		};
		for (int y = 0; y < size; ++y)
		{
			for (int x = 0; x < size; ++x)
			{
				const float dx = (static_cast<float>(x) + 0.5f - half) / half;
				const float dy = (static_cast<float>(y) + 0.5f - half) / half;
				const float r = std::sqrt(dx * dx + dy * dy);
				const float ring = std::clamp((r - 0.88f) / edge + 0.5f, 0.0f, 1.0f) * std::clamp((0.98f - r) / edge + 0.5f, 0.0f, 1.0f);
				const float fill = r < 0.9f ? 0.08f : 0.0f;
				const float icon = std::max({ round_at(dx, dy, -0.14f), round_at(dx, dy, 0.0f), round_at(dx, dy, 0.14f) });
				const float alpha = std::max({ ring * 0.6f, fill, icon * 0.85f });
				const auto a = static_cast<std::uint32_t>(std::lround(alpha * 255.0f));
				const auto red = static_cast<std::uint32_t>(std::lround(colour[0] * alpha * 255.0f));
				const auto green = static_cast<std::uint32_t>(std::lround(colour[1] * alpha * 255.0f));
				const auto blue = static_cast<std::uint32_t>(std::lround(colour[2] * alpha * 255.0f));
				pixels[static_cast<std::size_t>(y) * static_cast<std::size_t>(size) + static_cast<std::size_t>(x)] = (a << 24) | (red << 16) | (green << 8) | blue;
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
