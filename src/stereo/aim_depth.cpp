#include "stereo/aim_depth.hpp"

#include <windows.h>
#include <d3d11_1.h>
#include <d3dcompiler.h>
#include <wrl/client.h>

#include <atomic>
#include <cstdio>
#include <cstring>
#include <mutex>

#include "common/log.hpp"
#include "input/vr_controls.hpp"
#include "stereo/aim_depth_math.hpp"
#include "stereo/eye_pass.hpp"
#include "stereo/tracking.hpp"

namespace bfvr::stereo
{
	using Microsoft::WRL::ComPtr;
	namespace math = aim_depth_math;

	namespace
	{
		std::mutex report_lock;
		D3D11_DEPTH_STENCIL_VIEW_DESC view_desc{};
		D3D11_TEXTURE2D_DESC texture_desc{};
		bool seen{};
		std::atomic<unsigned long long> frames{};
		std::atomic<bool> enabled{ true };

		// The depth at each sample's texel: one thread per sample (R24 of the R24G8 depth, 0..1).
		constexpr char shader_source[] = R"(
Texture2D<float> scene_depth : register(t0);
cbuffer samples : register(b0) { float4 texels[32]; };
RWBuffer<float> depths : register(u0);
[numthreads(32, 1, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
	float4 p = texels[id.x];
	depths[id.x] = p.z > 0.5 ? scene_depth.Load(int3(int(p.x), int(p.y), 0)) : -1.0;
}
)";
		struct gpu
		{
			ComPtr<ID3D11ComputeShader> shader;
			ComPtr<ID3D11Buffer> texels;  // constant buffer, 32 float4
			ComPtr<ID3D11Buffer> depths;  // 32 floats
			ComPtr<ID3D11UnorderedAccessView> depths_view;
			static constexpr int ring = 3;
			ComPtr<ID3D11Buffer> staging[ring];
			math::sample_set sets[ring];
			math::projection projections[ring];
			bool pending[ring]{};
			int next{};
			ComPtr<ID3D11ShaderResourceView> depth_view;
			ID3D11Resource* depth_view_of{};
			bool failed{};
		} g;

		std::atomic<float> hit{ -1.0f };
		std::atomic<ULONGLONG> hit_at{};
		std::atomic<unsigned long long> dispatches{};
		std::atomic<unsigned long long> readbacks{};

