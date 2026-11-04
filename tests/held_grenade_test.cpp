// Checks the held grenade overlay (src/ui/held_grenade): the texture's shape (body, fuse, pin ring,
// premultiplied alpha) and its placement (faces the eye, fuse up, body at the palm).
// An optional argument is a folder: the two textures are written there as BMPs to look at.
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <string>
#include <vector>

#include "body/arm_ik.hpp"
#include "ui/held_grenade.hpp"

using namespace bfvr::ui;
namespace ik = bfvr::body::ik;

namespace
{
	int failures = 0;
	constexpr int size = 128;

	void expect(bool condition, const char* what)
	{
		if (!condition)
		{
			std::printf("FAIL: %s\n", what);
			++failures;
		}
	}

	std::uint32_t alpha_at(const std::vector<std::uint32_t>& pixels, float u, float v)
	{
		const int x = static_cast<int>((u + 1.0f) * 0.5f * size);
		const int y = static_cast<int>((1.0f - v) * 0.5f * size);
		return pixels[static_cast<std::size_t>(y) * size + static_cast<std::size_t>(x)] >> 24;
	}

	void test_texture()
	{
		std::vector<std::uint32_t> pin, armed;
		held_grenade_texture(size, true, pin);
		held_grenade_texture(size, false, armed);
		expect(pin.size() == static_cast<std::size_t>(size) * size, "texture size");
		const float half = 0.5f * held_grenade_quad_size;
		const float body_v = -held_grenade_body_drop / half;
		expect(alpha_at(pin, 0.0f, body_v) == 255, "body centre opaque");
		expect(alpha_at(pin, 0.0f, body_v - 0.5f) == 255, "body lower half opaque");
		expect(alpha_at(pin, 0.0f, 0.66f) == 255, "fuse above the body");
		expect(alpha_at(pin, -0.95f, 0.95f) == 0 && alpha_at(pin, 0.95f, -0.95f) == 0, "corners clear");
		expect(alpha_at(pin, -0.38f, 0.69f) > 200, "pin ring drawn");
		expect(alpha_at(armed, -0.38f, 0.69f) == 0, "no ring once armed");
		expect(alpha_at(armed, 0.0f, 0.66f) == 255, "fuse stays once armed");
		bool premultiplied = true;
		for (const std::uint32_t p : pin)
		{
			const std::uint32_t a = p >> 24;
			premultiplied = premultiplied && ((p >> 16) & 0xFF) <= a && ((p >> 8) & 0xFF) <= a && (p & 0xFF) <= a;
		}
		expect(premultiplied, "colour never above alpha");
		// The body is lit from above-left: its upper left brighter than its lower right.
		const auto green = [&](float u, float v)
		{
			const int x = static_cast<int>((u + 1.0f) * 0.5f * size);
			const int y = static_cast<int>((1.0f - v) * 0.5f * size);
			return (pin[static_cast<std::size_t>(y) * size + static_cast<std::size_t>(x)] >> 8) & 0xFF;
		};
		expect(green(-0.25f, body_v + 0.25f) > green(0.25f, body_v - 0.35f), "lit from above-left");
	}

	ik::vec3 axis(const reticle_quad& quad, ik::vec3 local)
	{
		const ik::quat q{ quad.pose.orientation[0], quad.pose.orientation[1], quad.pose.orientation[2], quad.pose.orientation[3] };
		return ik::rotate(q, local);
	}

	bool near(ik::vec3 a, ik::vec3 b, float tolerance)
	{
		return ik::length(a - b) < tolerance;
	}

