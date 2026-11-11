#include "ui/body_calibration_flow.hpp"

#include <windows.h>

#include <atomic>
#include <cmath>
#include <cstdio>
#include <mutex>

#include "body/body_calibration.hpp"
#include "common/log.hpp"
#include "common/settings_store.hpp"
#include "input/haptics.hpp"
#include "safety/mp_guard.hpp"
#include "stereo/tracking.hpp"

namespace bfvr::ui
{
	namespace
	{
		namespace cal = body::calibration;

		constexpr ULONGLONG prompt_ms = 40000; // after a level loads the prompt waits this long for the pose
		constexpr ULONGLONG result_ms = 4000;  // the result stays on screen
		constexpr std::uint8_t trigger_pressed = 128;

		std::mutex lock;
		cal::t_pose detector;
		bool prompting{};
		ULONGLONG prompt_until{};
		ULONGLONG result_until{};
		std::string result_text;
		std::string notice;
		bool level_was_loaded{};
		bool start_requested{};
		LARGE_INTEGER last_update{};
		std::atomic<bool> holds_triggers{};
		cal::measurement last_measured;
		cal::body_shape last_shape;

		std::string mode()
		{
			return settings::global().text("body_calibration", "first");
		}

		void begin(ULONGLONG now, const char* why)
		{
			prompting = true;
			prompt_until = now + prompt_ms;
			detector.reset();
			log::write("body calibration: asking for the T-pose (%s)", why);
		}

		void finish(const cal::measurement& measured)
		{
			last_measured = measured;
			last_shape = cal::shape_for(measured);
			settings::registry& store = settings::global();
			char value[32];
			std::snprintf(value, sizeof(value), "%.2f", measured.stature);
			store.set("body_height", value);
			std::snprintf(value, sizeof(value), "%.2f", measured.span);
			store.set("body_span", value);
			std::snprintf(value, sizeof(value), "%.2f", last_shape.arm_length);
			store.set("arm_length", value);
			std::snprintf(value, sizeof(value), "%.2f", last_shape.shoulder_width);
			store.set("shoulder_width", value);
			const std::string saved = settings::save_global();
			char text[128];
			std::snprintf(text, sizeof(text), "Body fitted: %.2f m tall, arms %.2f m%s", measured.stature, measured.span + 0.18f,
				measured.from_floor ? "" : " (height from the arms)");
			result_text = text;
			log::write("body calibration: stature %.3f m (%s), eyes %.3f m, span %.3f m -> arm_length %.2f shoulder_width %.3f scale %.3f%s%s",
				measured.stature, measured.from_floor ? "floor" : "span", measured.eye_height, measured.span, last_shape.arm_length, last_shape.shoulder_width,
				last_shape.scale, saved.empty() ? "" : "; save: ", saved.c_str());
			input::haptic(0, input::pulses::grab);
			input::haptic(1, input::pulses::grab);
		}
	}

	void update_body_calibration()
	{
		const std::lock_guard guard(lock);
		LARGE_INTEGER counter{}, frequency{};
		QueryPerformanceCounter(&counter);
		QueryPerformanceFrequency(&frequency);
		const float seconds = last_update.QuadPart ? std::fmin(static_cast<float>(counter.QuadPart - last_update.QuadPart) / static_cast<float>(frequency.QuadPart), 0.1f) : 0.0f;
		last_update = counter;
		const ULONGLONG now = GetTickCount64();

		const std::string how = mode();
		const bool loaded = safety::campaign_level_loaded();
		if (loaded && !level_was_loaded && how != "off" && (how == "always" || settings::global().real("body_height", 0.0f) <= 0.0f))
		{
			begin(now, how == "always" ? "every level" : "not calibrated yet");
		}
		level_was_loaded = loaded;
		if (start_requested)
		{
			start_requested = false;
			begin(now, "asked");
		}
		if (prompting && (now > prompt_until || !loaded))
		{
			prompting = false;
			log::write("body calibration: no T-pose seen, keeping the body as it was");
		}

		holds_triggers = false;
		if (prompting)
		{
			cal::sample s;
			s.seconds = seconds;
			const stereo::head_sample head = stereo::latest_head_sample();
			s.head_valid = head.valid;
			if (head.valid)
			{
				s.head = { 0.5f * (head.eyes[0].position[0] + head.eyes[1].position[0]), 0.5f * (head.eyes[0].position[1] + head.eyes[1].position[1]),
					0.5f * (head.eyes[0].position[2] + head.eyes[1].position[2]) };
			}
			const stereo::hand_sample hands = stereo::latest_hand_sample();
			s.hands_valid = hands.valid[0] && hands.valid[1];
			s.left = { hands.grip[0].position[0], hands.grip[0].position[1], hands.grip[0].position[2] };
			s.right = { hands.grip[1].position[0], hands.grip[1].position[1], hands.grip[1].position[2] };
			const input::gamepad_state pad = input::latest();
			s.left_trigger = pad.left_trigger >= trigger_pressed;
			s.right_trigger = pad.right_trigger >= trigger_pressed;
			s.floor_valid = stereo::latest_floor(s.floor_y);
			const cal::t_pose::state state = detector.update(s);
			holds_triggers = detector.in_pose(s) || state == cal::t_pose::state::done;
			if (state == cal::t_pose::state::done)
			{
				finish(detector.result());
				prompting = false;
				result_until = now + result_ms;
			}
			else if (state == cal::t_pose::state::holding)
			{
				char text[96];
				std::snprintf(text, sizeof(text), "Hold still: measuring %d %%", static_cast<int>(detector.progress() * 100.0f));
				notice = text;
			}
			else
			{
				notice = "Fit the body to you: arms out to the sides, hold both triggers";
			}
		}
		if (!prompting)
		{
			notice = now < result_until ? result_text : std::string{};
		}
	}

	std::string body_calibration_notice()
	{
		const std::lock_guard guard(lock);
		return notice;
	}

	input::gamepad_state filter_calibration_pad(const input::gamepad_state& pad)
	{
		input::gamepad_state out = pad;
		if (holds_triggers.load(std::memory_order_relaxed))
		{
			out.left_trigger = 0;
			out.right_trigger = 0;
		}
		return out;
	}

	std::string body_calibration_command(const std::string& argument)
	{
		const std::lock_guard guard(lock);
		if (argument == "start")
		{
			start_requested = true;
			return "body calibration: asking for the T-pose";
		}
		if (argument == "stop")
		{
			prompting = false;
			return "body calibration: stopped";
		}
		char text[400];
		std::snprintf(text, sizeof(text),
			"body_calibration=%s prompting=%d progress=%.2f holds_triggers=%d notice=\"%s\"\nlast: valid=%d stature=%.3f eyes=%.3f span=%.3f from_floor=%d -> scale=%.3f arm_length=%.2f shoulder_width=%.3f",
			mode().c_str(), prompting ? 1 : 0, detector.progress(), holds_triggers.load() ? 1 : 0, notice.c_str(), last_measured.valid ? 1 : 0, last_measured.stature,
			last_measured.eye_height, last_measured.span, last_measured.from_floor ? 1 : 0, last_shape.scale, last_shape.arm_length, last_shape.shoulder_width);
		return text;
	}
}
