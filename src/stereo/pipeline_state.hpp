#pragma once

#include <d3d11.h>
#include <wrl/client.h>

namespace bfvr::stereo
{
	// Everything a full-screen draw inside the game's frame changes, put back afterwards (Get* add
	// references, released by ComPtr). Pixel shader resources are kept for the first two slots.
	struct saved_pipeline
	{
		static constexpr UINT view_slots = 2;

		D3D11_PRIMITIVE_TOPOLOGY topology{};
		Microsoft::WRL::ComPtr<ID3D11InputLayout> layout;
		Microsoft::WRL::ComPtr<ID3D11VertexShader> vs;
		Microsoft::WRL::ComPtr<ID3D11PixelShader> ps;
		Microsoft::WRL::ComPtr<ID3D11GeometryShader> gs;
		Microsoft::WRL::ComPtr<ID3D11HullShader> hs;
		Microsoft::WRL::ComPtr<ID3D11DomainShader> ds;
		Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> ps_views[view_slots];
		Microsoft::WRL::ComPtr<ID3D11SamplerState> ps_sampler;
		Microsoft::WRL::ComPtr<ID3D11Buffer> ps_buffer;
		Microsoft::WRL::ComPtr<ID3D11RasterizerState> rasterizer;
		UINT viewport_count{ D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE };
		D3D11_VIEWPORT viewports[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE]{};
		Microsoft::WRL::ComPtr<ID3D11RenderTargetView> targets[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT];
		Microsoft::WRL::ComPtr<ID3D11DepthStencilView> depth;
		Microsoft::WRL::ComPtr<ID3D11BlendState> blend;
		float blend_factor[4]{};
		UINT sample_mask{};
		Microsoft::WRL::ComPtr<ID3D11DepthStencilState> depth_state;
		UINT stencil_ref{};

		void save(ID3D11DeviceContext* c)
		{
			c->IAGetPrimitiveTopology(&topology);
			c->IAGetInputLayout(&layout);
			c->VSGetShader(&vs, nullptr, nullptr);
			c->PSGetShader(&ps, nullptr, nullptr);
			c->GSGetShader(&gs, nullptr, nullptr);
			c->HSGetShader(&hs, nullptr, nullptr);
			c->DSGetShader(&ds, nullptr, nullptr);
			ID3D11ShaderResourceView* views[view_slots]{};
			c->PSGetShaderResources(0, view_slots, views);
			for (UINT i = 0; i < view_slots; ++i)
			{
				ps_views[i].Attach(views[i]);
			}
			c->PSGetSamplers(0, 1, &ps_sampler);
			c->PSGetConstantBuffers(0, 1, &ps_buffer);
			c->RSGetState(&rasterizer);
			c->RSGetViewports(&viewport_count, viewports);
			ID3D11RenderTargetView* raw[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT]{};
			c->OMGetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, raw, &depth);
			for (UINT i = 0; i < D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT; ++i)
			{
				targets[i].Attach(raw[i]);
			}
			c->OMGetBlendState(&blend, blend_factor, &sample_mask);
			c->OMGetDepthStencilState(&depth_state, &stencil_ref);
		}

		void restore(ID3D11DeviceContext* c)
		{
			c->IASetPrimitiveTopology(topology);
			c->IASetInputLayout(layout.Get());
			c->VSSetShader(vs.Get(), nullptr, 0);
			c->PSSetShader(ps.Get(), nullptr, 0);
			c->GSSetShader(gs.Get(), nullptr, 0);
			c->HSSetShader(hs.Get(), nullptr, 0);
			c->DSSetShader(ds.Get(), nullptr, 0);
			ID3D11ShaderResourceView* views[view_slots]{};
			for (UINT i = 0; i < view_slots; ++i)
			{
				views[i] = ps_views[i].Get();
			}
			c->PSSetShaderResources(0, view_slots, views);
			ID3D11SamplerState* s = ps_sampler.Get();
			c->PSSetSamplers(0, 1, &s);
			ID3D11Buffer* b = ps_buffer.Get();
			c->PSSetConstantBuffers(0, 1, &b);
			c->RSSetState(rasterizer.Get());
			c->RSSetViewports(viewport_count, viewports);
			ID3D11RenderTargetView* raw[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT]{};
			for (UINT i = 0; i < D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT; ++i)
			{
				raw[i] = targets[i].Get();
			}
			c->OMSetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, raw, depth.Get());
			c->OMSetBlendState(blend.Get(), blend_factor, sample_mask);
			c->OMSetDepthStencilState(depth_state.Get(), stencil_ref);
		}
	};
}
