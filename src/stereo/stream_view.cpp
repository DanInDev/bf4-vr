#include "stereo/stream_view.hpp"

#include <windows.h>
#include <d3dcompiler.h>
#include <wrl/client.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <mutex>
#include <sstream>

#include "common/log.hpp"
#include "hooks/game_window.hpp"
#include "stereo/eye_pass.hpp"
#include "stereo/hud_capture.hpp"
#include "stereo/pipeline_state.hpp"
#include "stereo/stream_math.hpp"
#include "stereo/tracking.hpp"

using Microsoft::WRL::ComPtr;

namespace bfvr::stereo
{
	namespace
	{
		constexpr float picture_aspect = 16.0f / 9.0f;
		constexpr float speed_up = 3.0f;       // Hz of cut-off per radian a second of turning
		constexpr ULONGLONG stale_ms = 500;     // no new eye image for this long: not drawn over
		constexpr std::uint64_t hud_stale_presents = 10;
		constexpr float degrees = 57.29578f;
		// The HUD texture's main piece (crosshair, markers, objective text) ends here; below it sit the
		// minimap and counters, which the headset shows on the body (theater upper_rect).
		constexpr float hud_main_rows = 0.82f;
		constexpr int timer_sets = 4;

		// Pass 1 (view): each pixel of the stream picture looks along its ray from the steadied camera;
		// turned into the eye's camera, the ray lands on the eye's image (and the HUD layer, drawn for
		// the same view), which is read there. Pass 2 (blit): the picture into the back buffer's
		// letterbox rectangle.
		constexpr const char shader_source[] = R"(
cbuffer stream : register(b0)
{
	float4 row0, row1, row2; // a ray from the stream camera, turned into the eye's camera
	float4 extent;           // xy: half width and height of the stream picture as tangents; z: 1 = HUD on; w: HUD rows used (top part)
	float4 edges;            // the eye image's left, right, top and bottom edges as tangents
};
Texture2D source : register(t0);
Texture2D hud : register(t1);
SamplerState linear_clamp : register(s0);
struct vertex { float4 position : SV_Position; float2 uv : TEXCOORD0; };
vertex vs(uint id : SV_VertexID)
{
	vertex v;
	v.uv = float2((id << 1) & 2, id & 2);
	v.position = float4(v.uv * float2(2, -2) + float2(-1, 1), 0, 1);
	return v;
}
float4 view(vertex v) : SV_Target
{
	float3 ray = float3((v.uv.x * 2 - 1) * extent.x, (1 - v.uv.y * 2) * extent.y, -1);
	float3 r = float3(dot(row0.xyz, ray), dot(row1.xyz, ray), dot(row2.xyz, ray));
	if (r.z > -1e-4)
		return float4(0, 0, 0, 1);
	float2 t = r.xy / -r.z;
	float2 p = float2((t.x - edges.x) / (edges.y - edges.x), (edges.z - t.y) / (edges.z - edges.w));
	if (any(p < 0) || any(p > 1))
		return float4(0, 0, 0, 1);
	float3 c = source.SampleLevel(linear_clamp, p, 0).rgb;
	if (extent.z > 0.5 && p.y < extent.w)
	{
		float4 h = hud.SampleLevel(linear_clamp, p, 0); // premultiplied
		c = c * (1 - h.a) + h.rgb;
	}
	return float4(c, 1);
}
float4 blit(vertex v) : SV_Target
{
	return float4(source.SampleLevel(linear_clamp, v.uv, 0).rgb, 1);
}
)";

		struct constants
		{
			float rows[3][4];
			float extent[4];
			float edges[4];
		};

		struct sample
		{
			ULONGLONG at{};
			float eye_yaw{}, eye_pitch{}, eye_roll{};
			float yaw{}, pitch{};
			float kept{};
		};

		// Settings (present thread).
		bool enabled{ true };
		float fov_degrees{ 80.0f };
		float smoothing{ 0.3f };
		bool show_hud{ true };

