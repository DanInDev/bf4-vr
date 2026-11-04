#include "stereo/foveation.hpp"

#include <windows.h>
#include <wrl/client.h>

#include <atomic>
#include <cstdio>
#include <cstdint>
#include <mutex>

#include "common/log.hpp"
#include "stereo/foveation_pattern.hpp"
#include "stereo/scope_scissor.hpp"

using Microsoft::WRL::ComPtr;

namespace bfvr::stereo
{
	namespace
	{
		// NVAPI (public SDK, MIT: nvapi.h, nvapi_interface.h), resolved through nvapi_QueryInterface.
		constexpr std::uint32_t id_initialize = 0x0150e828;
		constexpr std::uint32_t id_set_viewports_rates = 0x34f7938f;
		constexpr std::uint32_t id_create_rate_view = 0x99ca2dff;
		constexpr std::uint32_t id_set_rate_view = 0x1b0c2f83;
		constexpr int nv_rate_full = 5;     // NV_PIXEL_X1_PER_RASTER_PIXEL
		constexpr int nv_rate_2x2 = 8;      // NV_PIXEL_X1_PER_2X2_RASTER_PIXELS
		constexpr int nv_rate_4x4 = 11;     // NV_PIXEL_X1_PER_4X4_RASTER_PIXELS
		constexpr int nv_max_rates = 16;    // NV_MAX_PIXEL_SHADING_RATES
		constexpr int nv_srrv_texture2d = 4; // NV_SRRV_DIMENSION_TEXTURE2D

		struct nv_viewport_rate
		{
			bool enable;
			int table[nv_max_rates];
		};
		struct nv_viewports_rates
		{
			std::uint32_t version;
			std::uint32_t count;
			nv_viewport_rate* viewports;
		};
		struct nv_rate_view_desc
		{
			std::uint32_t version;
			DXGI_FORMAT format;
			int dimension;
			std::uint32_t mip_slice;
			std::uint32_t first_array_slice;
			std::uint32_t array_size;
		};
		template <typename T>
		constexpr std::uint32_t nv_version(std::uint32_t v) { return static_cast<std::uint32_t>(sizeof(T)) | (v << 16); }

		using query_interface_t = void*(__cdecl*)(std::uint32_t);
		using initialize_t = int(__cdecl*)();
		using set_viewports_rates_t = int(__cdecl*)(IUnknown*, nv_viewports_rates*);
		using create_rate_view_t = int(__cdecl*)(ID3D11Device*, ID3D11Resource*, nv_rate_view_desc*, IUnknown**);
		using set_rate_view_t = int(__cdecl*)(IUnknown*, IUnknown*);
		set_viewports_rates_t set_viewports_rates{};
		create_rate_view_t create_rate_view{};
		set_rate_view_t set_rate_view{};
		bool tried{};
		bool available{};

		std::mutex lock;
		foveation_pattern::rings rings_now;
		std::uint64_t level_revision{};

		// Render thread.
		struct rate_map
		{
			UINT width{}, height{};
			std::uint64_t revision{};
			ComPtr<ID3D11Texture2D> texture;
			ComPtr<IUnknown> view;
		};
		rate_map maps[4]; // full, half, quarter, eighth
		bool active{};
		bool rates_on{};
		UINT full_width{}, full_height{};
		foveation_pattern::rings rings_frame;
		std::uint64_t revision_frame{};
		std::atomic<std::uint64_t> shaded_targets{};
		std::atomic<std::uint64_t> other_targets{};

		bool load()
		{
			if (tried)
			{
				return available;
			}
			tried = true;
#if defined(_M_X64)
			HMODULE module = GetModuleHandleW(L"nvapi64.dll");
			if (!module)
			{
				module = LoadLibraryW(L"nvapi64.dll");
			}
#else
			HMODULE module = GetModuleHandleW(L"nvapi.dll");
#endif
			const auto query = module ? reinterpret_cast<query_interface_t>(GetProcAddress(module, "nvapi_QueryInterface")) : nullptr;
			const auto initialize = query ? reinterpret_cast<initialize_t>(query(id_initialize)) : nullptr;
			if (query)
			{
				set_viewports_rates = reinterpret_cast<set_viewports_rates_t>(query(id_set_viewports_rates));
				create_rate_view = reinterpret_cast<create_rate_view_t>(query(id_create_rate_view));
				set_rate_view = reinterpret_cast<set_rate_view_t>(query(id_set_rate_view));
			}
			available = initialize && initialize() == 0 && set_viewports_rates && create_rate_view && set_rate_view;
			log::write("foveation: NVAPI variable rate shading %s", available ? "available" : "not available (needs an NVIDIA GPU, Turing or newer)");
			return available;
		}

