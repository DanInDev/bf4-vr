// Checks the ground cache (src/physics/ground_cache, #61): a column not known yet is unknown and queued once,
// its ray starts above the asking point and looks down; an answer is kept per 4 cm column and 1 m band; a miss
// is "none"; a steep hit counts as flat ground; a new level forgets everything.
#include <cmath>
#include <cstdio>

#include "physics/ground_cache.hpp"

using namespace bfvr::physics;

namespace
{
	int failures = 0;

	void expect(bool condition, const char* what)
	{
		if (!condition)
		{
			std::printf("FAIL: %s\n", what);
			++failures;
		}
	}

	bool near(float a, float b, float tolerance = 1e-4f)
	{
		return std::fabs(a - b) <= tolerance;
	}
}

int main()
{
	ground_cache cache;
	const v3 at{ 10.013f, 397.6f, -649.71f };
	expect(cache.below(at).what == ground_plane::kind::unknown, "first ask: unknown");
	expect(cache.below({ at.x + 0.005f, at.y - 0.1f, at.z }).what == ground_plane::kind::unknown && cache.stats().queued == 1,
		"same column and band: queued once");
	ground_ray rays[8];
	expect(cache.take_rays(rays, 8) == 1 && cache.stats().queued == 0, "the game thread takes the one ray");
	const ground_ray& ray = rays[0];
	expect(ray.from.y > at.y && ray.from.y - at.y <= 1.5f + 1e-3f && near(ray.to.y, ray.from.y - 50.0f), "ray: starts above the point, looks 50 m down");
	expect(std::fabs(ray.from.x - at.x) <= 0.02f + 1e-4f && std::fabs(ray.from.z - at.z) <= 0.02f + 1e-4f && near(ray.from.x, ray.to.x),
		"ray: straight down the middle of the 4 cm column");
	expect(cache.below(at).what == ground_plane::kind::unknown && cache.stats().queued == 0, "cast, not answered: not asked again");
	cache.answer(ray.key, true, { ray.from.x, 396.2f, ray.from.z }, { 0.0f, 2.0f, 0.0f });
	const ground_plane plane = cache.below(at);
	expect(plane.what == ground_plane::kind::plane && near(plane.point.y, 396.2f) && near(plane.normal.y, 1.0f), "answered: the plane, normal made unit");

	const v3 lower{ at.x, at.y - 1.2f, at.z };
	expect(cache.below(lower).what == ground_plane::kind::unknown, "a band lower is its own question (under an overhang)");
	cache.take_rays(rays, 8);
	cache.answer(rays[0].key, false, {}, {});
	expect(cache.below(lower).what == ground_plane::kind::none, "nothing within 50 m: none");

	const v3 wall{ -3.5f, 2.0f, 1.0f };
	cache.below(wall);
	cache.take_rays(rays, 8);
	cache.answer(rays[0].key, true, { -3.5f, 1.4f, 1.0f }, { 1.0f, 0.1f, 0.0f });
	const ground_plane steep = cache.below(wall);
	expect(steep.what == ground_plane::kind::plane && near(steep.normal.y, 1.0f) && near(steep.point.y, 1.4f), "a steep hit: flat ground at its height");

	expect(ground_cache::key_of({ -0.01f, 0.0f, 0.0f }) != ground_cache::key_of({ 0.01f, 0.0f, 0.0f }), "negative and positive columns differ");
	const ground_ray back = ground_cache::ray_for(ground_cache::key_of({ -12.345f, -7.5f, 88.8f }));
	expect(std::fabs(back.from.x + 12.345f) <= 0.02f + 1e-3f && std::fabs(back.from.z - 88.8f) <= 0.02f + 1e-3f && back.from.y > -7.5f,
		"negative coordinates round-trip through the key");

	for (int i = 0; i < 100; ++i)
	{
		cache.below({ static_cast<float>(i), 0.0f, 0.0f });
	}
	expect(cache.stats().queued == ground_cache::most_queued, "the queue is capped");
	cache.clear();
	expect(cache.stats().columns == 0 && cache.stats().queued == 0 && cache.below(at).what == ground_plane::kind::unknown, "a new level: forgotten");

	if (failures)
	{
		std::printf("ground_cache: %d failed\n", failures);
		return 1;
	}
	std::printf("ground_cache: all checks passed\n");
	return 0;
}
