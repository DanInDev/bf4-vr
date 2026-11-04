#include "ui/held_grenade.hpp"

#include <algorithm>
#include <cmath>

#include "body/arm_ik.hpp"

namespace bfvr::ui
{
	namespace
	{
		struct rgba
		{
			float r{}, g{}, b{}, a{};
		};

		// Premultiplied "over".
		void over(rgba& dst, float r, float g, float b, float coverage)
		{
			dst.r = r * coverage + dst.r * (1.0f - coverage);
			dst.g = g * coverage + dst.g * (1.0f - coverage);
			dst.b = b * coverage + dst.b * (1.0f - coverage);
			dst.a = coverage + dst.a * (1.0f - coverage);
		}

		// Lambert from a light above-left-front, an ambient floor and a small highlight; n is in quad
		// axes (x right, y up, z towards the eye). Kept dim so it does not glow in dark scenes.
		void shade(const float (&base)[3], float nx, float ny, float nz, float shine, float (&out)[3])
		{
			constexpr float lx = -0.40f, ly = 0.70f, lz = 0.59f;
			const float diffuse = std::max(0.0f, nx * lx + ny * ly + nz * lz);
			// Blinn half vector between the light and the eye (0, 0, 1).
			const float hx = lx, hy = ly, hz = lz + 1.0f;
			const float hl = std::sqrt(hx * hx + hy * hy + hz * hz);
			const float spec = shine * std::pow(std::max(0.0f, (nx * hx + ny * hy + nz * hz) / hl), 24.0f);
			for (int i = 0; i < 3; ++i)
			{
				out[i] = std::min(1.0f, 0.85f * (base[i] * (0.34f + 0.66f * diffuse) + spec));
			}
		}

		float coverage(float inside_distance, float edge)
		{
			return std::clamp(inside_distance / edge + 0.5f, 0.0f, 1.0f);
		}
	}

	void held_grenade_texture(int size, bool pin, std::vector<std::uint32_t>& pixels)
	{
		pixels.assign(static_cast<std::size_t>(size) * static_cast<std::size_t>(size), 0u);
		// Normalised quad coordinates: u right, v up, both -1..1 across the quad.
		const float half = 0.5f * held_grenade_quad_size;
		const float body_v = -held_grenade_body_drop / half;
		const float body_r = 0.032f / half;
		const float edge = 3.0f / static_cast<float>(size); // ~1.5 px
		constexpr float olive[3] = { 0.30f, 0.34f, 0.20f };
		constexpr float band[3] = { 0.62f, 0.52f, 0.12f };   // the yellow filler marking
		constexpr float metal[3] = { 0.42f, 0.43f, 0.36f };   // fuse and lever
		constexpr float steel[3] = { 0.62f, 0.62f, 0.60f };   // pin and ring
		for (int y = 0; y < size; ++y)
		{
			for (int x = 0; x < size; ++x)
			{
				const float u = (static_cast<float>(x) + 0.5f) / static_cast<float>(size) * 2.0f - 1.0f;
				const float v = 1.0f - (static_cast<float>(y) + 0.5f) / static_cast<float>(size) * 2.0f;
				rgba px;
				float c[3]{};

				// The lever (spoon) along the right side, from the fuse head down past the equator.
				const float du = u, dv = v - body_v;
				const float from_body = std::sqrt(du * du + dv * dv);
				{
					const float shell = std::min(from_body - body_r, body_r + 0.07f - from_body);
					const float side = std::min(u - 0.12f, dv + 0.18f);
					const float cov = coverage(std::min(shell, side), edge);
					if (cov > 0.0f)
					{
						const float nx = du / from_body, ny = dv / from_body;
						shade(metal, nx, ny, std::sqrt(std::max(0.0f, 1.0f - nx * nx - ny * ny)) + 0.3f, 0.25f, c);
						over(px, c[0], c[1], c[2], cov);
					}
				}
				// The body: a lit sphere with the marking band near the top.
				{
					const float cov = coverage(body_r - from_body, edge);
					if (cov > 0.0f)
					{
						const float nx = du / body_r, ny = dv / body_r;
						const float nz = std::sqrt(std::max(0.0f, 1.0f - nx * nx - ny * ny));
						const bool marked = ny > 0.56f && ny < 0.66f;
						shade(marked ? band : olive, nx, ny, nz, 0.18f, c);
						over(px, c[0], c[1], c[2], cov);
					}
				}
				// The fuse: a short cylinder on top, its head a little wider.
				{
					const float neck = std::min({ 0.15f - std::fabs(u), v - (body_v + body_r - 0.05f), 0.68f - v });
					const float head = std::min({ 0.19f - std::fabs(u), v - 0.60f, 0.76f - v });
					const float cov = coverage(std::max(neck, head), edge);
					if (cov > 0.0f)
					{
						const float width = v > 0.60f ? 0.19f : 0.15f;
						const float nx = std::clamp(u / width, -1.0f, 1.0f);
						shade(metal, nx, 0.0f, std::sqrt(1.0f - nx * nx), 0.35f, c);
						over(px, c[0], c[1], c[2], cov);
					}
				}
				// The lever's top, from the fuse head over to the side.
				{
					const float top = std::min({ u - 0.12f, 0.48f - u, v - 0.58f, 0.68f - v });
					// ...and down to where the lever meets the body.
					const float drop = std::min({ u - 0.39f, 0.48f - u, v - 0.30f, 0.68f - v });
					const float cov = coverage(std::max(top, drop), edge);
					if (cov > 0.0f)
					{
						shade(metal, 0.3f, 0.6f, 0.74f, 0.25f, c);
						over(px, c[0], c[1], c[2], cov);
					}
				}
				// The safety pin: a ring on the left of the fuse and its straight leg into the fuse.
				if (pin)
				{
					const float ru = u + 0.38f, rv = v - 0.56f;
					const float from_ring = std::sqrt(ru * ru + rv * rv);
					const float ring = 0.032f - std::fabs(from_ring - 0.13f);
					const float leg = std::min(0.024f - std::fabs(v - 0.56f), std::min(u + 0.26f, -0.10f - u));
					const float cov = coverage(std::max(ring, leg), edge);
					if (cov > 0.0f)
					{
						const float nx = from_ring > 1e-4f ? ru / from_ring : 0.0f;
						const float ny = from_ring > 1e-4f ? rv / from_ring : 1.0f;
						shade(steel, nx * 0.6f, ny * 0.6f, 0.8f, 0.5f, c);
						over(px, c[0], c[1], c[2], cov);
					}
				}

				const auto to8 = [](float value) { return static_cast<std::uint32_t>(std::lround(std::clamp(value, 0.0f, 1.0f) * 255.0f)); };
				pixels[static_cast<std::size_t>(y) * static_cast<std::size_t>(size) + static_cast<std::size_t>(x)]
					= (to8(px.a) << 24) | (to8(px.r) << 16) | (to8(px.g) << 8) | to8(px.b);
			}
		}
	}

