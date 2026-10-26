#include "hooks/dxgi_probe.hpp"

#include <dxgi1_2.h>
#include <wrl/client.h>

#include <MinHook.h>

#include "common/log.hpp"

using Microsoft::WRL::ComPtr;

namespace bfvr::hooks
{
	namespace
	{
		// Vtable slots: IUnknown 0-2, IDXGIObject 3-6, IDXGIFactory 7-11, IDXGIFactory1 12-13, IDXGIFactory2 14+.
		// CreateSwapChain (10) belongs to the swap chain size hooks, which log it too.
		constexpr int is_windowed_stereo_enabled_slot = 14;
		constexpr int create_swap_chain_for_hwnd_slot = 15;
		constexpr int register_stereo_status_window_slot = 18;

		using is_windowed_stereo_enabled_t = BOOL(STDMETHODCALLTYPE*)(IDXGIFactory2*);
		using create_swap_chain_for_hwnd_t = HRESULT(STDMETHODCALLTYPE*)(IDXGIFactory2*, IUnknown*, HWND,
			const DXGI_SWAP_CHAIN_DESC1*, const DXGI_SWAP_CHAIN_FULLSCREEN_DESC*, IDXGIOutput*, IDXGISwapChain1**);
		using register_stereo_status_window_t = HRESULT(STDMETHODCALLTYPE*)(IDXGIFactory2*, HWND, UINT, DWORD*);

		is_windowed_stereo_enabled_t original_is_windowed_stereo_enabled{};
		create_swap_chain_for_hwnd_t original_create_swap_chain_for_hwnd{};
		register_stereo_status_window_t original_register_stereo_status_window{};

		BOOL STDMETHODCALLTYPE is_windowed_stereo_enabled_detour(IDXGIFactory2* factory)
		{
			const BOOL enabled = original_is_windowed_stereo_enabled(factory);
			log::write("dxgi: IsWindowedStereoEnabled -> %d", enabled);
			return enabled;
		}

		HRESULT STDMETHODCALLTYPE create_swap_chain_for_hwnd_detour(IDXGIFactory2* factory, IUnknown* device, HWND window,
			const DXGI_SWAP_CHAIN_DESC1* desc, const DXGI_SWAP_CHAIN_FULLSCREEN_DESC* fullscreen, IDXGIOutput* output, IDXGISwapChain1** swap_chain)
		{
			const HRESULT result = original_create_swap_chain_for_hwnd(factory, device, window, desc, fullscreen, output, swap_chain);
			if (desc)
			{
				log::write("dxgi: CreateSwapChainForHwnd %ux%u format=%d stereo=%d buffers=%u effect=%d -> 0x%08lX",
					desc->Width, desc->Height, desc->Format, desc->Stereo, desc->BufferCount, desc->SwapEffect, result);
			}
			return result;
		}

		HRESULT STDMETHODCALLTYPE register_stereo_status_window_detour(IDXGIFactory2* factory, HWND window, UINT message, DWORD* cookie)
		{
			log::write("dxgi: RegisterStereoStatusWindow message=0x%x", message);
			return original_register_stereo_status_window(factory, window, message, cookie);
		}

		bool hook(void* target, void* detour, void** original, const char* name)
		{
			MH_STATUS status = MH_CreateHook(target, detour, original);
			if (status == MH_OK)
			{
				status = MH_EnableHook(target);
			}
			if (status != MH_OK && status != MH_ERROR_ALREADY_CREATED)
			{
				log::write("dxgi: hooking %s failed: %s", name, MH_StatusToString(status));
				return false;
			}
			return true;
		}
	}

	bool install_dxgi_stereo_probe()
	{
		const MH_STATUS status = MH_Initialize();
		if (status != MH_OK && status != MH_ERROR_ALREADY_INITIALIZED)
		{
			return false;
		}

		// Every factory version shares one implementation, so hooking our factory's methods covers the game's.
		ComPtr<IDXGIFactory2> factory;
		if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))))
		{
			log::write("dxgi: probe needs IDXGIFactory2 (Windows 8+)");
			return false;
		}
		void** const vtable = *reinterpret_cast<void***>(factory.Get());

		const bool ok = hook(vtable[is_windowed_stereo_enabled_slot], reinterpret_cast<void*>(&is_windowed_stereo_enabled_detour),
				reinterpret_cast<void**>(&original_is_windowed_stereo_enabled), "IsWindowedStereoEnabled")
			&& hook(vtable[create_swap_chain_for_hwnd_slot], reinterpret_cast<void*>(&create_swap_chain_for_hwnd_detour),
				reinterpret_cast<void**>(&original_create_swap_chain_for_hwnd), "CreateSwapChainForHwnd")
			&& hook(vtable[register_stereo_status_window_slot], reinterpret_cast<void*>(&register_stereo_status_window_detour),
				reinterpret_cast<void**>(&original_register_stereo_status_window), "RegisterStereoStatusWindow");
		log::write("dxgi: stereo probe %s", ok ? "installed" : "incomplete");
		return ok;
	}
}
