#include "input/haptics.hpp"

#include <algorithm>
#include <cstdio>
#include <mutex>

namespace bfvr::input
{
	void haptics_mixer::pulse(int hand, const haptic_pulse& pulse, bool from_game)
	{
		if (hand < 0 || hand > 1 || pulse.amplitude <= 0.0f || pulse.seconds <= 0.0f)
		{
			return;
		}
		// Full: replace the weakest, so a strong event is never dropped for a tick.
		running* slot = count_ < max_pulses ? &pulses_[count_++]
			: std::min_element(pulses_, pulses_ + max_pulses, [](const running& a, const running& b) { return a.amplitude < b.amplitude; });
		*slot = { hand, std::min(pulse.amplitude, 1.0f), pulse.seconds, from_game };
	}

	void haptics_mixer::recoil(const haptic_pulse& pulse)
	{
		this->pulse(1, pulse, true);
		if (two_handed_)
		{
			this->pulse(0, pulse, true);
		}
	}

	haptic_output haptics_mixer::update(float seconds)
	{
		haptic_output out;
		float level[2]{};
		for (int i = 0; i < count_; ++i)
		{
			if (pulses_[i].game ? config_.game : config_.gestures)
			{
				level[pulses_[i].hand] = std::max(level[pulses_[i].hand], pulses_[i].amplitude);
			}
		}
		if (config_.game)
		{
			const float low = std::clamp(low_, 0.0f, 1.0f), high = std::clamp(high_, 0.0f, 1.0f);
			level[1] = std::max({ level[1], low, high });
			level[0] = std::max({ level[0], low, two_handed_ ? high : 0.0f });
		}
		// Pulses last at least one frame, however short the frame.
		int kept = 0;
		for (int i = 0; i < count_; ++i)
		{
			pulses_[i].left -= seconds;
			if (pulses_[i].left > 0.0f)
			{
				pulses_[kept++] = pulses_[i];
			}
		}
		count_ = kept;
		const float strength = config_.enabled ? std::clamp(config_.strength, 0.0f, 1.0f) : 0.0f;
		for (int hand = 0; hand < 2; ++hand)
		{
			out.amplitude[hand] = level[hand] * strength;
		}
		return out;
	}

	namespace
	{
		std::mutex lock;
		haptics_mixer mixer;
		haptic_output last;
		float game_low{}, game_high{};
		unsigned long long pulse_count{}, rumble_count{}, frames_active{};
	}

	void configure_haptics(const haptics_config& config)
	{
		const std::lock_guard guard(lock);
		mixer.configure(config);
	}

	void set_game_rumble(float low, float high)
	{
		const std::lock_guard guard(lock);
		if (low != game_low || high != game_high)
		{
			++rumble_count;
		}
		game_low = low;
		game_high = high;
		mixer.game_rumble(low, high);
	}

	void set_two_handed_haptics(bool held)
	{
		const std::lock_guard guard(lock);
		mixer.set_two_handed(held);
	}

	void haptic(int hand, const haptic_pulse& pulse)
	{
		const std::lock_guard guard(lock);
		mixer.pulse(hand, pulse);
		++pulse_count;
	}

	void recoil(const haptic_pulse& pulse)
	{
		const std::lock_guard guard(lock);
		mixer.recoil(pulse);
		++pulse_count;
	}

	haptic_output haptics_frame(float seconds)
	{
		const std::lock_guard guard(lock);
		last = mixer.update(seconds);
		if (last.amplitude[0] > 0.0f || last.amplitude[1] > 0.0f)
		{
			++frames_active;
		}
		return last;
	}

	std::string haptics_pipe_command(const std::string& argument)
	{
		int hand = -1;
		float amplitude = 0.0f, ms = 0.0f;
		if (!argument.empty())
		{
			if (sscanf_s(argument.c_str(), "%d %f %f", &hand, &amplitude, &ms) != 3 || hand < 0 || hand > 1)
			{
				return "error: usage: haptics [<hand 0 off|1 gun> <amplitude 0..1> <ms>]";
			}
			haptic(hand, { amplitude, ms / 1000.0f });
		}
		const std::lock_guard guard(lock);
		const haptics_config& c = mixer.config();
		char text[256];
		std::snprintf(text, sizeof(text), "haptics=%d strength=%.2f game=%d gestures=%d game_low=%.2f game_high=%.2f rumble_changes=%llu pulses=%llu frames_active=%llu last_off=%.2f last_gun=%.2f",
			c.enabled ? 1 : 0, c.strength, c.game ? 1 : 0, c.gestures ? 1 : 0, game_low, game_high, rumble_count, pulse_count, frames_active,
			last.amplitude[0], last.amplitude[1]);
		return text;
	}
}