		// GPU objects (present thread only).
		ComPtr<ID3D11Device> device;
		ComPtr<ID3D11VertexShader> vertex_shader;
		ComPtr<ID3D11PixelShader> view_shader;
		ComPtr<ID3D11PixelShader> blit_shader;
		ComPtr<ID3D11SamplerState> sampler;
		ComPtr<ID3D11Buffer> constant_buffer;
		bool failed{};
		ComPtr<ID3D11Texture2D> source;          // the eye image the view is read from
		ComPtr<ID3D11ShaderResourceView> source_view;
		ComPtr<ID3D11Texture2D> aer_left;        // stereo_render = aer: the last left eye
		ComPtr<ID3D11Texture2D> hud_source;
		ComPtr<ID3D11ShaderResourceView> hud_view;
		ComPtr<ID3D11Texture2D> picture;         // the 16:9 stream picture
		ComPtr<ID3D11RenderTargetView> picture_target;
		ComPtr<ID3D11ShaderResourceView> picture_view;

		// The eye image's view (present thread).
		pose image_pose;
		fov image_fov;
		bool image_valid{};
		std::uint64_t last_pair{};
		std::uint64_t last_copy{ ~0ull };
		ULONGLONG image_at{};
		std::uint64_t last_hud_frame{};
		std::uint64_t presents{};
		// GPU time of the two passes: timestamp queries read a few frames later, never waited for.
		struct timer_set
		{
			ComPtr<ID3D11Query> disjoint, begin, end;
			bool pending{};
		};
		timer_set timers[timer_sets];
		int timer_next{};
		double gpu_ms{};
		std::uint64_t gpu_samples{};
		std::uint64_t hud_seen_at{};
		LARGE_INTEGER last_draw{};
		stream::steadier steady;

		// Report (pipe thread reads under the lock).
		std::mutex report_lock;
		std::uint64_t drawn{}, errors{};
		const char* state_text{ "idle" };
		const char* source_text{ "-" };
		UINT picture_width{}, picture_height{}, window_width{}, window_height{};
		stream::rect last_rect;
		sample last;
		double cpu_ms{};
		fov reported_fov;
		bool hud_drawn{};
		constexpr std::size_t trace_size = 256;
		sample trace[trace_size];
		std::size_t trace_next{};
		std::atomic<ID3D11Texture2D*> latest_picture{};

		void set_state(const char* text)
		{
			const std::lock_guard guard(report_lock);
			state_text = text;
		}

		DXGI_FORMAT typed(DXGI_FORMAT format)
		{
			switch (format)
			{
			case DXGI_FORMAT_R8G8B8A8_TYPELESS: return DXGI_FORMAT_R8G8B8A8_UNORM;
			case DXGI_FORMAT_B8G8R8A8_TYPELESS: return DXGI_FORMAT_B8G8R8A8_UNORM;
			case DXGI_FORMAT_R10G10B10A2_TYPELESS: return DXGI_FORMAT_R10G10B10A2_UNORM;
			case DXGI_FORMAT_R16G16B16A16_TYPELESS: return DXGI_FORMAT_R16G16B16A16_FLOAT;
			default: return format;
			}
		}

		ComPtr<ID3DBlob> compile(const char* entry, const char* target)
		{
			ComPtr<ID3DBlob> code, messages;
			const HRESULT hr = D3DCompile(shader_source, sizeof(shader_source) - 1, "stream_view", nullptr, nullptr, entry, target,
				D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &code, &messages);
			if (FAILED(hr))
			{
				log::write("stream view: %s compile failed 0x%08lx %s", entry, static_cast<unsigned long>(hr),
					messages ? static_cast<const char*>(messages->GetBufferPointer()) : "");
				return {};
			}
			return code;
		}

