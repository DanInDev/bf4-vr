#include "input/gamepad.hpp"

#include <atomic>
#include <cstdio>
#include <mutex>

#include "common/log.hpp"
#include "input/release_gate.hpp"

namespace bfvr::input
{
	namespace
	{
		std::mutex lock;
		gamepad_state current;
		gamepad_state last_raw;
		release_gate gate;
		bool controllers_ours = true;
		bool was_holding = false;
		std::atomic<bool> release_on{ true };
		std::atomic<int> forced{ -1 }; // pipe fakefocus: -1 off, 0 lost, 1 back
		std::atomic<std::uint64_t> masked_frames{};

		// Under the lock.
		bool ours_now()
		{
			const int force = forced.load(std::memory_order_relaxed);
			return force >= 0 ? force == 1 : controllers_ours;
		}

		// Under the lock. Only a fresh sample may open the gate: the last one was taken while the runtime had the
		// controllers (a neutral pad), so judging it as ours would open everything before the held trigger arrives.
		void refresh()
		{
			const bool ours = ours_now();
			if (!release_on.load(std::memory_order_relaxed))
			{
				current = last_raw;
				return;
			}
			const bool held_before = gate.holding();
			current = gate.apply(last_raw, ours);
			const bool holding = ours && gate.holding();
			if (holding)
			{
				++masked_frames;
			}
			if (ours && held_before && !was_holding)
			{
				log::write("input: controllers back; closed until released: buttons=%04X lt=%d rt=%d sticks=%d%d (raw buttons=%04X lt=%u rt=%u)",
					gate.closed_buttons(), gate.closed_left_trigger() ? 1 : 0, gate.closed_right_trigger() ? 1 : 0, gate.closed_left_stick() ? 1 : 0,
					gate.closed_right_stick() ? 1 : 0, last_raw.buttons, last_raw.left_trigger, last_raw.right_trigger);
			}
			if (was_holding && !holding && ours)
			{
				log::write("input: every input released once; the pad is open again");
			}
			was_holding = ours && holding;
		}
	}

	void publish(const gamepad_state& state)
	{
		const std::lock_guard guard(lock);
		last_raw = state;
		refresh();
	}

	gamepad_state latest()
	{
		const std::lock_guard guard(lock);
		return current;
	}

	void set_controllers_ours(bool ours)
	{
		const std::lock_guard guard(lock);
		if (ours != controllers_ours)
		{
			log::write("input: %s", ours ? "controllers back from the runtime" : "the runtime has the controllers (system menu or a frozen stream): neutral pad");
			controllers_ours = ours;
			if (!ours_now())
			{
				refresh(); // neutral at once; coming back waits for the next publish
			}
		}
	}

	void set_focus_release(bool enabled)
	{
		release_on = enabled;
	}

	std::string fake_focus(const std::string& argument)
	{
		if (argument == "lost")
		{
			forced = 0;
		}
		else if (argument == "back")
		{
			forced = 1;
		}
		else if (argument == "off")
		{
			forced = -1;
		}
		else if (!argument.empty())
		{
			return "error: usage: fakefocus [lost|back|off]";
		}
		{
			const std::lock_guard guard(lock);
			if (!ours_now())
			{
				refresh(); // like the XR loop: lost neutralises now, back waits for the next sample
			}
		}
		return focus_release_report();
	}

	std::string focus_release_report()
	{
		const std::lock_guard guard(lock);
		const int force = forced.load();
		char text[256];
		std::snprintf(text, sizeof(text),
			"focus_release=%d controllers_ours=%d fake=%s closures=%u holding=%d closed buttons=%04X lt=%d rt=%d sticks=%d%d masked_frames=%llu raw buttons=%04X lt=%u rt=%u",
			release_on.load() ? 1 : 0, controllers_ours ? 1 : 0, force < 0 ? "off" : force == 0 ? "lost" : "back", gate.times_closed(),
			gate.holding() ? 1 : 0, gate.closed_buttons(), gate.closed_left_trigger() ? 1 : 0, gate.closed_right_trigger() ? 1 : 0,
			gate.closed_left_stick() ? 1 : 0, gate.closed_right_stick() ? 1 : 0, static_cast<unsigned long long>(masked_frames.load()), last_raw.buttons,
			last_raw.left_trigger, last_raw.right_trigger);
		return text;
	}
}
