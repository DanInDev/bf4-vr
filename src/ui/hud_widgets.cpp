#include "ui/hud_widgets.hpp"

#include <windows.h>

#include <atomic>
#include <cstdio>
#include <cstring>

#include <MinHook.h>

#include "common/log.hpp"
#include "safety/mp_guard.hpp"
#include "ui/hud_widget_table.hpp"

namespace bfvr::ui
{
	namespace
	{
		std::atomic<std::uint32_t> hidden{};
		std::atomic<bool> active{};
		std::atomic<std::uint64_t> calls{};
		std::atomic<std::uint64_t> hidden_answers{};
		std::atomic<const char*> failure{ "not installed" };

		// Research (pipe `hudwidgets classes` / `hide <vtable>`): the distinct vtables that asked isVisible,
		// with their visible answers, and one extra vtable to hide regardless of the settings.
		constexpr int seen_capacity = 128;
		struct seen_entry
		{
			std::atomic<std::uint64_t> vtable{};
			std::atomic<std::uint32_t> visible{};
		};
		seen_entry seen[seen_capacity];
		std::atomic<bool> collecting{};
		std::atomic<std::uint64_t> extra_hidden{};
		std::atomic<std::uint64_t> instances_of{}; // nonzero: record widget addresses of this vtable instead

		void record(std::uint64_t vtable, bool visible) // a widget address in instance mode
		{
			for (seen_entry& entry : seen)
			{
				std::uint64_t expected = 0;
				if (entry.vtable.load(std::memory_order_relaxed) == vtable || entry.vtable.compare_exchange_strong(expected, vtable))
				{
					if (visible)
					{
						entry.visible.fetch_add(1, std::memory_order_relaxed);
					}
					return;
				}
			}
		}
	}

#if defined(_M_X64)
	namespace
	{
		// push rbx (REX); sub rsp,20h; cmp qword ptr [rcx+20h],0; mov rbx,rcx; je +20h
		constexpr std::uint8_t expected_prologue[] = { 0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x83, 0x79, 0x20, 0x00, 0x48, 0x8B, 0xD9, 0x74, 0x20 };

		using is_visible_t = bool(__fastcall*)(void* widget);
		is_visible_t original{};

		bool __fastcall is_visible_detour(void* widget)
		{
			const bool visible = original(widget);
			calls.fetch_add(1, std::memory_order_relaxed);
			const std::uint64_t vtable = *static_cast<const std::uintptr_t*>(widget);
			if (collecting.load(std::memory_order_relaxed))
			{
				const std::uint64_t filter = instances_of.load(std::memory_order_relaxed);
				if (!filter)
				{
					record(vtable, visible);
				}
				else if (vtable == filter)
				{
					record(reinterpret_cast<std::uintptr_t>(widget), visible);
				}
			}
			if (visible && vtable == extra_hidden.load(std::memory_order_relaxed))
			{
				hidden_answers.fetch_add(1, std::memory_order_relaxed);
				return false;
			}
			const std::uint32_t mask = hidden.load(std::memory_order_relaxed);
			if (!visible || mask == 0)
			{
				return visible;
			}
			int element = hud_widgets::element_of(vtable);
			if (element < 0 && (mask & hud_widgets::screen_elements()))
			{
				const auto screen = *reinterpret_cast<const std::uintptr_t*>(static_cast<const std::uint8_t*>(widget) + hud_widgets::widget_screen_offset);
				if (screen)
				{
					element = hud_widgets::element_of_screen(*reinterpret_cast<const std::uintptr_t*>(screen));
				}
			}
			if (element >= 0 && (mask & (1u << element)))
			{
				hidden_answers.fetch_add(1, std::memory_order_relaxed);
				return false;
			}
			return true;
		}

		bool read(std::uintptr_t address, void* out, std::size_t size)
		{
			return ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<LPCVOID>(address), out, size, nullptr) != FALSE;
		}

		// Every listed class must still use the hooked function, or hiding it would do nothing.
		bool vtables_match()
		{
			for (const hud_widgets::widget_class& entry : hud_widgets::classes())
			{
				std::uintptr_t slot = 0;
				if (!read(entry.vtable + hud_widgets::is_visible_slot * sizeof(void*), &slot, sizeof(slot)) || slot != hud_widgets::base_is_visible)
				{
					log::write("hud_widgets: %s vtable %p slot 30 is %p, not the base isVisible", entry.name,
						reinterpret_cast<void*>(entry.vtable), reinterpret_cast<void*>(slot));
					return false;
				}
			}
			// The world icons screen class (not reflected): its first slot as in the dump.
			std::uintptr_t first = 0;
			if (!read(0x141BFBAD0, &first, sizeof(first)) || first != 0x1408C9BD0)
			{
				log::write("hud_widgets: world icons screen vtable slot 0 is %p", reinterpret_cast<void*>(first));
				return false;
			}
			return true;
		}

