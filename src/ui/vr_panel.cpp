#include "ui/vr_panel.hpp"

#include <windows.h>
#include <xinput.h>

#include <atomic>
#include <cmath>
#include <cstdio>
#include <mutex>
#include <utility>

#include "body/arm_ik.hpp"
#include "common/log.hpp"
#include "common/settings_store.hpp"
#include "input/handedness.hpp"
#include "input/haptics.hpp"
#include "input/pointer.hpp"
#include "safety/mp_guard.hpp"
#include "stereo/eye_math.hpp"
#include "ui/menu_button.hpp"
#include "ui/panel_canvas.hpp"
#include "ui/settings_panel.hpp"

namespace bfvr::ui
{
	namespace
	{
		constexpr float panel_distance = 0.8f;
		constexpr float panel_width = 0.8f;  // metres; height follows the image aspect
		constexpr ULONGLONG start_pulse_ms = 150;
		constexpr ULONGLONG status_ms = 2500;
		constexpr std::uint8_t trigger_press = 150;
		constexpr std::uint8_t trigger_release = 80;
		constexpr std::uint16_t stick_clicks = XINPUT_GAMEPAD_LEFT_THUMB | XINPUT_GAMEPAD_RIGHT_THUMB;

		std::mutex lock;
		settings_panel panel;
		bool pages_built{};
		bool open{};
		stereo::pose pose;
		menu_button button;
		stick_chord chord;
		level_hint hint;
		LARGE_INTEGER last_update{};
		int pointer_hand{ 1 };
		bool trigger_down[2]{};
		std::string status;
		ULONGLONG status_until{};
		bool hint_shown{};

		// The image and what it was drawn from.
		std::vector<std::uint32_t> image;
		std::uint64_t image_revision{};
		std::string drawn_key;

		std::string notice_text;
		std::vector<std::uint32_t> notice_image;
		std::uint64_t notice_revision{};

		std::atomic<bool> panel_open{};
		std::atomic<bool> enabled{ true };
		std::atomic<bool> stick_menu{ true };
		std::atomic<bool> focus_lost{};
		std::atomic<bool> notice_on{};
		std::atomic<std::uint16_t> sticks_to_game{}; // game-side bits of the clicks the game may see
		std::atomic<ULONGLONG> pass_start_until{};

		// Physical stick clicks to the game's bits (dominant_hand = left swaps them, input/handedness).
		std::uint16_t game_sticks(bool left, bool right)
		{
			if (input::left_handed())
			{
				std::swap(left, right);
			}
			return static_cast<std::uint16_t>((left ? XINPUT_GAMEPAD_LEFT_THUMB : 0) | (right ? XINPUT_GAMEPAD_RIGHT_THUMB : 0));
		}

		// Called with lock held.
		void ensure_pages()
		{
			if (!pages_built)
			{
				panel.set_pages(default_pages(settings::global()));
				pages_built = true;
			}
		}

		// Called with lock held: ahead of the head at eye height, level, facing it.
		void place_in_front()
		{
			pose = {};
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
			pose.position[1] = centre.position[1];
			pose.position[2] = centre.position[2] - std::cos(yaw) * panel_distance;
		}

