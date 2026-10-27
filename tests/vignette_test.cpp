// Checks the comfort vignette (src/ui/vignette): strength from turning and moving per mode, attack
// and release times, and the overlay texture (clear centre, dark edge, nothing at strength 0); and
// the aim reticle (src/ui/reticle): its texture and its placement facing the eye.
#include <cmath>
#include <cstdio>
#include <vector>

#include "body/arm_ik.hpp"
#include "ui/reticle.hpp"
#include "ui/vignette.hpp"

using namespace bfvr::ui;

namespace
{
	int failures = 0;
	constexpr float frame = 1.0f / 90.0f;

	void expect(bool condition, const char* what)
	{
		if (!condition)
		{
			std::printf("FAIL: %s\n", what);
			++failures;
		}
	}

	float run(vignette& v, float yaw_rate, float speed, float seconds)
	{
		float strength = 0;
		for (float t = 0; t < seconds - 1e-6f; t += frame)
		{
			strength = v.update(yaw_rate, speed, frame);
		}
		return strength;
	}

	void test_modes()
	{
		vignette turning;
		expect(run(turning, 0.2f, 0, 1.0f) == 0, "slow aiming stays clear");
		expect(std::fabs(run(turning, 3.0f, 0, 1.0f) - 0.7f) < 1e-4f, "fast turn reaches the configured strength");
		turning.reset();
		expect(run(turning, 0, 6.0f, 1.0f) == 0, "turning mode ignores walking");
		expect(std::fabs(run(turning, -3.0f, 0, 1.0f) - 0.7f) < 1e-4f, "turning left counts too");

		vignette_config config;
		config.mode = vignette_mode::moving;
		vignette moving(config);
		expect(run(moving, 3.0f, 0, 1.0f) == 0, "moving mode ignores turning");
		const float half = run(moving, 0, 3.0f, 1.0f);
		expect(std::fabs(half - 0.35f) < 1e-3f, "half way between move_start and move_full: half strength");

		config.mode = vignette_mode::both;
		vignette both(config);
		expect(run(both, 3.0f, 0, 1.0f) > 0.69f && run(both, 0, 6.0f, 1.0f) > 0.69f, "both reacts to either");

		config.mode = vignette_mode::off;
		vignette off(config);
		expect(run(off, 10.0f, 10.0f, 1.0f) == 0, "off stays clear");
	}

	void test_timing()
	{
		vignette v;
		v.update(5.0f, 0, frame);
		expect(v.strength() > 0 && v.strength() < 0.7f, "attack is not instant");
		const float attacked = run(v, 5.0f, 0, 0.08f);
		expect(std::fabs(attacked - 0.7f) < 1e-3f, "full after the attack time");
		const float during_release = run(v, 0, 0, 0.1f);
		expect(during_release > 0.3f && during_release < 0.7f, "fades over the release time");
		expect(run(v, 0, 0, 0.4f) == 0, "clear after the release time");

		// A snap turn: one frame at a huge rate, then nothing: a short flash that fades.
		vignette snap;
		snap.update(0.5236f / frame, 0, frame);
		const float peak = snap.strength();
		expect(peak > 0.05f, "snap turn flashes");
		expect(run(snap, 0, 0, 0.5f) == 0, "snap flash fades");
	}

	void test_texture()
	{
		std::vector<std::uint32_t> pixels;
		vignette_texture(0.0f, 64, pixels);
		bool clear = true;
		for (const std::uint32_t pixel : pixels)
		{
			clear = clear && (pixel >> 24) == 0;
		}
		expect(pixels.size() == 64 * 64 && clear, "strength 0 draws nothing");

		vignette_texture(1.0f, 64, pixels);
		const auto alpha = [&](int x, int y) { return pixels[static_cast<std::size_t>(y) * 64 + static_cast<std::size_t>(x)] >> 24; };
		expect(alpha(32, 32) == 0, "centre stays clear at full strength");
		expect(alpha(0, 0) == 255 && alpha(63, 32) == 255, "edge and corners are opaque at full strength");
		expect((pixels[0] & 0x00FFFFFFu) == 0, "premultiplied black");
		std::vector<std::uint32_t> weaker;
		vignette_texture(0.5f, 64, weaker);
		expect((weaker[32 * 64 + 54] >> 24) < (pixels[32 * 64 + 54] >> 24), "weaker strength: lighter at the same radius");
		bool monotonic = true;
		for (int x = 32; x < 63; ++x)
		{
			monotonic = monotonic && alpha(x + 1, 32) >= alpha(x, 32);
		}
		expect(monotonic, "darker towards the edge");
	}

	void test_reticle()
	{
		std::vector<std::uint32_t> pixels;
		reticle_texture(32, pixels);
		const auto at = [&](int x, int y) { return pixels[static_cast<std::size_t>(y) * 32 + static_cast<std::size_t>(x)]; };
		expect((at(16, 16) >> 24) == 255 && ((at(16, 16) >> 8) & 0xFF) == 255, "reticle centre: opaque bright dot");
		expect((at(16 + 9, 16) >> 24) > 150 && ((at(16 + 9, 16) >> 8) & 0xFF) < 40, "reticle ring: dark and mostly opaque");
		expect(at(0, 0) == 0 && at(31, 16) == 0, "reticle outside: transparent");

		const float ahead[3]{ 0, 0, -25 };
		const float eye[3]{ 0, 0, 0 };
		reticle_quad quad = place_reticle(ahead, eye, 1.0f * 3.14159265f / 180.0f);
		expect(std::fabs(quad.size - 0.4363f) < 1e-3f, "1 degree at 25 m is 0.436 m");
		expect(std::fabs(quad.pose.orientation[3]) > 0.9999f, "straight ahead: faces the eye unturned");
		const float side[3]{ 10, 0, 0 };
		quad = place_reticle(side, eye, 0.02f);
		const bfvr::body::ik::vec3 normal = bfvr::body::ik::rotate({ quad.pose.orientation[0], quad.pose.orientation[1], quad.pose.orientation[2], quad.pose.orientation[3] }, { 0, 0, 1 });
		expect(std::fabs(normal.x + 1.0f) < 1e-4f, "to the side: the quad turns to face the eye");
		expect(std::fabs(quad.size - 2.0f * 10.0f * std::tan(0.01f)) < 1e-4f, "size scales with distance");
	}
}

int main()
{
	test_reticle();
	test_modes();
	test_timing();
	test_texture();
	if (failures)
	{
		std::printf("%d failure(s)\n", failures);
		return 1;
	}
	std::printf("vignette: all checks passed\n");
	return 0;
}