	reticle_quad place_held_grenade(const float (&grenade)[3], const float (&fuse)[3], const float (&eye)[3])
	{
		using body::ik::vec3;
		reticle_quad quad;
		quad.size = held_grenade_quad_size;
		const vec3 centre{ grenade[0], grenade[1], grenade[2] };
		vec3 z = vec3{ eye[0], eye[1], eye[2] } - centre;
		const float distance = body::ik::length(z);
		if (distance < 1e-4f)
		{
			quad.pose.position[0] = grenade[0];
			quad.pose.position[1] = grenade[1];
			quad.pose.position[2] = grenade[2];
			return quad;
		}
		z = z * (1.0f / distance);
		// Up: the fuse as seen from the eye; pointing at the eye, fall back to LOCAL up, then to x.
		vec3 up{ fuse[0], fuse[1], fuse[2] };
		vec3 y = up - z * body::ik::dot(up, z);
		if (body::ik::length(y) < 1e-3f * std::max(1e-6f, body::ik::length(up)))
		{
			up = { 0.0f, 1.0f, 0.0f };
			y = up - z * body::ik::dot(up, z);
			if (body::ik::length(y) < 1e-3f)
			{
				up = { 1.0f, 0.0f, 0.0f };
				y = up - z * body::ik::dot(up, z);
			}
		}
		y = body::ik::normalize(y);
		const vec3 x = body::ik::cross(y, z);

		// Rotation with columns x, y, z.
		body::ik::quat q;
		const float trace = x.x + y.y + z.z;
		if (trace > 0.0f)
		{
			const float s = std::sqrt(trace + 1.0f) * 2.0f;
			q = { (y.z - z.y) / s, (z.x - x.z) / s, (x.y - y.x) / s, 0.25f * s };
		}
		else if (x.x > y.y && x.x > z.z)
		{
			const float s = std::sqrt(1.0f + x.x - y.y - z.z) * 2.0f;
			q = { 0.25f * s, (y.x + x.y) / s, (z.x + x.z) / s, (y.z - z.y) / s };
		}
		else if (y.y > z.z)
		{
			const float s = std::sqrt(1.0f + y.y - x.x - z.z) * 2.0f;
			q = { (y.x + x.y) / s, 0.25f * s, (z.y + y.z) / s, (z.x - x.z) / s };
		}
		else
		{
			const float s = std::sqrt(1.0f + z.z - x.x - y.y) * 2.0f;
			q = { (z.x + x.z) / s, (z.y + y.z) / s, 0.25f * s, (x.y - y.x) / s };
		}
		q = body::ik::normalize(q);
		quad.pose.orientation[0] = q.x;
		quad.pose.orientation[1] = q.y;
		quad.pose.orientation[2] = q.z;
		quad.pose.orientation[3] = q.w;
		// The quad's centre is above the body's by the drop, along the quad's up.
		const vec3 at = centre + y * held_grenade_body_drop;
		quad.pose.position[0] = at.x;
		quad.pose.position[1] = at.y;
		quad.pose.position[2] = at.z;
		return quad;
	}
}
