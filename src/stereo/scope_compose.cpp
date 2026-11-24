#include "stereo/scope_compose.hpp"

#include <windows.h>
#include <d3dcompiler.h>
#include <wrl/client.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <mutex>

#include "common/log.hpp"
#include "stereo/eye_pass.hpp"
#include "stereo/pipeline_state.hpp"

using Microsoft::WRL::ComPtr;

namespace bfvr::stereo
{
	namespace
	{
		// #139: 1024, the glass reads it at up to its power (512 left the glass 3.6x upscaled in an ACOG).
		constexpr UINT output_size = 1024;

		// Full-screen triangle; the pixel shader maps the quad to the lens disc.
		constexpr const char shader_source[] = R"(
Texture2D source : register(t0);
SamplerState linear_clamp : register(s0);
cbuffer scope : register(b0)
{
	float4 shape;  // dot radius, edge width, edge darkening, unused (fractions of the lens radius)
	float4 colour; // dot colour, alpha = strength
};
struct vertex { float4 position : SV_Position; float2 uv : TEXCOORD0; };
vertex vs(uint id : SV_VertexID)
{
	vertex v;
	v.uv = float2((id << 1) & 2, id & 2);
	v.position = float4(v.uv * float2(2, -2) + float2(-1, 1), 0, 1);
	return v;
}
float4 ps(vertex v) : SV_Target
{
	float2 p = v.uv * 2 - 1;
	float r = length(p);
	float3 c = source.SampleLevel(linear_clamp, v.uv, 0).rgb;
	c *= 1 - shape.z * smoothstep(0.7, 1.0, r);
	float dot_alpha = colour.a * saturate((shape.x - r) / max(shape.x * 0.35, 1e-4) + 0.5);
	c = lerp(c, colour.rgb, dot_alpha);
	float alpha = saturate((1 - r) / max(shape.y, 1e-4));
	return float4(c * alpha, alpha);
}
)";

		struct constants
		{
			float shape[4];
			float colour[4];
		};

		std::mutex lock;
		ComPtr<ID3D11Device> device;
		ComPtr<ID3D11VertexShader> vertex_shader;
		ComPtr<ID3D11PixelShader> pixel_shader;
		ComPtr<ID3D11SamplerState> sampler;
		ComPtr<ID3D11Buffer> constant_buffer;
		ComPtr<ID3D11Texture2D> source;
		ComPtr<ID3D11ShaderResourceView> source_view;
		D3D11_TEXTURE2D_DESC source_desc{};
		ComPtr<ID3D11Texture2D> output;
		ComPtr<ID3D11RenderTargetView> output_target;
		bool failed{};
		scope_image latest;
		std::atomic<std::uint64_t> composed{};
		std::atomic<std::uint64_t> errors{};

		ComPtr<ID3DBlob> compile(const char* entry, const char* target)
		{
			ComPtr<ID3DBlob> code, messages;
			const HRESULT hr = D3DCompile(shader_source, sizeof(shader_source) - 1, "scope", nullptr, nullptr, entry, target,
				D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &code, &messages);
			if (FAILED(hr))
			{
				log::write("scope: %s compile failed 0x%08lx %s", entry, static_cast<unsigned long>(hr),
					messages ? static_cast<const char*>(messages->GetBufferPointer()) : "");
				return {};
			}
			return code;
		}

		bool create_pipeline(ID3D11Device* d)
		{
			if (device.Get() == d && pixel_shader)
			{
				return true;
			}
			if (failed)
			{
				return false;
			}
			device = d;
			const ComPtr<ID3DBlob> vs = compile("vs", "vs_5_0");
			const ComPtr<ID3DBlob> ps = compile("ps", "ps_5_0");
			D3D11_SAMPLER_DESC sampler_desc{};
			sampler_desc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
			sampler_desc.AddressU = sampler_desc.AddressV = sampler_desc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
			sampler_desc.MaxLOD = D3D11_FLOAT32_MAX;
			D3D11_BUFFER_DESC buffer_desc{};
			buffer_desc.ByteWidth = sizeof(constants);
			buffer_desc.Usage = D3D11_USAGE_DEFAULT;
			buffer_desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
			if (!vs || !ps || FAILED(d->CreateVertexShader(vs->GetBufferPointer(), vs->GetBufferSize(), nullptr, &vertex_shader))
				|| FAILED(d->CreatePixelShader(ps->GetBufferPointer(), ps->GetBufferSize(), nullptr, &pixel_shader))
				|| FAILED(d->CreateSamplerState(&sampler_desc, &sampler)) || FAILED(d->CreateBuffer(&buffer_desc, nullptr, &constant_buffer)))
			{
				log::write("scope: pipeline creation failed, magnified sights off");
				failed = true;
				pixel_shader.Reset();
				return false;
			}
			log::write("scope: pipeline ready");
			return true;
		}

