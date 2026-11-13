#include "physics/ground_cache.hpp"

#include <algorithm>
#include <cmath>

namespace bfvr::physics
{
	namespace
	{
		constexpr std::uint64_t mask21 = (1ull << 21) - 1;
		constexpr std::uint64_t mask22 = (1ull << 22) - 1;

		std::int64_t extend(std::uint64_t value, int bits)
		{
			const std::uint64_t sign = 1ull << (bits - 1);
			return static_cast<std::int64_t>((value ^ sign) - sign);
		}
	}

	std::uint64_t ground_cache::key_of(const v3& point)
	{
		const auto x = static_cast<std::int64_t>(std::floor(point.x / column));
		const auto z = static_cast<std::int64_t>(std::floor(point.z / column));
		const auto y = static_cast<std::int64_t>(std::floor(point.y / band));
		return (static_cast<std::uint64_t>(x) & mask21) | ((static_cast<std::uint64_t>(z) & mask21) << 21) | ((static_cast<std::uint64_t>(y) & mask22) << 42);
	}

	ground_ray ground_cache::ray_for(std::uint64_t key)
	{
		const float x = (static_cast<float>(extend(key & mask21, 21)) + 0.5f) * column;
		const float z = (static_cast<float>(extend((key >> 21) & mask21, 21)) + 0.5f) * column;
		const float top = (static_cast<float>(extend((key >> 42) & mask22, 22)) + 1.0f) * band + above;
		return { key, { x, top, z }, { x, top - depth, z } };
	}

	ground_plane ground_cache::below(const v3& point)
	{
		if (!std::isfinite(point.x) || !std::isfinite(point.y) || !std::isfinite(point.z))
		{
			return {};
		}
		const std::uint64_t key = key_of(point);
		const std::lock_guard guard(lock_);
		const auto found = known_.find(key);
		if (found != known_.end())
		{
			return found->second;
		}
		if (!waiting_.count(key) && queue_.size() < most_queued)
		{
			waiting_[key] = true;
			queue_.push_back(key);
			++asked_;
		}
		return {};
	}

	int ground_cache::take_rays(ground_ray* out, int most)
	{
		const std::lock_guard guard(lock_);
		const int count = std::min(most, static_cast<int>(queue_.size()));
		for (int i = 0; i < count; ++i)
		{
			out[i] = ray_for(queue_[i]);
		}
		queue_.erase(queue_.begin(), queue_.begin() + count);
		return count;
	}

	void ground_cache::answer(std::uint64_t key, bool hit, const v3& point, const v3& normal)
	{
		const std::lock_guard guard(lock_);
		if (!waiting_.erase(key))
		{
			return; // cleared meanwhile (a new level)
		}
		if (known_.size() >= most_columns)
		{
			known_.clear();
		}
		ground_plane plane;
		if (!hit || !std::isfinite(point.y))
		{
			plane.what = ground_plane::kind::none;
			++misses_;
		}
		else
		{
			plane.what = ground_plane::kind::plane;
			plane.point = point;
			const float n = std::sqrt(normal.x * normal.x + normal.y * normal.y + normal.z * normal.z);
			plane.normal = n > 1e-4f && normal.y / n >= steepest_normal_y ? v3{ normal.x / n, normal.y / n, normal.z / n } : v3{ 0.0f, 1.0f, 0.0f };
			++hits_;
		}
		known_[key] = plane;
	}

	void ground_cache::retry_later(std::uint64_t key)
	{
		const std::lock_guard guard(lock_);
		waiting_.erase(key);
	}

	void ground_cache::clear()
	{
		const std::lock_guard guard(lock_);
		known_.clear();
		waiting_.clear();
		queue_.clear();
	}

	ground_cache::counts ground_cache::stats() const
	{
		const std::lock_guard guard(lock_);
		return { known_.size(), queue_.size(), asked_, hits_, misses_ };
	}
}
