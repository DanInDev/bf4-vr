#include "stereo/tracking.hpp"

#include <atomic>
#include <cmath>
#include <mutex>

namespace bfvr::stereo
{
	namespace
	{
		std::mutex lock;
		head_sample head;
		hand_sample hands;
		rendered_view rendered;
	}

	void publish_hand_sample(const hand_sample& sample)
	{
		const std::lock_guard guard(lock);
		hands = sample;
	}

	hand_sample latest_hand_sample()
	{
		const std::lock_guard guard(lock);
		return hands;
	}

	void publish_head_sample(const head_sample& sample)
	{
		const std::lock_guard guard(lock);
		head = sample;
	}

	head_sample latest_head_sample()
	{
		const std::lock_guard guard(lock);
		return head;
	}

	namespace
	{
		std::atomic<bool> floor_known{};
		std::atomic<float> floor_height{};
	}

	void publish_floor(bool valid, float y)
	{
		floor_height = y;
		floor_known = valid && std::isfinite(y);
	}

	bool latest_floor(float& y)
	{
		y = floor_height.load(std::memory_order_relaxed);
		return floor_known.load(std::memory_order_relaxed);
	}

	void publish_rendered_view(const rendered_view& view)
	{
		const std::lock_guard guard(lock);
		rendered = view;
	}

	rendered_view latest_rendered_view()
	{
		const std::lock_guard guard(lock);
		return rendered;
	}
}