		bool ensure_textures(DXGI_FORMAT format, UINT width, UINT height)
		{
			if (!source || source_desc.Width != width || source_desc.Height != height || source_desc.Format != format)
			{
				source.Reset();
				source_view.Reset();
				D3D11_TEXTURE2D_DESC desc{};
				desc.Width = width;
				desc.Height = height;
				desc.MipLevels = desc.ArraySize = 1;
				desc.Format = format;
				desc.SampleDesc.Count = 1;
				desc.Usage = D3D11_USAGE_DEFAULT;
				desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
				if (FAILED(device->CreateTexture2D(&desc, nullptr, &source)) || FAILED(device->CreateShaderResourceView(source.Get(), nullptr, &source_view)))
				{
					source.Reset();
					return false;
				}
				source_desc = desc;
			}
			D3D11_TEXTURE2D_DESC have{};
			if (output)
			{
				output->GetDesc(&have);
			}
			if (!output || have.Format != format)
			{
				output.Reset();
				output_target.Reset();
				D3D11_TEXTURE2D_DESC desc{};
				desc.Width = desc.Height = output_size;
				desc.MipLevels = desc.ArraySize = 1;
				desc.Format = format;
				desc.SampleDesc.Count = 1;
				desc.Usage = D3D11_USAGE_DEFAULT;
				desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
				if (FAILED(device->CreateTexture2D(&desc, nullptr, &output)) || FAILED(device->CreateRenderTargetView(output.Get(), nullptr, &output_target)))
				{
					output.Reset();
					return false;
				}
			}
			return true;
		}
	}

	bool compose_scope(ID3D11DeviceContext* context, ID3D11Texture2D* back_buffer, float aspect, const scope_request& request)
	{
		if (!context || !back_buffer || !(aspect > 0.1f && aspect < 10.0f) || !(request.crop > 0.0f && request.crop <= 1.0f))
		{
			return false;
		}
		ComPtr<ID3D11Device> d;
		back_buffer->GetDevice(&d);
		D3D11_TEXTURE2D_DESC desc{};
		back_buffer->GetDesc(&desc);
		const std::lock_guard guard(lock);
		if (!create_pipeline(d.Get()))
		{
			return false;
		}
		// The lens circle: crop of the height; horizontally the same angle spans crop / aspect of the width.
		const UINT height = std::clamp<UINT>(static_cast<UINT>(std::lround(request.crop * static_cast<float>(desc.Height))), 16, desc.Height);
		const UINT width = std::clamp<UINT>(static_cast<UINT>(std::lround(request.crop * static_cast<float>(desc.Width) / aspect)), 16, desc.Width);
		if (!ensure_textures(desc.Format, width, height))
		{
			++errors;
			return false;
		}
		const D3D11_BOX box{ (desc.Width - width) / 2, (desc.Height - height) / 2, 0, (desc.Width - width) / 2 + width, (desc.Height - height) / 2 + height, 1 };
		context->CopySubresourceRegion(source.Get(), 0, 0, 0, 0, back_buffer, 0, &box);

		// #139: the optic's own glass draws the optic's own reticle over the picture: no dot burnt in then.
		const float dot = scope_glass_shown() ? 0.0f : 0.95f;
		const constants values{ { 0.022f, 0.04f, 0.35f, 0.0f }, { 1.0f, 0.12f, 0.06f, dot } };
		context->UpdateSubresource(constant_buffer.Get(), 0, nullptr, &values, 0, 0);

		saved_pipeline state;
		state.save(context);
		ID3D11ShaderResourceView* const no_view = nullptr;
		context->PSSetShaderResources(0, 1, &no_view);
		ID3D11RenderTargetView* const target = output_target.Get();
		context->OMSetRenderTargets(1, &target, nullptr);
		context->OMSetBlendState(nullptr, nullptr, 0xffffffffu);
		context->OMSetDepthStencilState(nullptr, 0);
		context->RSSetState(nullptr);
		const D3D11_VIEWPORT viewport{ 0, 0, static_cast<float>(output_size), static_cast<float>(output_size), 0, 1 };
		context->RSSetViewports(1, &viewport);
		context->IASetInputLayout(nullptr);
		context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		context->VSSetShader(vertex_shader.Get(), nullptr, 0);
		context->GSSetShader(nullptr, nullptr, 0);
		context->HSSetShader(nullptr, nullptr, 0);
		context->DSSetShader(nullptr, nullptr, 0);
		context->PSSetShader(pixel_shader.Get(), nullptr, 0);
		ID3D11ShaderResourceView* const view = source_view.Get();
		context->PSSetShaderResources(0, 1, &view);
		ID3D11SamplerState* const s = sampler.Get();
		context->PSSetSamplers(0, 1, &s);
		ID3D11Buffer* const b = constant_buffer.Get();
		context->PSSetConstantBuffers(0, 1, &b);
		context->Draw(3, 0);
		context->PSSetShaderResources(0, 1, &no_view);
		state.restore(context);

		latest.texture = output.Get();
		latest.frame = ++composed;
		latest.quad = request.quad;
		latest.quad_size = request.quad_size;
		latest.eye = request.eye;
		latest.lens_tan = std::tan(0.5f * request.render_fov_y) * request.crop;
		latest.magnification = request.magnification;
		for (int i = 0; i < 4; ++i)
		{
			latest.picture[i] = request.relative.orientation[i];
		}
		latest.at = GetTickCount64();
		return true;
	}

	scope_image latest_scope_image()
	{
		const std::lock_guard guard(lock);
		return latest;
	}

	std::string scope_compose_report()
	{
		char text[160];
		std::snprintf(text, sizeof(text), "scope_images=%llu scope_errors=%llu source=%ux%u pipeline=%s", composed.load(), errors.load(), source_desc.Width,
			source_desc.Height, failed ? "failed" : (pixel_shader ? "ready" : "not yet"));
		return text;
	}
}
