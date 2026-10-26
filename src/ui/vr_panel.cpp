#include "ui/vr_panel.hpp"

#include <windows.h>
#include <xinput.h>

#include <atomic>
#include <cmath>
#include <cstdio>
#include <mutex>

#include "body/arm_ik.hpp"
#include "common/log.hpp"
#include "common/settings_store.hpp"
#include "input/pointer.hpp"
#include "stereo/eye_math.hpp"
#include "ui/menu_button.hpp"
#include "ui/panel_canvas.hpp"
#include "ui/settings_panel.hpp"

namespace bfvr::ui
{
	namespace
	{
		constexpr float panel_distance = 0.75f;
		constexpr float panel_drop = 0.1f;   // below eye level
		constexpr float panel_width = 0.8f;  // metres; height follows the image aspect
		constexpr ULONGLONG start_pulse_ms = 150;
		constexpr ULONGLONG status_ms = 2500;
		constexpr std::uint8_t trigger_press = 150;
		constexpr std::uint8_t trigger_release = 80;

		std::mutex lock;
		settings_panel panel;
		bool pages_built{};
		bool open{};
		stereo::pose pose;
		menu_button button;
		LARGE_INTEGER last_update{};
		int pointer_hand{ 1 };
		bool trigger_down[2]{};
		std::string status;
		ULONGLONG status_until{};

		// The image and what it was drawn from.
		std::vector<std::uint32_t> image;
		std::uint64_t image_revision{};
		std::string drawn_key;

		std::atomic<bool> panel_open{};
		std::atomic<bool> enabled{ true };
		std::atomic<ULONGLONG> pass_start_until{};

		// Called with lock held.
		void ensure_pages()
		{
			if (!pages_built)
			{
				panel.set_pages(default_pages(settings::global()));
				pages_built = true;
			}
		}

		// Called with lock held: 0.75 m ahead of the head, level, facing it.
		void place_in_front()
		{
			pose = {};
			pose.position[1] = -panel_drop;
			pose.position[2] = -panel_distance;
			const stereo::head_sample head = stereo::latest_head_sample();
			if (!head.valid)
			{
				return;
			}
			const stereo::pose centre = stereo::eye_math::centre_pose(head);
			const body::ik::quat q = body::ik::normalize({ centre.orientation[0], centre.orientation[1], centre.orientation[2], centre.orientation[3] });
			const body::ik::vec3 forward = body::ik::rotate(q, { 0, 0, -1 });
			const float yaw = std::atan2(-forward.x, -forward.z);
			pose.orientation[1] = std::sin(0.5f * yaw);
			pose.orientation[3] = std::cos(0.5f * yaw);
			pose.position[0] = centre.position[0] - std::sin(yaw) * panel_distance;
			pose.position[1] = centre.position[1] - panel_drop;
			pose.position[2] = centre.position[2] - std::cos(yaw) * panel_distance;
		}

		void set_open(bool value)
		{
			if (value)
			{
				ensure_pages();
				place_in_front();
				status.clear();
				// A trigger already held when the panel opens is not a click.
				trigger_down[0] = trigger_down[1] = true;
			}
			panel.hover({});
			open = value;
			panel_open = value;
			log::write("panel: %s", value ? "open" : "closed");
		}

		// Called with lock held.
		void apply(settings_panel::action result)
		{
			switch (result)
			{
			case settings_panel::action::save:
			{
				const std::string error = settings::save_global();
				status = error.empty() ? "saved to bfvr.ini" : error;
				status_until = GetTickCount64() + status_ms;
				break;
			}
			case settings_panel::action::close:
				set_open(false);
				break;
			case settings_panel::action::changed:
			case settings_panel::action::none:
				break;
			}
		}

		input::quad_target quad()
		{
			input::quad_target area;
			area.pose = pose;
			area.width = panel_width;
			area.height = panel_width * settings_panel::height / settings_panel::width;
			return area;
		}

		// Called with lock held: redraws when anything shown changed.
		void redraw()
		{
			const settings::registry& store = settings::global();
			if (GetTickCount64() > status_until)
			{
				status.clear();
			}
			char key[160];
			std::snprintf(key, sizeof(key), "%d/%d/%d/%d/%llu/%d/", panel.current_page(), panel.first_row(), static_cast<int>(panel.hovered().kind),
				panel.hovered().index, static_cast<unsigned long long>(store.revision()), store.changed() ? 1 : 0);
			const std::string current = key + status;
			if (current == drawn_key && !image.empty())
			{
				return;
			}
			if (rasterise(panel.draw(store, status), settings_panel::width, settings_panel::height, image))
			{
				drawn_key = current;
				++image_revision;
			}
		}
	}