		// Called with lock held.
		void set_open(bool value, const char* by)
		{
			if (value)
			{
				ensure_pages();
				place_in_front();
				status.clear();
				hint.dismiss();
				// A trigger already held when the panel opens is not a click.
				trigger_down[0] = trigger_down[1] = true;
			}
			if (value != open)
			{
				input::haptic(0, input::pulses::grab);
				input::haptic(1, input::pulses::grab);
			}
			panel.hover({});
			open = value;
			panel_open = value;
			log::write("panel: %s (%s)", value ? "open" : "closed", by);
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
				set_open(false, "Close");
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

		// Called with lock held: the notice for this frame (empty: none).
		void show_notice(const std::string& text, bool warning)
		{
			if (text == notice_text)
			{
				return;
			}
			notice_text = text;
			if (!text.empty())
			{
				const std::vector<draw_item> items = {
					{ draw_item::type::box, { 0, 0, notice_width, notice_height }, warning ? 0x6B4A12u : 0x2A3038u, {}, 0, 0, false },
					{ draw_item::type::text, { 12, 0, notice_width - 24, notice_height }, warning ? 0xFFE3A0u : 0xE8ECF0u, text, 1, 34, false },
				};
				if (!rasterise(items, notice_width, notice_height, notice_image))
				{
					notice_image.clear();
				}
				++notice_revision;
			}
			notice_on = !text.empty() && !notice_image.empty();
			log::write("panel: notice %s", text.empty() ? "hidden" : ("\"" + text + "\"").c_str());
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

		const settings::registry& store = settings::global();
		enabled = store.boolean("vr_panel", true);
		stick_menu = enabled.load() && store.boolean("stick_menu", true);
		input::gamepad_state pad = input::latest();
		if (focus_lost.load())
		{
			// The runtime took the controllers (SteamVR dashboard): what was held is no press.
			if (button.down() || chord.chord())
			{
				log::write("panel: input focus lost with the menu button held %.2f s, stick chord %d: dropped", button.held(), chord.chord() ? 1 : 0);
			}
			button = menu_button{};
			chord = stick_chord{};
			pad = {};
		}

		const menu_button_events events = button.update((pad.buttons & XINPUT_GAMEPAD_START) != 0, seconds);
		if (events.long_press && enabled.load())
		{
			set_open(!open, "menu button");
		}
		if (events.short_press)
		{
			if (open)
			{
				set_open(false, "menu button");
			}
			else
			{
				pass_start_until = GetTickCount64() + start_pulse_ms;
			}
		}

		const stick_chord_events chord_events = chord.update((pad.buttons & XINPUT_GAMEPAD_LEFT_THUMB) != 0, (pad.buttons & XINPUT_GAMEPAD_RIGHT_THUMB) != 0, seconds);
		if (stick_menu.load())
		{
			if (chord_events.started)
			{
				input::haptic(0, input::pulses::tick);
				input::haptic(1, input::pulses::tick);
			}
			if (chord_events.fired)
			{
				set_open(!open, "both sticks");
			}
		}
		sticks_to_game = game_sticks(chord.to_game(0), chord.to_game(1));

		if (open && !enabled.load())
		{
			set_open(false, "vr_panel off");
		}

		hint_shown = hint.update(safety::campaign_level_loaded(), seconds) && enabled.load() && store.boolean("menu_hint", true) && !open;
		if (focus_lost.load() && store.boolean("focus_notice", true))
		{
			show_notice("SteamVR has the controllers: close its menu", true);
		}
		else if (hint_shown)
		{
			show_notice(stick_menu.load() ? "Hold both sticks: VR settings" : "Hold the left menu button: VR settings", false);
		}
		else
		{
			show_notice({}, false);
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
			input::haptic(pointer_hand, input::pulses::click);
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

	bool vr_notice_shown()
	{
		return notice_on.load(std::memory_order_relaxed);
	}

	bool vr_notice_pixels(std::uint64_t& revision, std::vector<std::uint32_t>& pixels)
	{
		const std::lock_guard guard(lock);
		if (notice_image.empty() || notice_revision == revision)
		{
			return false;
		}
		pixels = notice_image;
		revision = notice_revision;
		return true;
	}

	void set_input_focus_lost(bool lost)
	{
		focus_lost = lost;
	}

	float menu_hold_seconds()
	{
		const std::lock_guard guard(lock);
		return button.held();
	}

	input::gamepad_state filter_panel_pad(const input::gamepad_state& pad)
	{
		if (!enabled.load(std::memory_order_relaxed))
		{
			return pad;
		}
		input::gamepad_state out = pad;
		const std::uint16_t raw = input::latest().buttons;
		if (stick_menu.load(std::memory_order_relaxed))
		{
			// Clicks that are down but may still become a chord wait; a late tap is added.
			const std::uint16_t down = game_sticks((raw & XINPUT_GAMEPAD_LEFT_THUMB) != 0, (raw & XINPUT_GAMEPAD_RIGHT_THUMB) != 0);
			const std::uint16_t pass = sticks_to_game.load(std::memory_order_relaxed) & stick_clicks;
			out.buttons &= static_cast<std::uint16_t>(~(down & ~pass));
			out.buttons |= static_cast<std::uint16_t>(pass & ~down);
		}
		if (panel_open.load(std::memory_order_relaxed))
		{
			out = {};
			out.connected = pad.connected;
		}
		// Only the menu button's own Start waits for its release; a Start mapped from another button
		// (x_menu: left X) passes straight through.
		if (raw & XINPUT_GAMEPAD_START)
		{
			out.buttons &= static_cast<std::uint16_t>(~XINPUT_GAMEPAD_START);
		}
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
			set_open(command == "open", "pipe");
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
		char text[512];
		std::snprintf(text, sizeof(text),
			"panel open=%d enabled=%d page=%d first_row=%d hover=%d/%d image_revision=%llu pose=(%.2f %.2f %.2f) status=%s\n"
			"openers menu_held=%.2f stick_menu=%d chord=%d chord_held=%.2f sticks_to_game=%04X hint=%d input_focus=%d notice=%s",
			open ? 1 : 0, enabled.load() ? 1 : 0, panel.current_page(), panel.first_row(), static_cast<int>(panel.hovered().kind), panel.hovered().index,
			static_cast<unsigned long long>(image_revision), pose.position[0], pose.position[1], pose.position[2], status.empty() ? "-" : status.c_str(),
			button.held(), stick_menu.load() ? 1 : 0, chord.chord() ? 1 : 0, chord.held(), sticks_to_game.load(), hint_shown ? 1 : 0,
			focus_lost.load() ? 0 : 1, notice_text.empty() ? "-" : notice_text.c_str());
		return text;
	}
}
