#include "stereo/hud_capture.hpp"

#include <wrl/client.h>

#include <atomic>
#include <cstring>

#include <MinHook.h>

#include "common/config.hpp"
#include "common/log.hpp"
#include "stereo/tracking.hpp"

using Microsoft::WRL::ComPtr;

namespace bfvr::stereo
{
	namespace
	{
		// UI render of the UI render module (0x1401691C9 calls it for module slots 0 and 2): binds the back
		// buffer and draws the Twinkle HUD (draws from 0x140C81BD2), after the world module's tonemap.
		constexpr std::uint64_t ui_render_address = 0x1408863e0;
		// push rbx (REX); push rbp; push r14; sub rsp,60h; cmp byte ptr [rcx+308h],0
		constexpr std::uint8_t ui_render_prologue[] = { 0x40, 0x53, 0x55, 0x41, 0x56, 0x48, 0x83, 0xec, 0x60, 0x80, 0xb9, 0x08,
			0x03, 0x00, 0x00, 0x00 };
		// Six argument slots passed through bit for bit (the 5th/6th are floats on the stack).
		using ui_render_t = void(__fastcall*)(void* module, std::uint64_t slot, void* context, std::uint64_t flags, std::uint64_t a, std::uint64_t b);
		ui_render_t original_ui_render{};

		constexpr int om_set_render_targets_slot = 33;
		using om_set_render_targets_t = void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, UINT, ID3D11RenderTargetView* const*, ID3D11DepthStencilView*);
		om_set_render_targets_t original_om_set_render_targets{};

		bool enabled{};
		bool every_frame{}; // hud_layer = 2: also without head tracking (monitor tests)
		std::atomic<bool> in_hud{};
		std::atomic<ID3D11Resource*> back_buffer{};
		ComPtr<ID3D11Device> device;
		ComPtr<ID3D11Texture2D> texture;
		ComPtr<ID3D11RenderTargetView> view;
		D3D11_TEXTURE2D_DESC texture_desc{};
		std::uint64_t presents{};
		std::uint64_t cleared_for{};
		std::atomic<std::uint64_t> captured_frame{};
		// The UI module drew straight into the image (not a head-tracked frame): pause menu, menus.
		std::atomic<std::uint64_t> uncaptured_frame{};

		void __fastcall ui_render_detour(void* module, std::uint64_t slot, void* context, std::uint64_t flags, std::uint64_t a, std::uint64_t b)
		{
			// Menus and loading screens stay in the image (theater screen); only head-tracked frames
			// move their HUD to the layer.
			const bool capture = enabled && view && (every_frame || latest_rendered_view().stereo);
			in_hud = capture;
			if (!capture)
			{
				uncaptured_frame = presents;
			}
			original_ui_render(module, slot, context, flags, a, b);
			in_hud = false;
		}

		void STDMETHODCALLTYPE om_set_render_targets_detour(ID3D11DeviceContext* context, UINT count, ID3D11RenderTargetView* const* views,
			ID3D11DepthStencilView* depth)
		{
			if (in_hud.load(std::memory_order_relaxed) && count >= 1 && views && views[0])
			{
				ComPtr<ID3D11Resource> resource;
				views[0]->GetResource(&resource);
				if (resource.Get() == back_buffer.load())
				{
					ID3D11RenderTargetView* replaced[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT]{};
					for (UINT i = 0; i < count && i < D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT; ++i)
					{
						replaced[i] = views[i];
					}
					replaced[0] = view.Get();
					if (cleared_for != presents)
					{
						constexpr float transparent[4]{};
						context->ClearRenderTargetView(view.Get(), transparent);
						cleared_for = presents;
						captured_frame = presents;
					}
					original_om_set_render_targets(context, count, replaced, depth);
					return;
				}
			}
			original_om_set_render_targets(context, count, views, depth);
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
				log::write("hud: hooking %s failed: %s", name, MH_StatusToString(status));
				return false;
			}
			return true;
		}
	}

	void install_hud_capture()
	{
		enabled = config::get_bool("hud_layer", false);
		every_frame = config::get("hud_layer", "0") == "2";
	}

	bool hud_capture_enabled()
	{
		return enabled;
	}

	void hud_capture_on_present(IDXGISwapChain* swap_chain)
	{
		if (!enabled)
		{
			return;
		}
		++presents;
		ComPtr<ID3D11Texture2D> buffer;
		if (FAILED(swap_chain->GetBuffer(0, IID_PPV_ARGS(&buffer))))
		{
			return;
		}
		back_buffer = buffer.Get(); // identity only; the swap chain keeps it alive

#if defined(_M_X64)
		if (!original_om_set_render_targets)
		{
			// By the first Present the DRM has decrypted the game's code.
			auto* const ui_render = reinterpret_cast<void*>(static_cast<std::uintptr_t>(ui_render_address));
			std::uint8_t bytes[sizeof(ui_render_prologue)]{};
			if (!ReadProcessMemory(GetCurrentProcess(), ui_render, bytes, sizeof(bytes), nullptr)
				|| std::memcmp(bytes, ui_render_prologue, sizeof(bytes)) != 0)
			{
				// Early Presents can precede the DRM's decryption; a different build never matches.
				if (presents > 1200)
				{
					log::write("hud: UI render %p does not match build 179547; hud_layer off", ui_render);
					enabled = false;
				}
				return;
			}
			if (FAILED(swap_chain->GetDevice(IID_PPV_ARGS(&device))))
			{
				enabled = false;
				return;
			}
			ComPtr<ID3D11DeviceContext> context;
			device->GetImmediateContext(&context);
			void** const vtable = *reinterpret_cast<void***>(context.Get());
			if (!hook(vtable[om_set_render_targets_slot], reinterpret_cast<void*>(&om_set_render_targets_detour),
					reinterpret_cast<void**>(&original_om_set_render_targets), "OMSetRenderTargets")
				|| !hook(ui_render, reinterpret_cast<void*>(&ui_render_detour), reinterpret_cast<void**>(&original_ui_render), "UI render"))
			{
				enabled = false;
				return;
			}
			log::write("hud: capturing the HUD into its own layer");
		}
#endif

		D3D11_TEXTURE2D_DESC desc{};
		buffer->GetDesc(&desc);
		if (!texture || desc.Width != texture_desc.Width || desc.Height != texture_desc.Height || desc.Format != texture_desc.Format)
		{
			// Recreated only between frames, on this (the render) thread.
			view.Reset();
			texture.Reset();
			D3D11_TEXTURE2D_DESC hud_desc = desc;
			hud_desc.Usage = D3D11_USAGE_DEFAULT;
			hud_desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
			hud_desc.CPUAccessFlags = 0;
			hud_desc.MiscFlags = 0;
			hud_desc.ArraySize = 1;
			hud_desc.MipLevels = 1;
			if (FAILED(device->CreateTexture2D(&hud_desc, nullptr, &texture)) || FAILED(device->CreateRenderTargetView(texture.Get(), nullptr, &view)))
			{
				log::write("hud: texture creation failed; hud_layer off");
				view.Reset();
				texture.Reset();
				enabled = false;
				return;
			}
			texture_desc = desc;
			log::write("hud: layer texture %ux%u format %d", desc.Width, desc.Height, desc.Format);
		}
	}

	bool ui_on_screen()
	{
		const std::uint64_t at = uncaptured_frame.load();
		return enabled && at != 0 && presents - at <= 5;
	}

	hud_frame latest_hud()
	{
		return { texture.Get(), captured_frame.load(), uncaptured_frame.load(), presents };
	}
}
