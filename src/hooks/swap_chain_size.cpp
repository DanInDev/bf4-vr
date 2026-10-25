#include "hooks/swap_chain_size.hpp"

#include <dxgi.h>
#include <intrin.h>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

#include <MinHook.h>

#include "common/config.hpp"
#include "common/log.hpp"
#include "hooks/game_window.hpp"

namespace bfvr::hooks
{
	namespace
	{
		// Vtable slots: IUnknown 0-2, IDXGIObject 3-6, IDXGIFactory 7-11 (CreateSwapChain 10);
		// IDXGIDeviceSubObject 7, IDXGISwapChain 8-17; IDXGIOutput GetDesc 7, GetDisplayModeList 8,
		// FindClosestMatchingMode 9.
		constexpr int create_swap_chain_slot = 10;
		constexpr int set_fullscreen_state_slot = 10;
		constexpr int get_fullscreen_state_slot = 11;
		constexpr int get_desc_slot = 12;
		constexpr int resize_buffers_slot = 13;
		constexpr int resize_target_slot = 14;
		constexpr int get_display_mode_list_slot = 8;
		constexpr int find_closest_matching_mode_slot = 9;

		using create_swap_chain_t = HRESULT(STDMETHODCALLTYPE*)(IDXGIFactory*, IUnknown*, DXGI_SWAP_CHAIN_DESC*, IDXGISwapChain**);
		using set_fullscreen_state_t = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain*, BOOL, IDXGIOutput*);
		using get_desc_t = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain*, DXGI_SWAP_CHAIN_DESC*);
		using get_fullscreen_state_t = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain*, BOOL*, IDXGIOutput**);
		using resize_buffers_t = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain*, UINT, UINT, UINT, DXGI_FORMAT, UINT);
		using resize_target_t = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain*, const DXGI_MODE_DESC*);
		using get_display_mode_list_t = HRESULT(STDMETHODCALLTYPE*)(IDXGIOutput*, DXGI_FORMAT, UINT, UINT*, DXGI_MODE_DESC*);
		using find_closest_matching_mode_t = HRESULT(STDMETHODCALLTYPE*)(IDXGIOutput*, const DXGI_MODE_DESC*, DXGI_MODE_DESC*, IUnknown*);

		create_swap_chain_t original_create_swap_chain{};
		set_fullscreen_state_t original_set_fullscreen_state{};
		get_fullscreen_state_t original_get_fullscreen_state{};
		get_desc_t original_get_desc{};
		resize_buffers_t original_resize_buffers{};
		resize_target_t original_resize_target{};
		get_display_mode_list_t original_get_display_mode_list{};
		find_closest_matching_mode_t original_find_closest_matching_mode{};

		unsigned mode_width{};
		unsigned mode_height{};
		std::atomic<bool> virtual_fullscreen{};
		std::atomic<unsigned> buffer_width{};
		std::atomic<unsigned> buffer_height{};
		std::atomic<unsigned long long> resizes{};
		std::atomic<unsigned long long> skipped_resizes{};
		std::atomic<unsigned long long> find_calls{};

		bool overriding() { return mode_width != 0; }

#if defined(_M_X64)
		// BF4 build 179547: bool find_mode(list, DXGI_MODE_DESC* mode, DXGI_FORMAT, float refresh_hz,
		// UINT width, UINT height). Every frame the renderer looks its current mode up in its cached
		// display mode list (up to 100 entries) and, when it is missing, applies the mode again
		// (FindClosestMatchingMode, ResizeTarget, ResizeBuffers): re/notes/render-quality.md.
		constexpr std::uintptr_t find_mode_address = 0x140be4eb0;
		// mov [rsp+18h],rbp; mov [rsp+20h],rsi; push rdi; sub rsp,40h; cmp byte ptr [rcx+18h],0
		constexpr std::uint8_t find_mode_prologue[] = { 0x48, 0x89, 0x6c, 0x24, 0x18, 0x48, 0x89, 0x74, 0x24, 0x20, 0x57,
			0x48, 0x83, 0xec, 0x40, 0x80, 0x79, 0x18, 0x00 };
		using find_mode_t = bool(__fastcall*)(void* list, DXGI_MODE_DESC* mode, DXGI_FORMAT format, float refresh_hz, UINT width, UINT height);
		find_mode_t original_find_mode{};

		bool __fastcall find_mode_detour(void* list, DXGI_MODE_DESC* mode, DXGI_FORMAT format, float refresh_hz, UINT width, UINT height)
		{
			if (original_find_mode(list, mode, format, refresh_hz, width, height))
			{
				return true;
			}
			if (overriding() && width == mode_width && height == mode_height && mode)
			{
				mode->Width = width;
				mode->Height = height;
				mode->Format = format;
				return true;
			}
			return false;
		}
