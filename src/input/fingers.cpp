#include "input/fingers.hpp"

#include <windows.h>

#include <mutex>

namespace bfvr::input
{
	namespace
	{
		constexpr ULONGLONG max_age_ms = 250;

		std::mutex lock;
		body::fingers::finger_input current[2];
		ULONGLONG at{};
		bool overridden[2]{};
		body::fingers::finger_input override_value[2];
	}

	void publish_fingers(const body::fingers::finger_input (&hands)[2])
	{
		const std::lock_guard guard(lock);
		current[0] = hands[0];
		current[1] = hands[1];
		at = GetTickCount64();
	}

	void latest_fingers(body::fingers::finger_input (&hands)[2])
	{
		const std::lock_guard guard(lock);
		const bool fresh = GetTickCount64() - at < max_age_ms;
		for (int hand = 0; hand < 2; ++hand)
		{
			hands[hand] = overridden[hand] ? override_value[hand] : fresh ? current[hand] : body::fingers::finger_input{};
		}
	}

	void set_finger_override(int hand, bool active, const body::fingers::finger_input& input)
	{
		if (hand < 0 || hand > 1)
		{
			return;
		}
		const std::lock_guard guard(lock);
		overridden[hand] = active;
		override_value[hand] = input;
	}
}
