#include "stereo/tracking.hpp"

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