		DWORD WINAPI wait_and_hook(LPVOID)
		{
			auto* const target = reinterpret_cast<void*>(hud_widgets::base_is_visible);
			std::uint8_t bytes[sizeof(expected_prologue)]{};
			for (int attempt = 0; attempt < 600; ++attempt)
			{
				if (safety::mod_disabled())
				{
					failure = "mod disabled";
					return 0;
				}
				if (read(hud_widgets::base_is_visible, bytes, sizeof(bytes)) && std::memcmp(bytes, expected_prologue, sizeof(bytes)) == 0)
				{
					if (!vtables_match())
					{
						failure = "vtable mismatch";
						return 0;
					}
					MH_STATUS status = MH_Initialize();
					if (status == MH_OK || status == MH_ERROR_ALREADY_INITIALIZED)
					{
						status = MH_CreateHook(target, reinterpret_cast<void*>(&is_visible_detour), reinterpret_cast<void**>(&original));
						if (status == MH_OK)
						{
							status = MH_EnableHook(target);
						}
					}
					active = status == MH_OK;
					failure = active ? "" : "MinHook failed";
					log::write("hud_widgets: isVisible hook at %p: %s", target, MH_StatusToString(status));
					return 0;
				}
				Sleep(200);
			}
			failure = "isVisible never matched build 179547";
			log::write("hud_widgets: %s; HUD element toggles off", failure.load());
			return 0;
		}
	}

	void install_hud_widgets()
	{
		const HANDLE thread = CreateThread(nullptr, 0, &wait_and_hook, nullptr, 0, nullptr);
		if (thread)
		{
			CloseHandle(thread);
		}
	}
#else
	void install_hud_widgets()
	{
		failure = "BF4 only";
	}
#endif

	void set_hud_widgets_hidden(std::uint32_t mask)
	{
		hidden = mask;
	}

	std::string hud_widgets_command(const std::string& argument)
	{
		unsigned long long filter = 0;
		const bool instances = sscanf_s(argument.c_str(), "instances %llx", &filter) == 1;
		if (argument.rfind("classes", 0) == 0 || instances)
		{
			for (seen_entry& entry : seen)
			{
				entry.vtable = 0;
				entry.visible = 0;
			}
			instances_of = instances ? filter : 0;
			collecting = true;
			Sleep(1000);
			collecting = false;
			std::string reply = instances ? "widget data visible_per_s:" : "vtable visible_per_s:";
			for (const seen_entry& entry : seen)
			{
				if (!entry.vtable)
				{
					break;
				}
				char line[80];
				if (instances)
				{
					std::uint64_t data = 0;
					ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<LPCVOID>(entry.vtable.load() + 0x28), &data, sizeof(data), nullptr);
					std::snprintf(line, sizeof(line), "\n0x%llx 0x%llx %u", static_cast<unsigned long long>(entry.vtable.load()),
						static_cast<unsigned long long>(data), entry.visible.load());
				}
				else
				{
					std::snprintf(line, sizeof(line), "\n0x%llx %u", static_cast<unsigned long long>(entry.vtable.load()), entry.visible.load());
				}
				reply += line;
			}
			return reply;
		}
		unsigned long long vtable = 0;
		if (sscanf_s(argument.c_str(), "hide %llx", &vtable) == 1)
		{
			extra_hidden = vtable;
			return "extra hidden vtable " + std::to_string(vtable);
		}
		return hud_widgets_report();
	}

	std::string hud_widgets_report()
	{
		const bool on = active.load();
		char text[160];
		std::snprintf(text, sizeof(text), "hud_widgets active=%d%s%s hidden_mask=0x%x calls=%llu hidden_answers=%llu",
			on ? 1 : 0, on ? "" : " reason=", on ? "" : failure.load(), hidden.load(),
			static_cast<unsigned long long>(calls.exchange(0)), static_cast<unsigned long long>(hidden_answers.exchange(0)));
		return text;
	}
}