	void update_vr_panel()
	{
		const std::lock_guard guard(lock);
		LARGE_INTEGER now{}, frequency{};
		QueryPerformanceCounter(&now);
		QueryPerformanceFrequency(&frequency);
		const float seconds = last_update.QuadPart ? std::fmin(static_cast<float>(now.QuadPart - last_update.QuadPart) / static_cast<float>(frequency.QuadPart), 0.1f) : 0.0f;
		last_update = now;

		enabled = settings::global().boolean("vr_panel", true);
		const input::gamepad_state pad = input::latest();
		const menu_button_events events = button.update((pad.buttons & XINPUT_GAMEPAD_START) != 0, seconds);
		if (events.long_press && enabled.load())
		{
			set_open(!open);
		}
		if (events.short_press)
		{
			if (open)
			{
				set_open(false);
			}
			else
			{
				pass_start_until = GetTickCount64() + start_pulse_ms;
			}
		}
		if (open && !enabled.load())
		{
			set_open(false);
		}
		if (!open)
		{
			return;
		}

		// Either hand points; the one whose trigger was pulled last owns the pointer.
		const std::uint8_t triggers[2] = { pad.left_trigger, pad.right_trigger };
		bool clicked = false;
		for (int hand = 0; hand < 2; ++hand)
		{
			const bool down = trigger_down[hand] ? triggers[hand] > trigger_release : triggers[hand] >= trigger_press;
			if (down && !trigger_down[hand])
			{
				pointer_hand = hand;
				clicked = true;
			}
			trigger_down[hand] = down;
		}
		const stereo::hand_sample hands = stereo::latest_hand_sample();
		target over;
		if (hands.valid[pointer_hand])
		{
			const input::pointer_hit hit = input::point_at(hands.aim[pointer_hand], quad());
			if (hit.hit)
			{
				over = panel.hit(hit.u, hit.v);
			}
		}
		panel.hover(over);
		if (clicked && over.kind != target_kind::none)
		{
			apply(panel.click(over, settings::global()));
		}
		if (open)
		{
			redraw();
		}
	}

	panel_view vr_panel_view()
	{
		const std::lock_guard guard(lock);
		panel_view view;
		view.open = open;
		view.pose = pose;
		view.width = panel_width;
		view.height = panel_width * settings_panel::height / settings_panel::width;
		return view;
	}

	bool vr_panel_pixels(std::uint64_t& revision, std::vector<std::uint32_t>& pixels)
	{
		const std::lock_guard guard(lock);
		if (image.empty() || image_revision == revision)
		{
			return false;
		}
		pixels = image;
		revision = image_revision;
		return true;
	}

	input::gamepad_state filter_panel_pad(const input::gamepad_state& pad)
	{
		if (!enabled.load(std::memory_order_relaxed))
		{
			return pad;
		}
		input::gamepad_state out = pad;
		if (panel_open.load(std::memory_order_relaxed))
		{
			out = {};
			out.connected = pad.connected;
		}
		out.buttons &= static_cast<std::uint16_t>(~XINPUT_GAMEPAD_START);
		if (GetTickCount64() < pass_start_until.load(std::memory_order_relaxed))
		{
			out.buttons |= XINPUT_GAMEPAD_START;
			out.connected = true;
		}
		return out;
	}

	std::string panel_pipe_command(const std::string& argument)
	{
		const std::lock_guard guard(lock);
		char verb[16]{};
		char path[260]{};
		float u{}, v{};
		sscanf_s(argument.c_str(), "%15s", verb, static_cast<unsigned>(sizeof(verb)));
		const std::string command = verb;
		if (command == "open" || command == "close")
		{
			set_open(command == "open");
		}
		else if ((command == "hover" || command == "click") && sscanf_s(argument.c_str(), "%*s %f %f", &u, &v) == 2)
		{
			ensure_pages();
			const target over = panel.hit(u, v);
			panel.hover(over);
			if (command == "click")
			{
				apply(panel.click(over, settings::global()));
			}
		}
		else if (command == "shot" && sscanf_s(argument.c_str(), "%*s %259[^\n]", path, static_cast<unsigned>(sizeof(path))) == 1)
		{
			ensure_pages();
			drawn_key.clear();
			redraw();
			return write_bmp(path, image, settings_panel::width, settings_panel::height) ? std::string("panel written to ") + path : "error: could not write the image";
		}
		else if (!command.empty())
		{
			return "error: usage: panel [open|close|hover <u> <v>|click <u> <v>|shot <path.bmp>]";
		}
		if (open)
		{
			redraw();
		}
		char text[256];
		std::snprintf(text, sizeof(text), "panel open=%d enabled=%d page=%d first_row=%d hover=%d/%d image_revision=%llu pose=(%.2f %.2f %.2f) status=%s",
			open ? 1 : 0, enabled.load() ? 1 : 0, panel.current_page(), panel.first_row(), static_cast<int>(panel.hovered().kind), panel.hovered().index,
			static_cast<unsigned long long>(image_revision), pose.position[0], pose.position[1], pose.position[2], status.empty() ? "-" : status.c_str());
		return text;
	}
}
