#include "hooks/present_hook.hpp"

#include <d3d11.h>
#include <wrl/client.h>

#include <initializer_list>

#include <MinHook.h>

#include "common/log.hpp"

using Microsoft::WRL::ComPtr;

namespace bfvr::hooks
{
	namespace
	{
		using present_t = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain*, UINT, UINT);

		constexpr int present_vtable_index = 8;

		present_t original_present{};
		present_callback on_present{};

		HRESULT STDMETHODCALLTYPE present_detour(IDXGISwapChain* swap_chain, UINT sync_interval, UINT flags)
		{
			// DXGI_PRESENT_TEST only queries occlusion; nothing is shown, so there is no frame to mirror.
			if (on_present && !(flags & DXGI_PRESENT_TEST))
			{
				on_present(swap_chain);
			}

			return original_present(swap_chain, sync_interval, flags);
		}

		// Every DXGI swap chain shares one implementation, so a throwaway swap chain on a
		// hidden window reveals the Present address the game's swap chain will call.
		void* find_present_address()
		{
			WNDCLASSEXW window_class{};
			window_class.cbSize = sizeof(window_class);
			window_class.lpfnWndProc = DefWindowProcW;
			window_class.hInstance = GetModuleHandleW(nullptr);
			window_class.lpszClassName = L"bfvr_present_probe";
			RegisterClassExW(&window_class);

			const HWND window = CreateWindowExW(0, window_class.lpszClassName, L"", WS_OVERLAPPEDWINDOW,
				0, 0, 64, 64, nullptr, nullptr, window_class.hInstance, nullptr);
			if (!window)
			{
				log::write("present hook: probe window creation failed: %lu", GetLastError());
				return nullptr;
			}

			DXGI_SWAP_CHAIN_DESC description{};
			description.BufferCount = 1;
			description.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
			description.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
			description.OutputWindow = window;
			description.SampleDesc.Count = 1;
			description.Windowed = TRUE;
			description.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

			ComPtr<IDXGISwapChain> swap_chain;
			ComPtr<ID3D11Device> device;
			HRESULT result{};
			for (const D3D_DRIVER_TYPE driver : { D3D_DRIVER_TYPE_HARDWARE, D3D_DRIVER_TYPE_WARP })
			{
				result = D3D11CreateDeviceAndSwapChain(nullptr, driver, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION,
					&description, &swap_chain, &device, nullptr, nullptr);
				if (SUCCEEDED(result))
				{
					break;
				}
			}

			void* present = nullptr;
			if (SUCCEEDED(result))
			{
				present = (*reinterpret_cast<void***>(swap_chain.Get()))[present_vtable_index];
			}
			else
			{
				log::write("present hook: probe device creation failed: hr=0x%08lX", result);
			}

			swap_chain.Reset();
			device.Reset();
			DestroyWindow(window);
			UnregisterClassW(window_class.lpszClassName, window_class.hInstance);
			return present;
		}
	}

	bool install_present_hook(present_callback callback)
	{
		void* const target = find_present_address();
		if (!target)
		{
			return false;
		}

		MH_STATUS status = MH_Initialize();
		if (status != MH_OK && status != MH_ERROR_ALREADY_INITIALIZED)
		{
			log::write("present hook: MH_Initialize failed: %s", MH_StatusToString(status));
			return false;
		}

		on_present = callback;
		status = MH_CreateHook(target, reinterpret_cast<void*>(&present_detour), reinterpret_cast<void**>(&original_present));
		if (status == MH_OK)
		{
			status = MH_EnableHook(target);
		}
		if (status != MH_OK)
		{
			log::write("present hook: hooking %p failed: %s", target, MH_StatusToString(status));
			return false;
		}

		log::write("present hook: installed at %p", target);
		return true;
	}
}