		bool create_pipeline(ID3D11Device* d)
		{
			if (device.Get() == d && blit_shader)
			{
				return true;
			}
			if (failed)
			{
				return false;
			}
			device = d;
			source.Reset();
			source_view.Reset();
			aer_left.Reset();
			hud_source.Reset();
			hud_view.Reset();
			picture.Reset();
			picture_target.Reset();
			picture_view.Reset();
			const ComPtr<ID3DBlob> vs = compile("vs", "vs_5_0");
			const ComPtr<ID3DBlob> ps_view = compile("view", "ps_5_0");
			const ComPtr<ID3DBlob> ps_blit = compile("blit", "ps_5_0");
			D3D11_SAMPLER_DESC sampler_desc{};
			sampler_desc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
			sampler_desc.AddressU = sampler_desc.AddressV = sampler_desc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
			sampler_desc.MaxLOD = D3D11_FLOAT32_MAX;
			D3D11_BUFFER_DESC buffer_desc{};
			buffer_desc.ByteWidth = sizeof(constants);
			buffer_desc.Usage = D3D11_USAGE_DEFAULT;
			buffer_desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
			if (!vs || !ps_view || !ps_blit || FAILED(d->CreateVertexShader(vs->GetBufferPointer(), vs->GetBufferSize(), nullptr, &vertex_shader))
				|| FAILED(d->CreatePixelShader(ps_view->GetBufferPointer(), ps_view->GetBufferSize(), nullptr, &view_shader))
				|| FAILED(d->CreatePixelShader(ps_blit->GetBufferPointer(), ps_blit->GetBufferSize(), nullptr, &blit_shader))
				|| FAILED(d->CreateSamplerState(&sampler_desc, &sampler)) || FAILED(d->CreateBuffer(&buffer_desc, nullptr, &constant_buffer)))
			{
				log::write("stream view: pipeline creation failed, stream view off");
				failed = true;
				blit_shader.Reset();
				return false;
			}
			for (timer_set& t : timers)
			{
				D3D11_QUERY_DESC query{ D3D11_QUERY_TIMESTAMP_DISJOINT, 0 };
				D3D11_QUERY_DESC stamp{ D3D11_QUERY_TIMESTAMP, 0 };
				t = {};
				if (FAILED(d->CreateQuery(&query, &t.disjoint)) || FAILED(d->CreateQuery(&stamp, &t.begin)) || FAILED(d->CreateQuery(&stamp, &t.end)))
				{
					t = {};
				}
			}
			log::write("stream view: pipeline ready");
			return true;
		}

		// A shader view of texture, made again only when the texture changes.
		bool view_of(ID3D11Texture2D* texture, ComPtr<ID3D11Texture2D>& held, ComPtr<ID3D11ShaderResourceView>& view)
		{
			if (held.Get() == texture && view)
			{
				return true;
			}
			held = texture;
			view.Reset();
			D3D11_TEXTURE2D_DESC desc{};
			texture->GetDesc(&desc);
			if (!(desc.BindFlags & D3D11_BIND_SHADER_RESOURCE) || desc.SampleDesc.Count != 1)
			{
				held.Reset();
				return false;
			}
			D3D11_SHADER_RESOURCE_VIEW_DESC view_desc{};
			view_desc.Format = typed(desc.Format);
			view_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
			view_desc.Texture2D.MipLevels = 1;
			if (FAILED(device->CreateShaderResourceView(texture, &view_desc, &view)))
			{
				held.Reset();
				return false;
			}
			return true;
		}

