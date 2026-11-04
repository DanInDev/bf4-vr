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
			const std::uint32_t mask = hidden.load(std::memory_order_relaxed);
			if (!visible || mask == 0)
			{
				return visible;
			}
			const int element = hud_widgets::element_of(*static_cast<const std::uintptr_t*>(widget));
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