		IUnknown* view_for(ID3D11Device* device, UINT width, UINT height)
		{
			int slot = -1;
			for (int i = 0; i < 4; ++i)
			{
				if (std::abs(static_cast<int>(full_width >> i) - static_cast<int>(width)) <= 1 && std::abs(static_cast<int>(full_height >> i) - static_cast<int>(height)) <= 1)
				{
					slot = i;
					break;
				}
			}
			if (slot < 0)
			{
				return nullptr;
			}
			rate_map& map = maps[slot];
			if (!map.view || map.width != width || map.height != height || map.revision != revision_frame)
			{
				map = {};
				std::uint32_t tiles_x{}, tiles_y{};
				const std::vector<std::uint8_t> rates = foveation_pattern::make(width, height, rings_frame, tiles_x, tiles_y);
				D3D11_TEXTURE2D_DESC desc{};
				desc.Width = tiles_x;
				desc.Height = tiles_y;
				desc.MipLevels = desc.ArraySize = 1;
				desc.Format = DXGI_FORMAT_R8_UINT;
				desc.SampleDesc.Count = 1;
				desc.Usage = D3D11_USAGE_DEFAULT;
				desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
				const D3D11_SUBRESOURCE_DATA data{ rates.data(), tiles_x, 0 };
				nv_rate_view_desc view_desc{ nv_version<nv_rate_view_desc>(1), DXGI_FORMAT_R8_UINT, nv_srrv_texture2d, 0, 0, 0 };
				IUnknown* view = nullptr;
				if (FAILED(device->CreateTexture2D(&desc, &data, &map.texture)) || create_rate_view(device, map.texture.Get(), &view_desc, &view) != 0 || !view)
				{
					log::write("foveation: shading-rate map %ux%u failed", width, height);
					map = {};
					return nullptr;
				}
				map.view.Attach(view);
				map.width = width;
				map.height = height;
				map.revision = revision_frame;
			}
			return map.view.Get();
		}

		void rates(ID3D11DeviceContext* context, bool on)
		{
			nv_viewport_rate viewport{};
			viewport.enable = true;
			for (int& r : viewport.table)
			{
				r = nv_rate_full;
			}
			viewport.table[1] = nv_rate_2x2;
			viewport.table[2] = nv_rate_4x4;
			nv_viewports_rates desc{ nv_version<nv_viewports_rates>(1), on ? 1u : 0u, on ? &viewport : nullptr };
			set_viewports_rates(context, &desc);
			rates_on = on;
		}

		void on_target(ID3D11DeviceContext* context, UINT width, UINT height, bool full_viewport)
		{
			if (!active)
			{
				return;
			}
			ComPtr<ID3D11Device> device;
			context->GetDevice(&device);
			IUnknown* const view = full_viewport && width && height ? view_for(device.Get(), width, height) : nullptr;
			if (view)
			{
				set_rate_view(context, view);
				rates(context, true);
				++shaded_targets;
			}
			else
			{
				if (rates_on)
				{
					rates(context, false);
				}
				++other_targets;
			}
		}
	}

	void set_foveation(const std::string& level)
	{
		const std::lock_guard guard(lock);
		const foveation_pattern::rings next = foveation_pattern::level(level);
		if (next.enabled != rings_now.enabled || next.inner != rings_now.inner || next.outer != rings_now.outer)
		{
			rings_now = next;
			++level_revision;
		}
	}

	void begin_foveation(ID3D11DeviceContext* context, UINT back_width, UINT back_height)
	{
		{
			const std::lock_guard guard(lock);
			rings_frame = rings_now;
			revision_frame = level_revision;
		}
		if (!context || !rings_frame.enabled || !back_width || !back_height || !load())
		{
			return;
		}
		install_scope_scissor(context);
		full_width = back_width;
		full_height = back_height;
		active = true;
		set_target_listener(&on_target);
		scope_scissor_targets_changed(context);
	}

	void end_foveation(ID3D11DeviceContext* context)
	{
		if (!active)
		{
			return;
		}
		active = false;
		set_target_listener(nullptr);
		if (rates_on)
		{
			rates(context, false);
		}
		set_rate_view(context, nullptr);
	}

	std::string foveation_report()
	{
		foveation_pattern::rings r;
		{
			const std::lock_guard guard(lock);
			r = rings_now;
		}
		float share[3]{};
		foveation_pattern::coverage(r, share);
		char text[200];
		std::snprintf(text, sizeof(text), "foveation enabled=%d inner=%.2f outer=%.2f full=%.0f%% 2x2=%.0f%% 4x4=%.0f%% available=%d shaded_targets=%llu other_targets=%llu",
			r.enabled ? 1 : 0, r.inner, r.outer, 100 * share[0], 100 * share[1], 100 * share[2], available ? 1 : 0, shaded_targets.load(), other_targets.load());
		return text;
	}
}