		bool ensure_picture(UINT width, UINT height, DXGI_FORMAT format)
		{
			if (picture)
			{
				D3D11_TEXTURE2D_DESC have{};
				picture->GetDesc(&have);
				if (have.Width == width && have.Height == height && have.Format == format)
				{
					return true;
				}
			}
			latest_picture = nullptr;
			picture.Reset();
			picture_target.Reset();
			picture_view.Reset();
			D3D11_TEXTURE2D_DESC desc{};
			desc.Width = width;
			desc.Height = height;
			desc.MipLevels = desc.ArraySize = 1;
			desc.Format = format;
			desc.SampleDesc.Count = 1;
			desc.Usage = D3D11_USAGE_DEFAULT;
			desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
			if (FAILED(device->CreateTexture2D(&desc, nullptr, &picture)) || FAILED(device->CreateRenderTargetView(picture.Get(), nullptr, &picture_target))
				|| FAILED(device->CreateShaderResourceView(picture.Get(), nullptr, &picture_view)))
			{
				picture.Reset();
				picture_target.Reset();
				picture_view.Reset();
				return false;
			}
			return true;
		}

		// stereo_render = aer: the back buffer holds one eye per frame; the left ones are kept.
		bool keep_aer_left(ID3D11DeviceContext* context, ID3D11Texture2D* back_buffer, const D3D11_TEXTURE2D_DESC& desc)
		{
			D3D11_TEXTURE2D_DESC have{};
			if (aer_left)
			{
				aer_left->GetDesc(&have);
			}
			if (!aer_left || have.Width != desc.Width || have.Height != desc.Height || have.Format != desc.Format)
			{
				aer_left.Reset();
				D3D11_TEXTURE2D_DESC copy = desc;
				copy.BindFlags = D3D11_BIND_SHADER_RESOURCE;
				copy.MiscFlags = 0;
				copy.CPUAccessFlags = 0;
				copy.Usage = D3D11_USAGE_DEFAULT;
				copy.MipLevels = copy.ArraySize = 1;
				if (FAILED(device->CreateTexture2D(&copy, nullptr, &aer_left)))
				{
					return false;
				}
			}
			context->CopyResource(aer_left.Get(), back_buffer);
			return true;
		}

		float roll_of(const float (&q)[4])
		{
			stream::matrix m;
			stream::to_matrix(q, m);
			// Positive: the head tilts to its right (its right axis goes down).
			return std::atan2(-m[1][0], m[1][1]);
		}

		// The oldest finished timing, if any, into the running average.
		void read_timers(ID3D11DeviceContext* context)
		{
			for (timer_set& t : timers)
			{
				if (!t.pending)
				{
					continue;
				}
				D3D11_QUERY_DATA_TIMESTAMP_DISJOINT clock{};
				UINT64 begin = 0, end = 0;
				if (context->GetData(t.disjoint.Get(), &clock, sizeof(clock), D3D11_ASYNC_GETDATA_DONOTFLUSH) != S_OK
					|| context->GetData(t.begin.Get(), &begin, sizeof(begin), D3D11_ASYNC_GETDATA_DONOTFLUSH) != S_OK
					|| context->GetData(t.end.Get(), &end, sizeof(end), D3D11_ASYNC_GETDATA_DONOTFLUSH) != S_OK)
				{
					continue;
				}
				t.pending = false;
				if (clock.Disjoint || !clock.Frequency || end < begin)
				{
					continue;
				}
				const double ms = static_cast<double>(end - begin) * 1000.0 / static_cast<double>(clock.Frequency);
				const std::lock_guard guard(report_lock);
				gpu_ms = gpu_samples ? gpu_ms + 0.05 * (ms - gpu_ms) : ms;
				++gpu_samples;
			}
		}