	void test_placement()
	{
		const float palm[3] = { -0.2f, 1.1f, -0.35f };
		const float eye[3] = { 0.0f, 1.6f, 0.0f };
		// The fuse tilted forward and to the right.
		const float fuse[3] = { 0.3f, 1.0f, -0.4f };
		const reticle_quad quad = place_held_grenade(palm, fuse, eye);
		expect(std::fabs(quad.size - held_grenade_quad_size) < 1e-6f, "quad size");
		const ik::vec3 to_eye = ik::normalize(ik::vec3{ eye[0] - palm[0], eye[1] - palm[1], eye[2] - palm[2] });
		const ik::vec3 z = axis(quad, { 0, 0, 1 });
		const ik::vec3 y = axis(quad, { 0, 1, 0 });
		expect(ik::dot(z, to_eye) > 0.995f, "faces the eye");
		ik::vec3 f{ fuse[0], fuse[1], fuse[2] };
		f = ik::normalize(f - to_eye * ik::dot(f, to_eye));
		expect(ik::dot(y, f) > 0.999f, "up along the fuse as seen");
		const ik::vec3 body = ik::vec3{ quad.pose.position[0], quad.pose.position[1], quad.pose.position[2] } - y * held_grenade_body_drop;
		expect(near(body, { palm[0], palm[1], palm[2] }, 1e-5f), "body at the palm");

		// Fuse pointing straight at the eye: still a unit rotation facing it.
		const float at_eye[3] = { eye[0] - palm[0], eye[1] - palm[1], eye[2] - palm[2] };
		const reticle_quad degenerate = place_held_grenade(palm, at_eye, eye);
		const float* o = degenerate.pose.orientation;
		expect(std::fabs(o[0] * o[0] + o[1] * o[1] + o[2] * o[2] + o[3] * o[3] - 1.0f) < 1e-4f, "unit quaternion when the fuse faces the eye");
		expect(ik::dot(axis(degenerate, { 0, 0, 1 }), to_eye) > 0.995f, "still faces the eye");

		// Every quadrant of the rotation conversion: the eye on each side.
		for (const ik::vec3 offset : { ik::vec3{ 0.5f, 0, 0 }, ik::vec3{ -0.5f, 0, 0 }, ik::vec3{ 0, 0, 0.5f }, ik::vec3{ 0, 0, -0.5f }, ik::vec3{ 0, -0.5f, 0.01f } })
		{
			const float e[3] = { palm[0] + offset.x, palm[1] + offset.y, palm[2] + offset.z };
			const float up[3] = { 0.0f, 0.0f, 1.0f };
			const reticle_quad q = place_held_grenade(palm, up, e);
			expect(ik::dot(axis(q, { 0, 0, 1 }), ik::normalize(offset)) > 0.995f, "faces an eye on any side");
		}
	}

	void write_bmp(const std::string& path, const std::vector<std::uint32_t>& pixels)
	{
		std::FILE* file = nullptr;
		if (fopen_s(&file, path.c_str(), "wb") != 0 || !file)
		{
			return;
		}
		const std::uint32_t bytes = size * size * 4;
		const std::uint8_t header[54] = { 'B', 'M', static_cast<std::uint8_t>(54 + bytes), static_cast<std::uint8_t>((54 + bytes) >> 8),
			static_cast<std::uint8_t>((54 + bytes) >> 16), 0, 0, 0, 0, 0, 54, 0, 0, 0, 40, 0, 0, 0, size, 0, 0, 0,
			static_cast<std::uint8_t>(-size), 0xFF, 0xFF, 0xFF, 1, 0, 32, 0 };
		std::fwrite(header, 1, sizeof(header), file);
		// Over mid grey, so the premultiplied colours read as they would in the headset.
		for (const std::uint32_t p : pixels)
		{
			const std::uint32_t a = p >> 24;
			std::uint8_t bgra[4];
			for (int c = 0; c < 3; ++c)
			{
				bgra[c] = static_cast<std::uint8_t>(((p >> (8 * c)) & 0xFF) + 128 * (255 - a) / 255);
			}
			bgra[3] = 255;
			std::fwrite(bgra, 1, 4, file);
		}
		std::fclose(file);
	}
}

int main(int argc, char** argv)
{
	test_texture();
	test_placement();
	if (argc > 1)
	{
		std::vector<std::uint32_t> pixels;
		held_grenade_texture(size, true, pixels);
		write_bmp(std::string(argv[1]) + "\\held_grenade_pin.bmp", pixels);
		held_grenade_texture(size, false, pixels);
		write_bmp(std::string(argv[1]) + "\\held_grenade_armed.bmp", pixels);
	}
	if (failures == 0)
	{
		std::printf("held_grenade: all tests passed\n");
	}
	return failures == 0 ? 0 : 1;
}
