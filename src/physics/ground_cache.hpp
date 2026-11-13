#pragma once

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <unordered_map>
#include <vector>

#include "physics/loose_body.hpp"

// The level's ground under a point, from rays cast straight down (#61): answered from a cache of 4 cm
// columns, each for one 1 m band of start heights (a ray from above that band down 50 m). A column not
// known yet is unknown and queued; the game thread casts the queued rays (physics/ray_cast) and answers.
// Only surfaces facing up count: a steep hit is taken as flat ground at its height. Thread-safe;
// engine-free; tested by tests/ground_cache_test.cpp.
namespace bfvr::physics
{
	struct ground_ray
	{
		std::uint64_t key{};
		v3 from;
		v3 to;
	};

	class ground_cache : public ground_source
	{
	public:
		static constexpr float column = 0.04f;
		static constexpr float band = 1.0f;
		static constexpr float above = 0.5f;  // the ray starts this far above its band
		static constexpr float depth = 50.0f; // and looks this far down
		static constexpr float steepest_normal_y = 0.3f;
		static constexpr std::size_t most_columns = 20000;
		static constexpr std::size_t most_queued = 64;

		ground_plane below(const v3& point) override;

		// The game thread: up to most queued rays, oldest first (taken off the queue).
		int take_rays(ground_ray* out, int most);
		// A ray's answer: hit (a point and the surface normal) or nothing within depth.
		void answer(std::uint64_t key, bool hit, const v3& point, const v3& normal);
		// A ray that could not be cast: asked again the next time.
		void retry_later(std::uint64_t key);
		// A new level: everything forgotten.
		void clear();

		struct counts
		{
			std::size_t columns{};
			std::size_t queued{};
			std::uint64_t asked{};
			std::uint64_t hits{};
			std::uint64_t misses{};
		};
		counts stats() const;

		static std::uint64_t key_of(const v3& point);
		static ground_ray ray_for(std::uint64_t key);

	private:
		mutable std::mutex lock_;
		std::unordered_map<std::uint64_t, ground_plane> known_;
		std::unordered_map<std::uint64_t, bool> waiting_; // queued or cast, not yet answered
		std::vector<std::uint64_t> queue_;
		std::uint64_t asked_{};
		std::uint64_t hits_{};
		std::uint64_t misses_{};
	};
}