		void lose_image(const char* why)
		{
			image_valid = false;
			steady.reset();
			last_draw.QuadPart = 0;
			latest_picture = nullptr;
			set_state(why);
		}
	}

	void set_stream_view(bool on, float horizontal_degrees, float smoothing_seconds, bool hud)
	{
		enabled = on;
		fov_degrees = horizontal_degrees;
		smoothing = smoothing_seconds;
		show_hud = hud;
		steady.configure(smoothing_seconds, speed_up);
	}

	void stream_view_on_present(IDXGISwapChain* swap_chain, bool menu)
	{
		++presents;
		if (!enabled)
		{
			if (image_valid || latest_picture.load())
			{
				lose_image("off");
			}
			set_state("off");
			return;
		}
		LARGE_INTEGER start{}, frequency{};
		QueryPerformanceCounter(&start);
		QueryPerformanceFrequency(&frequency);

		const rendered_view tag = latest_rendered_view();
		if (!tag.stereo || menu)
		{
			lose_image(menu ? "menu over the level: the game's picture" : "flat frame: the game's picture");
			return;
		}
		ComPtr<ID3D11Texture2D> back_buffer;
		ComPtr<ID3D11Device> d;
		if (FAILED(swap_chain->GetBuffer(0, IID_PPV_ARGS(&back_buffer))) || FAILED(swap_chain->GetDevice(IID_PPV_ARGS(&d))))
		{
			return;
		}
		D3D11_TEXTURE2D_DESC back_desc{};
		back_buffer->GetDesc(&back_desc);
		if (back_desc.SampleDesc.Count != 1 || !(back_desc.BindFlags & D3D11_BIND_RENDER_TARGET) || !create_pipeline(d.Get()))
		{
			lose_image("unsupported back buffer");
			return;
		}
		ComPtr<ID3D11DeviceContext> context;
		d->GetImmediateContext(&context);

		// The left eye's image and the view it was rendered for.
		const ULONGLONG now = GetTickCount64();
		ID3D11Texture2D* eye_image = nullptr;
		const char* const source_name = tag.pair ? "pair" : "aer";
		if (tag.pair)
		{
			const std::uint64_t pairs = eye_pass_pairs();
			if (pairs != last_pair)
			{
				last_pair = pairs;
				image_pose = tag.eye_pose;
				image_fov = tag.rendered_fov;
				image_valid = true;
				image_at = now;
			}
			eye_image = eye_pass_image(0);
		}
		else
		{
			if (tag.eye == 0 && tag.copy_index != last_copy && keep_aer_left(context.Get(), back_buffer.Get(), back_desc))
			{
				last_copy = tag.copy_index;
				image_pose = tag.eye_pose;
				image_fov = tag.rendered_fov;
				image_valid = true;
				image_at = now;
			}
			eye_image = aer_left.Get();
		}
		if (!image_valid || !eye_image || now - image_at > stale_ms)
		{
			lose_image("no recent left eye image");
			return;
		}
		if (!view_of(eye_image, source, source_view))
		{
			++errors;
			lose_image("eye image not readable");
			return;
		}

		// HUD layer, while the capture delivers it.
		const hud_frame hud = latest_hud();
		if (hud.frame != last_hud_frame)
		{
			last_hud_frame = hud.frame;
			hud_seen_at = presents;
		}
		const bool hud_on = show_hud && hud.texture && presents - hud_seen_at <= hud_stale_presents && view_of(hud.texture, hud_source, hud_view);

		// Where the picture goes in the window, and its size: as many pixels as the window shows of it.
		unsigned client_w = back_desc.Width, client_h = back_desc.Height;
		hooks::real_client_size(client_w, client_h);
		const stream::rect box = stream::letterbox(static_cast<float>(client_w) / static_cast<float>(client_h), picture_aspect);
		const UINT height = std::clamp<UINT>(static_cast<UINT>(std::lround(box.height * static_cast<float>(client_h))), 360, 2160);
		const UINT width = static_cast<UINT>(std::lround(static_cast<float>(height) * picture_aspect));
		const DXGI_FORMAT format = typed(back_desc.Format);
		if (!ensure_picture(width, height, format))
		{
			++errors;
			lose_image("picture texture failed");
			return;
		}
		D3D11_RENDER_TARGET_VIEW_DESC target_desc{};
		target_desc.Format = format;
		target_desc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
		ComPtr<ID3D11RenderTargetView> back_target; // per frame: a held reference would block ResizeBuffers
		if (FAILED(d->CreateRenderTargetView(back_buffer.Get(), &target_desc, &back_target)))
		{
			++errors;
			lose_image("back buffer not drawable");
			return;
		}

		// The steadied camera.
		const float seconds = last_draw.QuadPart ? static_cast<float>(start.QuadPart - last_draw.QuadPart) / static_cast<float>(frequency.QuadPart) : 0.0f;
		last_draw = start;
		const stream::look eye_look = stream::look_of(image_pose.orientation);
		const stream::look steadied = steady.update(eye_look, seconds);
		const stream::extent extent = stream::extent_of(fov_degrees, picture_aspect);
		const stream::edges edges = stream::edges_of_fov(image_fov.left, image_fov.right, image_fov.up, image_fov.down);
		float camera[4];
		const float kept = stream::aim(image_pose.orientation, steadied, extent, edges, camera);
		stream::matrix to_eye;
		stream::between(image_pose.orientation, camera, to_eye);

		constants values{};
		for (int row = 0; row < 3; ++row)
		{
			for (int column = 0; column < 3; ++column)
			{
				values.rows[row][column] = to_eye[row][column];
			}
		}
		values.extent[0] = extent.x;
		values.extent[1] = extent.y;
		values.extent[2] = hud_on ? 1.0f : 0.0f;
		values.extent[3] = hud_main_rows;
		values.edges[0] = edges.left;
		values.edges[1] = edges.right;
		values.edges[2] = edges.up;
		values.edges[3] = edges.down;
		context->UpdateSubresource(constant_buffer.Get(), 0, nullptr, &values, 0, 0);

		read_timers(context.Get());
		timer_set& timer = timers[timer_next];
		const bool timed = timer.disjoint && !timer.pending;
		if (timed)
		{
			context->Begin(timer.disjoint.Get());
			context->End(timer.begin.Get());
		}

		saved_pipeline state;
		state.save(context.Get());
		ID3D11ShaderResourceView* const no_views[2]{};
		context->PSSetShaderResources(0, 2, no_views);
		context->OMSetBlendState(nullptr, nullptr, 0xffffffffu);
		context->OMSetDepthStencilState(nullptr, 0);
		context->RSSetState(nullptr);
		context->IASetInputLayout(nullptr);
		context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		context->VSSetShader(vertex_shader.Get(), nullptr, 0);
		context->GSSetShader(nullptr, nullptr, 0);
		context->HSSetShader(nullptr, nullptr, 0);
		context->DSSetShader(nullptr, nullptr, 0);
		ID3D11SamplerState* const s = sampler.Get();
		context->PSSetSamplers(0, 1, &s);
		ID3D11Buffer* const b = constant_buffer.Get();
		context->PSSetConstantBuffers(0, 1, &b);

		// Pass 1: the steadied view into the picture.
		ID3D11RenderTargetView* target = picture_target.Get();
		context->OMSetRenderTargets(1, &target, nullptr);
		const D3D11_VIEWPORT picture_port{ 0, 0, static_cast<float>(width), static_cast<float>(height), 0, 1 };
		context->RSSetViewports(1, &picture_port);
		context->PSSetShader(view_shader.Get(), nullptr, 0);
		ID3D11ShaderResourceView* const views[2]{ source_view.Get(), hud_on ? hud_view.Get() : nullptr };
		context->PSSetShaderResources(0, 2, views);
		context->Draw(3, 0);
		context->PSSetShaderResources(0, 2, no_views);

		// Pass 2: the picture into the back buffer, black bars around it.
		target = back_target.Get();
		context->OMSetRenderTargets(1, &target, nullptr);
		const float black[4]{ 0, 0, 0, 1 };
		if (box.width < 0.999f || box.height < 0.999f)
		{
			context->ClearRenderTargetView(target, black);
		}
		const D3D11_VIEWPORT back_port{ box.x * static_cast<float>(back_desc.Width), box.y * static_cast<float>(back_desc.Height),
			box.width * static_cast<float>(back_desc.Width), box.height * static_cast<float>(back_desc.Height), 0, 1 };
		context->RSSetViewports(1, &back_port);
		context->PSSetShader(blit_shader.Get(), nullptr, 0);
		ID3D11ShaderResourceView* const picture_source = picture_view.Get();
		context->PSSetShaderResources(0, 1, &picture_source);
		context->Draw(3, 0);
		context->PSSetShaderResources(0, 2, no_views);
		state.restore(context.Get());
		if (timed)
		{
			context->End(timer.end.Get());
			context->End(timer.disjoint.Get());
			timer.pending = true;
			timer_next = (timer_next + 1) % timer_sets;
		}
		latest_picture = picture.Get();

		LARGE_INTEGER end{};
		QueryPerformanceCounter(&end);
		const float camera_roll = roll_of(camera);
		const stream::look camera_look = stream::look_of(camera);
		const sample entry{ now, eye_look.yaw * degrees, eye_look.pitch * degrees, roll_of(image_pose.orientation) * degrees, camera_look.yaw * degrees,
			camera_look.pitch * degrees, kept };
		const std::lock_guard guard(report_lock);
		++drawn;
		source_text = source_name;
		state_text = kept < 1.0f && std::fabs(camera_roll) > 0.002f ? "drawn (roll kept: head rolled far)" : "drawn";
		picture_width = width;
		picture_height = height;
		window_width = client_w;
		window_height = client_h;
		last_rect = box;
		reported_fov = image_fov;
		last = entry;
		hud_drawn = hud_on;
		cpu_ms = static_cast<double>(end.QuadPart - start.QuadPart) * 1000.0 / static_cast<double>(frequency.QuadPart);
		trace[trace_next++ % trace_size] = entry;
	}

	ID3D11Texture2D* stream_view_image()
	{
		return latest_picture.load();
	}

	std::string stream_view_command(const std::string& argument)
	{
		const std::lock_guard guard(report_lock);
		char line[640];
		if (argument == "trace")
		{
			// Oldest first: ms since the first line, eye yaw pitch roll, stream yaw pitch, kept.
			std::ostringstream text;
			const std::size_t count = std::min(trace_next, trace_size);
			const std::size_t first = trace_next - count;
			const ULONGLONG origin = count ? trace[first % trace_size].at : 0;
			text << "ms eye_yaw eye_pitch eye_roll stream_yaw stream_pitch kept\n";
			for (std::size_t i = first; i < trace_next; ++i)
			{
				const sample& e = trace[i % trace_size];
				std::snprintf(line, sizeof(line), "%llu %.2f %.2f %.2f %.2f %.2f %.2f\n", e.at - origin, e.eye_yaw, e.eye_pitch, e.eye_roll, e.yaw, e.pitch, e.kept);
				text << line;
			}
			return text.str();
		}
		std::snprintf(line, sizeof(line),
			"stream_view=%d state=%s source=%s drawn=%llu errors=%llu picture=%ux%u window=%ux%u box=%.3f,%.3f %.3fx%.3f fov=%.0f smoothing=%.2f hud=%d\n"
			"eye_fov left=%.1f right=%.1f up=%.1f down=%.1f\n"
			"eye yaw=%.2f pitch=%.2f roll=%.2f stream yaw=%.2f pitch=%.2f kept=%.2f cpu_ms=%.3f gpu_ms=%.3f (%llu samples)",
			enabled ? 1 : 0, state_text, source_text, drawn, errors, picture_width, picture_height, window_width, window_height, last_rect.x, last_rect.y,
			last_rect.width, last_rect.height, fov_degrees, smoothing, hud_drawn ? 1 : 0, reported_fov.left * degrees, reported_fov.right * degrees,
			reported_fov.up * degrees, reported_fov.down * degrees, last.eye_yaw, last.eye_pitch, last.eye_roll, last.yaw, last.pitch,
			last.kept, cpu_ms, gpu_ms, gpu_samples);
		return line;
	}
}
