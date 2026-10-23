#include "input/gamepad.hpp"

#include <mutex>

namespace bfvr::input
{
	namespace
	{
		std::mutex lock;
		gamepad_state current;
	}

	void publish(const gamepad_state& state)
	{
		const std::lock_guard guard(lock);
		current = state;
	}

	gamepad_state latest()
	{
		const std::lock_guard guard(lock);
		return current;
	}
}
