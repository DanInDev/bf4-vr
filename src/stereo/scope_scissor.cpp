#include "stereo/scope_scissor.hpp"

#include <windows.h>
#include <wrl/client.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <unordered_map>

#include <MinHook.h>

#include "common/log.hpp"

using Microsoft::WRL::ComPtr;

namespace bfvr::stereo
{
	namespace
	{
		// ID3D11DeviceContext vtable (d3d11.h order).
		constexpr int om_set_render_targets_slot = 33;
		constexpr int om_set_render_targets_uav_slot = 34;
		constexpr int rs_set_state_slot = 43;
		constexpr int rs_set_viewports_slot = 44;
		constexpr int rs_set_scissor_rects_slot = 45;
		constexpr UINT max_rects = D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;

		using om_set_render_targets_t = void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, UINT, ID3D11RenderTargetView* const*, ID3D11DepthStencilView*);
		using om_set_render_targets_uav_t = void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, UINT, ID3D11RenderTargetView* const*, ID3D11DepthStencilView*, UINT, UINT,
			ID3D11UnorderedAccessView* const*, const UINT*);
		using rs_set_state_t = void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, ID3D11RasterizerState*);
		using rs_set_viewports_t = void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, UINT, const D3D11_VIEWPORT*);
		using rs_set_scissor_rects_t = void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, UINT, const D3D11_RECT*);
		om_set_render_targets_t original_om_set_render_targets{};
		om_set_render_targets_uav_t original_om_set_render_targets_uav{};
		rs_set_state_t original_rs_set_state{};
		rs_set_viewports_t original_rs_set_viewports{};
		rs_set_scissor_rects_t original_rs_set_scissor_rects{};

		// Render thread only.
		bool installed{};
		bool active{};
		UINT full_width{}, full_height{};
		float keep_x{}, keep_y{};
		ComPtr<ID3D11Device> device;
		ComPtr<ID3D11RasterizerState> game_state; // what the game set (null = default)
		bool game_scissor{};                        // the game's state has scissoring on
		D3D11_RECT game_rects[max_rects]{};
		UINT game_rect_count{};
		struct clone
		{
			ComPtr<ID3D11RasterizerState> original; // kept alive so its address is not reused
			ComPtr<ID3D11RasterizerState> scissored;
			bool scissor{};
		};
		std::unordered_map<ID3D11RasterizerState*, clone> clones;
		ComPtr<ID3D11RasterizerState> default_scissored;
		std::atomic<std::uint64_t> scissored_targets{};
		std::atomic<std::uint64_t> other_targets{};

		ID3D11RasterizerState* scissored_state(ID3D11RasterizerState* state, bool& had_scissor)
		{
			D3D11_RASTERIZER_DESC desc{ D3D11_FILL_SOLID, D3D11_CULL_BACK, FALSE, 0, 0.0f, 0.0f, TRUE, FALSE, FALSE, FALSE };
			if (!state)
			{
				had_scissor = false;
				if (!default_scissored)
				{
					desc.ScissorEnable = TRUE;
					device->CreateRasterizerState(&desc, &default_scissored);
				}
				return default_scissored.Get();
			}
			auto found = clones.find(state);
			if (found == clones.end())
			{
				clone c;
				c.original = state;
				state->GetDesc(&desc);
				c.scissor = desc.ScissorEnable != FALSE;
				desc.ScissorEnable = TRUE;
				device->CreateRasterizerState(&desc, &c.scissored);
				found = clones.emplace(state, std::move(c)).first;
			}
			had_scissor = found->second.scissor;
			return found->second.scissored.Get();
		}

		// Size of the bound colour target (else the depth target), mip level included.
		bool target_size(ID3D11DeviceContext* context, UINT& width, UINT& height)
		{
			ComPtr<ID3D11RenderTargetView> rtv;
			ComPtr<ID3D11DepthStencilView> dsv;
			context->OMGetRenderTargets(1, &rtv, &dsv);
			ComPtr<ID3D11Resource> resource;
			UINT mip = 0;
			if (rtv)
			{
				rtv->GetResource(&resource);
				D3D11_RENDER_TARGET_VIEW_DESC view{};
				rtv->GetDesc(&view);
				mip = view.ViewDimension == D3D11_RTV_DIMENSION_TEXTURE2D ? view.Texture2D.MipSlice : (view.ViewDimension == D3D11_RTV_DIMENSION_TEXTURE2DARRAY ? view.Texture2DArray.MipSlice : 0);
			}
			else if (dsv)
			{
				dsv->GetResource(&resource);
				D3D11_DEPTH_STENCIL_VIEW_DESC view{};
				dsv->GetDesc(&view);
				mip = view.ViewDimension == D3D11_DSV_DIMENSION_TEXTURE2D ? view.Texture2D.MipSlice : 0;
			}
			ComPtr<ID3D11Texture2D> texture;
			if (!resource || FAILED(resource.As(&texture)))
			{
				return false;
			}
			D3D11_TEXTURE2D_DESC desc{};
			texture->GetDesc(&desc);
			width = std::max(1u, desc.Width >> mip);
			height = std::max(1u, desc.Height >> mip);
			return true;
		}

		// full / target is 1, 2, 4 or 8 on both axes.
		bool proportional(UINT width, UINT height)
		{
			for (UINT divisor = 1; divisor <= 8; divisor *= 2)
			{
				const auto matches = [divisor](UINT full, UINT size) { return std::abs(static_cast<int>(full / divisor) - static_cast<int>(size)) <= 1; };
				if (matches(full_width, width) && matches(full_height, height))
				{
					return true;
				}
			}
			return false;
		}

		void apply(ID3D11DeviceContext* context)
		{
			UINT width{}, height{};
			D3D11_VIEWPORT viewport{};
			UINT viewports = 1;
			context->RSGetViewports(&viewports, &viewport);
			const bool full_viewport = viewports >= 1 && viewport.TopLeftX < 1.0f && viewport.TopLeftY < 1.0f;
			if (!target_size(context, width, height) || !proportional(width, height) || !full_viewport
				|| std::fabs(viewport.Width - static_cast<float>(width)) > 1.0f || std::fabs(viewport.Height - static_cast<float>(height)) > 1.0f)
			{
				original_rs_set_state(context, game_state.Get());
				if (game_rect_count)
				{
					original_rs_set_scissor_rects(context, game_rect_count, game_rects);
				}
				++other_targets;
				return;
			}
			bool had_scissor = false;
			original_rs_set_state(context, scissored_state(game_state.Get(), had_scissor));
			const LONG half_x = static_cast<LONG>(std::ceil(keep_x * static_cast<float>(width)));
			const LONG half_y = static_cast<LONG>(std::ceil(keep_y * static_cast<float>(height)));
			const LONG cx = static_cast<LONG>(width / 2), cy = static_cast<LONG>(height / 2);
			D3D11_RECT rect{ cx - half_x, cy - half_y, cx + half_x, cy + half_y };
			if (had_scissor && game_rect_count)
			{
				const D3D11_RECT& g = game_rects[0];
				rect = { std::max(rect.left, g.left), std::max(rect.top, g.top), std::min(rect.right, g.right), std::min(rect.bottom, g.bottom) };
				if (rect.right < rect.left || rect.bottom < rect.top)
				{
					rect = { 0, 0, 0, 0 };
				}
			}
			original_rs_set_scissor_rects(context, 1, &rect);
			++scissored_targets;
		}

		void STDMETHODCALLTYPE om_set_render_targets_detour(ID3D11DeviceContext* c, UINT n, ID3D11RenderTargetView* const* views, ID3D11DepthStencilView* depth)
		{
			original_om_set_render_targets(c, n, views, depth);
			if (active)
			{
				apply(c);
			}
		}

		void STDMETHODCALLTYPE om_set_render_targets_uav_detour(ID3D11DeviceContext* c, UINT n, ID3D11RenderTargetView* const* views, ID3D11DepthStencilView* depth,
			UINT start, UINT uavs, ID3D11UnorderedAccessView* const* uav, const UINT* counts)
		{
			original_om_set_render_targets_uav(c, n, views, depth, start, uavs, uav, counts);
			if (active)
			{
				apply(c);
			}
		}

		void STDMETHODCALLTYPE rs_set_state_detour(ID3D11DeviceContext* c, ID3D11RasterizerState* state)
		{
			if (!active)
			{
				original_rs_set_state(c, state);
				return;
			}
			game_state = state;
			apply(c);
		}

		void STDMETHODCALLTYPE rs_set_viewports_detour(ID3D11DeviceContext* c, UINT n, const D3D11_VIEWPORT* viewports)
		{
			original_rs_set_viewports(c, n, viewports);
			if (active)
			{
				apply(c);
			}
		}

		void STDMETHODCALLTYPE rs_set_scissor_rects_detour(ID3D11DeviceContext* c, UINT n, const D3D11_RECT* rects)
		{
			if (!active)
			{
				original_rs_set_scissor_rects(c, n, rects);
				return;
			}
			game_rect_count = std::min(n, max_rects);
			for (UINT i = 0; i < game_rect_count; ++i)
			{
				game_rects[i] = rects[i];
			}
			apply(c);
		}
	}

	void install_scope_scissor(ID3D11DeviceContext* context)
	{
		if (installed || !context)
		{
			return;
		}
		installed = true;
		context->GetDevice(&device);
		void** const vtable = *reinterpret_cast<void***>(context);
		const struct { int slot; void* detour; void** original; } table[] = {
			{ om_set_render_targets_slot, reinterpret_cast<void*>(&om_set_render_targets_detour), reinterpret_cast<void**>(&original_om_set_render_targets) },
			{ om_set_render_targets_uav_slot, reinterpret_cast<void*>(&om_set_render_targets_uav_detour), reinterpret_cast<void**>(&original_om_set_render_targets_uav) },
			{ rs_set_state_slot, reinterpret_cast<void*>(&rs_set_state_detour), reinterpret_cast<void**>(&original_rs_set_state) },
			{ rs_set_viewports_slot, reinterpret_cast<void*>(&rs_set_viewports_detour), reinterpret_cast<void**>(&original_rs_set_viewports) },
			{ rs_set_scissor_rects_slot, reinterpret_cast<void*>(&rs_set_scissor_rects_detour), reinterpret_cast<void**>(&original_rs_set_scissor_rects) },
		};
		for (const auto& entry : table)
		{
			MH_STATUS status = MH_CreateHook(vtable[entry.slot], entry.detour, entry.original);
			if (status == MH_OK)
			{
				status = MH_EnableHook(vtable[entry.slot]);
			}
			// OMSetRenderTargets is hooked by the HUD capture already: it calls scope_scissor_targets_changed.
			log::write("scope scissor: hook slot %d: %s", entry.slot, status == MH_ERROR_ALREADY_CREATED ? "taken (HUD capture forwards)" : MH_StatusToString(status));
		}
	}

	void scope_scissor_targets_changed(ID3D11DeviceContext* context)
	{
		if (active)
		{
			apply(context);
		}
	}

	void begin_scope_scissor(ID3D11DeviceContext* context, UINT back_width, UINT back_height, float half_width, float half_height)
	{
		if (!installed || !original_rs_set_state || !context || !back_width || !back_height)
		{
			return;
		}
		full_width = back_width;
		full_height = back_height;
		keep_x = std::clamp(half_width, 0.01f, 0.5f);
		keep_y = std::clamp(half_height, 0.01f, 0.5f);
		context->RSGetState(&game_state);
		game_rect_count = max_rects;
		context->RSGetScissorRects(&game_rect_count, game_rects);
		active = true;
		apply(context);
	}

	void end_scope_scissor(ID3D11DeviceContext* context)
	{
		if (!active)
		{
			return;
		}
		active = false;
		original_rs_set_state(context, game_state.Get());
		if (game_rect_count)
		{
			original_rs_set_scissor_rects(context, game_rect_count, game_rects);
		}
		game_state.Reset();
	}

	std::string scope_scissor_report()
	{
		char text[128];
		std::snprintf(text, sizeof(text), "scissor installed=%d scissored_targets=%llu other_targets=%llu clones=%zu", installed ? 1 : 0,
			scissored_targets.load(), other_targets.load(), clones.size());
		return text;
	}
}