#endif

		// Our own throwaway swap chains (present hook probe) are left alone.
		bool is_game_window(HWND window)
		{
			DWORD process{};
			GetWindowThreadProcessId(window, &process);
			wchar_t name[64]{};
			GetClassNameW(window, name, 64);
			return process == GetCurrentProcessId() && wcsncmp(name, L"bfvr_", 5) != 0;
		}

		HRESULT real_desc(IDXGISwapChain* swap_chain, DXGI_SWAP_CHAIN_DESC& desc)
		{
			return original_get_desc ? original_get_desc(swap_chain, &desc) : swap_chain->GetDesc(&desc);
		}

		bool is_game_swap_chain(IDXGISwapChain* swap_chain)
		{
			DXGI_SWAP_CHAIN_DESC desc{};
			return SUCCEEDED(real_desc(swap_chain, desc)) && is_game_window(desc.OutputWindow);
		}

		// Every frame the game looks its current mode up in the mode list (only the first 100 entries,
		// 0x140BE4EB0) and applies the mode again when it is missing. The virtual modes, one per
		// refresh rate and scaling of the largest real mode, come first.
		HRESULT STDMETHODCALLTYPE get_display_mode_list_detour(IDXGIOutput* output, DXGI_FORMAT format, UINT flags, UINT* count, DXGI_MODE_DESC* modes)
		{
			if (!overriding() || !count)
			{
				return original_get_display_mode_list(output, format, flags, count, modes);
			}
			UINT real_count = 0;
			HRESULT result = original_get_display_mode_list(output, format, flags, &real_count, nullptr);
			if (FAILED(result) || real_count == 0)
			{
				return original_get_display_mode_list(output, format, flags, count, modes);
			}
			std::vector<DXGI_MODE_DESC> real(real_count);
			result = original_get_display_mode_list(output, format, flags, &real_count, real.data());
			if (FAILED(result))
			{
				return result;
			}
			real.resize(real_count);

			UINT largest_width = 0;
			UINT largest_height = 0;
			for (const DXGI_MODE_DESC& mode : real)
			{
				if (mode.Width * mode.Height > largest_width * largest_height)
				{
					largest_width = mode.Width;
					largest_height = mode.Height;
				}
			}
			std::vector<DXGI_MODE_DESC> all;
			for (const DXGI_MODE_DESC& mode : real)
			{
				if (mode.Width == largest_width && mode.Height == largest_height)
				{
					DXGI_MODE_DESC virtual_mode = mode;
					virtual_mode.Width = mode_width;
					virtual_mode.Height = mode_height;
					all.push_back(virtual_mode);
				}
			}
			all.insert(all.end(), real.begin(), real.end());

			const UINT total = static_cast<UINT>(all.size());
			if (!modes)
			{
				*count = total;
				return S_OK;
			}
			const UINT copied = std::min(*count, total);
			std::copy_n(all.begin(), copied, modes);
			*count = copied;
			return copied < total ? DXGI_ERROR_MORE_DATA : S_OK;
		}

		// The game asks for the display mode closest to its resolution setting; with render_size it
		// gets that size as if the monitor offered it.
		HRESULT STDMETHODCALLTYPE find_closest_matching_mode_detour(IDXGIOutput* output, const DXGI_MODE_DESC* wanted, DXGI_MODE_DESC* closest, IUnknown* device)
		{
			const HRESULT result = original_find_closest_matching_mode(output, wanted, closest, device);
			if (SUCCEEDED(result) && wanted && closest)
			{
				const unsigned found_width = closest->Width;
				const unsigned found_height = closest->Height;
				if (overriding())
				{
					closest->Width = mode_width;
					closest->Height = mode_height;
				}
				if (find_calls++ < 8)
				{
					log::write("dxgi: FindClosestMatchingMode %ux%u -> %ux%u (display %ux%u) from %p", wanted->Width, wanted->Height,
						closest->Width, closest->Height, found_width, found_height, _ReturnAddress());
				}
			}
			return result;
		}

		// Exclusive fullscreen at a size the monitor cannot show is impossible, so the swap chain stays
		// windowed and only the game believes it is fullscreen. This also keeps alt-tab and the SteamVR
		// dashboard from switching display modes.
		HRESULT STDMETHODCALLTYPE set_fullscreen_state_detour(IDXGISwapChain* swap_chain, BOOL fullscreen, IDXGIOutput* output)
		{
			if (overriding() && is_game_swap_chain(swap_chain))
			{
				virtual_fullscreen = fullscreen != FALSE;
				set_virtual_client_size(fullscreen ? mode_width : 0, fullscreen ? mode_height : 0);
				log::write("dxgi: SetFullscreenState %d (virtual)", fullscreen);
				return S_OK;
			}
			const HRESULT result = original_set_fullscreen_state(swap_chain, fullscreen, output);
			log::write("dxgi: SetFullscreenState %d -> 0x%08lX", fullscreen, result);
			return result;
		}

		// The game checks every frame that its swap chain is in the state it asked for, and applies its
		// mode again if not.
		HRESULT STDMETHODCALLTYPE get_desc_detour(IDXGISwapChain* swap_chain, DXGI_SWAP_CHAIN_DESC* desc)
		{
			const HRESULT result = original_get_desc(swap_chain, desc);
			if (SUCCEEDED(result) && desc && virtual_fullscreen.load() && overriding() && is_game_window(desc->OutputWindow))
			{
				desc->Windowed = FALSE;
			}
			return result;
		}

		HRESULT STDMETHODCALLTYPE get_fullscreen_state_detour(IDXGISwapChain* swap_chain, BOOL* fullscreen, IDXGIOutput** target)
		{
			const HRESULT result = original_get_fullscreen_state(swap_chain, fullscreen, target);
			if (SUCCEEDED(result) && overriding() && is_game_swap_chain(swap_chain))
			{
				if (fullscreen)
				{
					*fullscreen = virtual_fullscreen.load() ? TRUE : FALSE;
				}
				if (target && !*target && virtual_fullscreen.load())
				{
					swap_chain->GetContainingOutput(target);
				}
			}
			return result;
		}

		// A mode larger than the monitor would make the window larger than the screen; the window keeps
		// to the monitor and DXGI scales the back buffer into it (the monitor image is only a preview).
		HRESULT STDMETHODCALLTYPE resize_target_detour(IDXGISwapChain* swap_chain, const DXGI_MODE_DESC* mode)
		{
			if (!mode || !overriding() || !is_game_swap_chain(swap_chain))
			{
				return original_resize_target(swap_chain, mode);
			}
			DXGI_SWAP_CHAIN_DESC desc{};
			real_desc(swap_chain, desc);
			MONITORINFO monitor{ sizeof(monitor) };
			GetMonitorInfoW(MonitorFromWindow(desc.OutputWindow, MONITOR_DEFAULTTOPRIMARY), &monitor);
			const RECT& screen = monitor.rcMonitor;
			const bool larger = mode->Width > static_cast<UINT>(screen.right - screen.left) || mode->Height > static_cast<UINT>(screen.bottom - screen.top);
			if (!virtual_fullscreen.load() && !larger)
			{
				return original_resize_target(swap_chain, mode); // an ordinary window size
			}
			// The window covers the monitor; every further ResizeTarget would only send window messages
			// that make the game apply its mode again, every frame.
			// GetWindowRect: GetClientRect answers with the virtual size (game_window.cpp).
			RECT placed{};
			GetWindowRect(desc.OutputWindow, &placed);
			if (EqualRect(&placed, &screen) && (GetWindowLongPtrW(desc.OutputWindow, GWL_STYLE) & WS_POPUP))
			{
				return S_OK;
			}
			SetWindowLongPtrW(desc.OutputWindow, GWL_STYLE, WS_POPUP | WS_VISIBLE);
			const BOOL moved = SetWindowPos(desc.OutputWindow, HWND_TOP, screen.left, screen.top, screen.right - screen.left,
				screen.bottom - screen.top, SWP_FRAMECHANGED | SWP_NOACTIVATE);
			const HRESULT result = moved ? S_OK : DXGI_ERROR_INVALID_CALL;
			log::write("dxgi: ResizeTarget %ux%u -> borderless window %ldx%ld -> 0x%08lX", mode->Width, mode->Height,
				screen.right - screen.left, screen.bottom - screen.top, result);
			return result;
		}

		HRESULT STDMETHODCALLTYPE resize_buffers_detour(IDXGISwapChain* swap_chain, UINT count, UINT width, UINT height, DXGI_FORMAT format, UINT flags)
		{
			DXGI_SWAP_CHAIN_DESC before{};
			const bool game = SUCCEEDED(real_desc(swap_chain, before)) && is_game_window(before.OutputWindow);
			// In virtual fullscreen the game applies its mode again every frame (it does not find the
			// virtual mode in its cached display mode list). Resizing to the same size is a no-op, so
			// the buffers are kept instead of being recreated each frame.
			if (game && overriding() && width == before.BufferDesc.Width && height == before.BufferDesc.Height
				&& (count == 0 || count == before.BufferCount) && (format == DXGI_FORMAT_UNKNOWN || format == before.BufferDesc.Format)
				&& flags == before.Flags)
			{
				if (skipped_resizes++ < 4)
				{
					log::write("dxgi: same-size ResizeBuffers %ux%u skipped, from %p", width, height, _ReturnAddress());
				}
				return S_OK;
			}
			const HRESULT result = original_resize_buffers(swap_chain, count, width, height, format, flags);
			DXGI_SWAP_CHAIN_DESC desc{};
			if (game && SUCCEEDED(real_desc(swap_chain, desc)))
			{
				buffer_width = desc.BufferDesc.Width;
				buffer_height = desc.BufferDesc.Height;
				++resizes;
				log::write("dxgi: ResizeBuffers %ux%u -> 0x%08lX (windowed=%d, game fullscreen=%d)", width, height, result,
					desc.Windowed, virtual_fullscreen.load() ? 1 : 0);
			}
			return result;
		}

		bool hook(void* target, void* detour, void** original, const char* name)
		{
			MH_STATUS status = MH_CreateHook(target, detour, original);
			if (status == MH_OK)
			{
				status = MH_EnableHook(target);
			}
			if (status != MH_OK)
			{
				log::write("dxgi: hooking %s failed: %s", name, MH_StatusToString(status));
				return false;
			}
			return true;
		}

		// Called at the game's first swap chain: by then the DRM has decrypted the code.
		void hook_mode_lookup()
		{
#if defined(_M_X64)
			std::uint8_t bytes[sizeof(find_mode_prologue)]{};
			auto* const find_mode = reinterpret_cast<void*>(find_mode_address);
			if (overriding() && ReadProcessMemory(GetCurrentProcess(), find_mode, bytes, sizeof(bytes), nullptr)
				&& std::memcmp(bytes, find_mode_prologue, sizeof(bytes)) == 0)
			{
				hook(find_mode, reinterpret_cast<void*>(&find_mode_detour), reinterpret_cast<void**>(&original_find_mode), "find_mode");
			}
			else if (overriding())
			{
				log::write("dxgi: mode lookup %p does not match build 179547; the game re-applies render_size every frame", find_mode);
			}
#endif
		}

		void hook_swap_chain(IDXGISwapChain* swap_chain)
		{
			void** const vtable = *reinterpret_cast<void***>(swap_chain);
			hook(vtable[set_fullscreen_state_slot], reinterpret_cast<void*>(&set_fullscreen_state_detour),
				reinterpret_cast<void**>(&original_set_fullscreen_state), "SetFullscreenState");
			hook(vtable[get_fullscreen_state_slot], reinterpret_cast<void*>(&get_fullscreen_state_detour),
				reinterpret_cast<void**>(&original_get_fullscreen_state), "GetFullscreenState");
			hook(vtable[get_desc_slot], reinterpret_cast<void*>(&get_desc_detour),
				reinterpret_cast<void**>(&original_get_desc), "GetDesc");
			hook(vtable[resize_buffers_slot], reinterpret_cast<void*>(&resize_buffers_detour),
				reinterpret_cast<void**>(&original_resize_buffers), "ResizeBuffers");
			hook(vtable[resize_target_slot], reinterpret_cast<void*>(&resize_target_detour),
				reinterpret_cast<void**>(&original_resize_target), "ResizeTarget");


			IDXGIOutput* output{};
			if (SUCCEEDED(swap_chain->GetContainingOutput(&output)))
			{
				hook((*reinterpret_cast<void***>(output))[get_display_mode_list_slot], reinterpret_cast<void*>(&get_display_mode_list_detour),
					reinterpret_cast<void**>(&original_get_display_mode_list), "GetDisplayModeList");
				hook((*reinterpret_cast<void***>(output))[find_closest_matching_mode_slot], reinterpret_cast<void*>(&find_closest_matching_mode_detour),
					reinterpret_cast<void**>(&original_find_closest_matching_mode), "FindClosestMatchingMode");
				output->Release();
			}
			else
			{
				log::write("dxgi: no output for the first swap chain; render_size inactive");
			}
		}

		HRESULT STDMETHODCALLTYPE create_swap_chain_detour(IDXGIFactory* factory, IUnknown* device, DXGI_SWAP_CHAIN_DESC* desc, IDXGISwapChain** swap_chain)
		{
			const bool game = desc && is_game_window(desc->OutputWindow);
			if (game && overriding() && !desc->Windowed)
			{
				desc->Windowed = TRUE;
				virtual_fullscreen = true;
			}
			const HRESULT result = original_create_swap_chain(factory, device, desc, swap_chain);
			if (desc)
			{
				log::write("dxgi: CreateSwapChain %ux%u format=%d buffers=%u effect=%d windowed=%d%s -> 0x%08lX",
					desc->BufferDesc.Width, desc->BufferDesc.Height, desc->BufferDesc.Format, desc->BufferCount,
					desc->SwapEffect, desc->Windowed, game ? "" : " (not the game's)", result);
			}
			if (SUCCEEDED(result) && game && swap_chain && *swap_chain)
			{
				attach_game_window(desc->OutputWindow);
				if (virtual_fullscreen.load())
				{
					set_virtual_client_size(mode_width, mode_height);
				}
				static bool mode_lookup_hooked = false;
				if (!mode_lookup_hooked && overriding())
				{
					mode_lookup_hooked = true;
					hook_mode_lookup();
				}
				buffer_width = desc->BufferDesc.Width;
				buffer_height = desc->BufferDesc.Height;
			}
			// Every swap chain shares one implementation, so the first one created (our own Present
			// probe's, on the primary monitor) reveals the methods the game's will call.
			if (SUCCEEDED(result) && swap_chain && *swap_chain && !original_resize_buffers)
			{
				hook_swap_chain(*swap_chain);
			}
			return result;
		}
	}

	bool install_swap_chain_hooks()
	{
		unsigned width{};
		unsigned height{};
		const std::string size = config::get("render_size", "0");
		if ((sscanf_s(size.c_str(), "%ux%u", &width, &height) == 2 || sscanf_s(size.c_str(), "%u %u", &width, &height) == 2)
			&& width >= 640 && height >= 480 && width <= 8192 && height <= 8192)
		{
			mode_width = width;
			mode_height = height;
			log::write("dxgi: render_size %ux%u (virtual fullscreen mode)", width, height);
		}

		const MH_STATUS status = MH_Initialize();
		if (status != MH_OK && status != MH_ERROR_ALREADY_INITIALIZED)
		{
			return false;
		}

		// Every factory version shares one implementation, so hooking our factory covers the game's.
		IDXGIFactory* factory{};
		if (FAILED(CreateDXGIFactory(__uuidof(IDXGIFactory), reinterpret_cast<void**>(&factory))))
		{
			log::write("dxgi: no factory; swap chain hooks inactive");
			return false;
		}
		void* const target = (*reinterpret_cast<void***>(factory))[create_swap_chain_slot];
		factory->Release();
		return hook(target, reinterpret_cast<void*>(&create_swap_chain_detour), reinterpret_cast<void**>(&original_create_swap_chain), "CreateSwapChain");
	}

	std::string render_size_report()
	{
		char text[160];
		std::snprintf(text, sizeof(text), "render_size=%ux%u buffer=%ux%u game_fullscreen=%d resizes=%llu same_size_skipped=%llu mode_queries=%llu",
			mode_width, mode_height, buffer_width.load(), buffer_height.load(), virtual_fullscreen.load() ? 1 : 0, resizes.load(),
			skipped_resizes.load(), find_calls.load());
		return text;
	}
}