		bool create(ID3D11Device* device)
		{
			if (g.shader)
			{
				return true;
			}
			if (g.failed)
			{
				return false;
			}
			ComPtr<ID3DBlob> code, errors;
			if (FAILED(D3DCompile(shader_source, sizeof(shader_source) - 1, "aim_depth", nullptr, nullptr, "main", "cs_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &code, &errors)))
			{
				log::write("aim depth: shader compile failed: %s", errors ? static_cast<const char*>(errors->GetBufferPointer()) : "?");
				g.failed = true;
				return false;
			}
			D3D11_BUFFER_DESC cb{};
			cb.ByteWidth = math::samples * 16;
			cb.Usage = D3D11_USAGE_DYNAMIC;
			cb.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
			cb.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
			D3D11_BUFFER_DESC out{};
			out.ByteWidth = math::samples * 4;
			out.Usage = D3D11_USAGE_DEFAULT;
			out.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
			D3D11_UNORDERED_ACCESS_VIEW_DESC uav{};
			uav.Format = DXGI_FORMAT_R32_FLOAT;
			uav.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
			uav.Buffer.NumElements = math::samples;
			D3D11_BUFFER_DESC staging{};
			staging.ByteWidth = math::samples * 4;
			staging.Usage = D3D11_USAGE_STAGING;
			staging.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
			bool ok = SUCCEEDED(device->CreateComputeShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr, &g.shader))
				&& SUCCEEDED(device->CreateBuffer(&cb, nullptr, &g.texels)) && SUCCEEDED(device->CreateBuffer(&out, nullptr, &g.depths))
				&& SUCCEEDED(device->CreateUnorderedAccessView(g.depths.Get(), &uav, &g.depths_view));
			for (int i = 0; i < gpu::ring && ok; ++i)
			{
				ok = SUCCEEDED(device->CreateBuffer(&staging, nullptr, &g.staging[i]));
			}
			if (!ok)
			{
				log::write("aim depth: creating the GPU objects failed");
				g = gpu{};
				g.failed = true;
				return false;
			}
			log::write("aim depth: ready");
			return true;
		}

		bool depth_view_for(ID3D11Device* device, ID3D11Resource* resource)
		{
			if (g.depth_view && g.depth_view_of == resource)
			{
				return true;
			}
			g.depth_view.Reset();
			g.depth_view_of = nullptr;
			D3D11_SHADER_RESOURCE_VIEW_DESC srv{};
			srv.Format = DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
			srv.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
			srv.Texture2D.MipLevels = 1;
			if (FAILED(device->CreateShaderResourceView(resource, &srv, &g.depth_view)))
			{
				return false;
			}
			g.depth_view_of = resource;
			return true;
		}

		// Earlier dispatches whose copies are done: their hit distance.
		void collect(ID3D11DeviceContext* context)
		{
			for (int k = 1; k <= gpu::ring; ++k)
			{
				const int i = (g.next + k) % gpu::ring; // oldest first
				if (!g.pending[i])
				{
					continue;
				}
				D3D11_MAPPED_SUBRESOURCE mapped{};
				if (FAILED(context->Map(g.staging[i].Get(), 0, D3D11_MAP_READ, D3D11_MAP_FLAG_DO_NOT_WAIT, &mapped)))
				{
					continue;
				}
				float depths[math::samples];
				std::memcpy(depths, mapped.pData, sizeof(depths));
				context->Unmap(g.staging[i].Get(), 0);
				g.pending[i] = false;
				++readbacks;
				hit = math::hit_distance(g.sets[i], depths, g.projections[i]);
				hit_at = GetTickCount64();
			}
		}
	}

	void aim_depth_on_hud_targets(ID3D11DeviceContext* context, ID3D11DepthStencilView* depth)
	{
		if (!depth)
		{
			return;
		}
		++frames;
		ComPtr<ID3D11Resource> resource;
		depth->GetResource(&resource);
		{
			const std::lock_guard guard(report_lock);
			depth->GetDesc(&view_desc);
			ComPtr<ID3D11Texture2D> texture;
			if (SUCCEEDED(resource.As(&texture)))
			{
				texture->GetDesc(&texture_desc);
				seen = true;
			}
		}
		if (!enabled.load(std::memory_order_relaxed) || texture_desc.SampleDesc.Count != 1 || texture_desc.Format != DXGI_FORMAT_R24G8_TYPELESS
			|| !(texture_desc.BindFlags & D3D11_BIND_SHADER_RESOURCE))
		{
			return;
		}
		ComPtr<ID3D11Device> device;
		context->GetDevice(&device);
		if (!create(device.Get()) || !depth_view_for(device.Get(), resource.Get()))
		{
			return;
		}
		collect(context);

		// This frame's samples: the shot line, projected into the eye whose depth this is (the second eye
		// in pair rendering, the eye of the copy otherwise).
		float start[3], direction[3], m[16];
		const rendered_view view = latest_rendered_view();
		if (!input::shot_line(start, direction) || !view.stereo || !world_projection(m) || g.pending[g.next])
		{
			return;
		}
		const pose& eye = view.pair ? view.second_pose : view.eye_pose;
		math::projection p;
		p.sx = m[0];
		p.sy = m[5];
		p.a = m[10];
		p.b = m[14];
		p.width = static_cast<float>(texture_desc.Width);
		p.height = static_cast<float>(texture_desc.Height);
		const float eye_position[3]{ eye.position[0], eye.position[1], eye.position[2] };
		const float eye_orientation[4]{ eye.orientation[0], eye.orientation[1], eye.orientation[2], eye.orientation[3] };
		const math::sample_set set = math::make_samples(start, direction, eye_position, eye_orientation, p);

		D3D11_MAPPED_SUBRESOURCE mapped{};
		if (FAILED(context->Map(g.texels.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
		{
			return;
		}
		auto* texels = static_cast<float*>(mapped.pData);
		for (int i = 0; i < math::samples; ++i)
		{
			texels[i * 4 + 0] = set.texel[i][0];
			texels[i * 4 + 1] = set.texel[i][1];
			texels[i * 4 + 2] = set.on_screen[i] ? 1.0f : 0.0f;
			texels[i * 4 + 3] = 0.0f;
		}
		context->Unmap(g.texels.Get(), 0);

		// The depth is the bound depth target (the HUD is about to bind it again): unbind the targets for the
		// read, and keep the game's compute state as it was.
		ComPtr<ID3D11DeviceContext1> context1;
		context->QueryInterface(IID_PPV_ARGS(&context1));
		ComPtr<ID3D11ComputeShader> old_shader;
		context->CSGetShader(&old_shader, nullptr, nullptr);
		ComPtr<ID3D11ShaderResourceView> old_srv;
		context->CSGetShaderResources(0, 1, &old_srv);
		ComPtr<ID3D11UnorderedAccessView> old_uav;
		context->CSGetUnorderedAccessViews(0, 1, &old_uav);
		ID3D11Buffer* old_cb{};
		UINT old_first{}, old_count{};
		if (context1)
		{
			context1->CSGetConstantBuffers1(0, 1, &old_cb, &old_first, &old_count);
		}
		else
		{
			context->CSGetConstantBuffers(0, 1, &old_cb);
		}

		context->OMSetRenderTargets(0, nullptr, nullptr);
		context->CSSetShader(g.shader.Get(), nullptr, 0);
		ID3D11ShaderResourceView* const srv = g.depth_view.Get();
		context->CSSetShaderResources(0, 1, &srv);
		ID3D11UnorderedAccessView* const uav = g.depths_view.Get();
		context->CSSetUnorderedAccessViews(0, 1, &uav, nullptr);
		ID3D11Buffer* const cb = g.texels.Get();
		context->CSSetConstantBuffers(0, 1, &cb);
		context->Dispatch(1, 1, 1);

		ID3D11ShaderResourceView* const no_srv = nullptr;
		context->CSSetShaderResources(0, 1, &no_srv); // the depth goes back to being a target
		context->CSSetShader(old_shader.Get(), nullptr, 0);
		ID3D11ShaderResourceView* const restored_srv = old_srv.Get();
		context->CSSetShaderResources(0, 1, &restored_srv);
		ID3D11UnorderedAccessView* const restored_uav = old_uav.Get();
		context->CSSetUnorderedAccessViews(0, 1, &restored_uav, nullptr);
		if (context1 && old_count)
		{
			context1->CSSetConstantBuffers1(0, 1, &old_cb, &old_first, &old_count);
		}
		else
		{
			context->CSSetConstantBuffers(0, 1, &old_cb);
		}
		if (old_cb)
		{
			old_cb->Release();
		}

		context->CopyResource(g.staging[g.next].Get(), g.depths.Get());
		g.sets[g.next] = set;
		g.projections[g.next] = p;
		g.pending[g.next] = true;
		g.next = (g.next + 1) % gpu::ring;
		++dispatches;
	}

	bool aim_hit_distance(float& distance)
	{
		distance = hit.load(std::memory_order_relaxed);
		return enabled.load(std::memory_order_relaxed) && distance > 0.0f && GetTickCount64() - hit_at.load(std::memory_order_relaxed) < 300;
	}

	void set_aim_depth(bool on)
	{
		enabled = on;
	}

	std::string aim_depth_report()
	{
		const std::lock_guard guard(report_lock);
		char text[600];
		std::snprintf(text, sizeof(text),
			"aim_depth=%d frames=%llu dispatches=%llu readbacks=%llu hit=%.2f age_ms=%llu | depth %ux%u format=%d samples=%u bind=0x%x",
			enabled.load() ? 1 : 0, frames.load(), dispatches.load(), readbacks.load(), hit.load(), static_cast<unsigned long long>(GetTickCount64() - hit_at.load()),
			texture_desc.Width, texture_desc.Height, static_cast<int>(texture_desc.Format), texture_desc.SampleDesc.Count, texture_desc.BindFlags);
		std::string out = text;
		float m[16]{};
		if (world_projection(m))
		{
			char rows[160];
			std::snprintf(rows, sizeof(rows), "\nprojection sx=%.4f sy=%.4f a=%.4f b=%.4f", m[0], m[5], m[10], m[14]);
			out += rows;
		}
		return out;
	}
}
