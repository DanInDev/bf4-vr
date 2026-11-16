#include "stereo/muzzle_quads.hpp"

#include <windows.h>
#include <d3dcompiler.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <mutex>
#include <string>

#include "body/arm_pose.hpp"
#include "common/log.hpp"
#include "input/gamepad.hpp"
#include "stereo/muzzle_move.hpp"
#include "weapon/physical_reload.hpp"

namespace bfvr::stereo::muzzle_quads
{
	namespace
	{
		std::atomic<bool> enabled{ true };
		std::atomic<UINT> quad_stride{ 48 };       // BF3's particle vertex (The Great Destroyer); BF4's from the survey
		constexpr UINT quads_most = 4096;
		constexpr UINT ring_bytes = 1024 * 1024;   // the particles' dynamic vertex buffer is at least this big
		constexpr ULONGLONG after_shot_ms = 3000;  // smoke still rising
		constexpr ULONGLONG live_ms = 200;         // the move follows the gun this long after the last shot, then holds
		constexpr float grow_ms = 600.0f;          // the region grows from the first flash's size to its whole size
		constexpr float flash_along = 0.9f;        // FireEffects1p offset along Wep_Aim (SCAR-H 0.925, AK-12 0.890)

		std::atomic<ULONGLONG> trigger_at{};
		std::atomic<ULONGLONG> opened_at{};
		std::atomic<std::uint64_t> moved_batches{}, not_moved{}, candidates{};

		ULONGLONG firing_ago()
		{
			const ULONGLONG now = GetTickCount64();
			if (input::latest().right_trigger > 128)
			{
				// The trigger: the first flash comes before the count goes down.
				const ULONGLONG was = trigger_at.exchange(now);
				if (now - std::max<ULONGLONG>(was, weapon::last_shot_tick()) >= after_shot_ms - 500)
				{
					opened_at = now;
				}
			}
			const ULONGLONG last = std::max<ULONGLONG>(trigger_at.load(std::memory_order_relaxed), weapon::last_shot_tick());
			return last ? now - last : ~0ull;
		}

		float fade(ULONGLONG ago)
		{
			return ago >= after_shot_ms ? 0.0f : std::min(1.0f, static_cast<float>(after_shot_ms - ago) / 1000.0f);
		}

		// The move as it is while shooting, held after.
		std::mutex move_lock;
		bool have_move{};
		muzzle_move::move current_move;

		bool move_now(ULONGLONG ago, muzzle_move::move& out)
		{
			const std::lock_guard guard(move_lock);
			if (ago < live_ms || !have_move)
			{
				body::muzzle_frame game, drawn;
				have_move = body::muzzle_frames(game, drawn) && muzzle_move::make(game.position, game.rotation, drawn.position, drawn.rotation, flash_along, current_move);
				if (!have_move)
				{
					return false;
				}
			}
			out = current_move;
			return true;
		}

		bool blended(ID3D11DeviceContext* context)
		{
			ID3D11BlendState* blend = nullptr;
			FLOAT factor[4];
			UINT mask = 0;
			context->OMGetBlendState(&blend, factor, &mask);
			bool on = false;
			if (blend)
			{
				D3D11_BLEND_DESC desc;
				blend->GetDesc(&desc);
				on = desc.RenderTarget[0].BlendEnable != FALSE;
				blend->Release();
			}
			return on;
		}

		// ---- The survey ----
		std::atomic<ULONGLONG> survey_until{};
		struct kind_stats
		{
			std::uint64_t draws{};
			std::uint64_t blended{};
			std::uint64_t read{};
			float nearest{ 1e9f };
			float nearest_along{};
			UINT largest_count{};
		};
		std::mutex survey_lock;
		std::map<std::uint64_t, kind_stats> survey;
		std::uint64_t survey_reads{};

		// The first float3 of each quad's first vertex, up to 16 quads (a stall: research only).
		int read_quads(ID3D11DeviceContext* context, ID3D11Buffer* stream, UINT offset, INT base, UINT stride, UINT quads, float (&points)[16][3])
		{
			D3D11_BUFFER_DESC desc;
			stream->GetDesc(&desc);
			quads = std::min<UINT>(quads, 16);
			const UINT left = offset + static_cast<UINT>(std::max(base, 0)) * stride;
			const UINT bytes = quads * 4 * stride;
			if (base < 0 || left + bytes > desc.ByteWidth || !bytes)
			{
				return 0;
			}
			ID3D11Device* device = nullptr;
			context->GetDevice(&device);
			if (!device)
			{
				return 0;
			}
			D3D11_BUFFER_DESC staging{ (bytes + 15) & ~15u, D3D11_USAGE_STAGING, 0, D3D11_CPU_ACCESS_READ, 0, 0 };
			ID3D11Buffer* copy = nullptr;
			int got = 0;
			if (SUCCEEDED(device->CreateBuffer(&staging, nullptr, &copy)))
			{
				const D3D11_BOX box{ left, 0, 0, left + bytes, 1, 1 };
				context->CopySubresourceRegion(copy, 0, 0, 0, 0, stream, 0, &box);
				D3D11_MAPPED_SUBRESOURCE mapped{};
				if (SUCCEEDED(context->Map(copy, 0, D3D11_MAP_READ, 0, &mapped)))
				{
					for (UINT q = 0; q < quads; ++q)
					{
						std::memcpy(points[q], static_cast<const std::uint8_t*>(mapped.pData) + q * 4 * stride, sizeof(points[q]));
					}
					context->Unmap(copy, 0);
					got = static_cast<int>(quads);
				}
				copy->Release();
			}
			device->Release();
			return got;
		}

		void survey_draw(ID3D11DeviceContext* context, UINT count, UINT start, INT base)
		{
			if (!count || count % 6 != 0 || start % 6 != 0)
			{
				return;
			}
			ID3D11Buffer* stream = nullptr;
			UINT stride = 0, offset = 0;
			context->IAGetVertexBuffers(0, 1, &stream, &stride, &offset);
			if (!stream)
			{
				return;
			}
			D3D11_BUFFER_DESC desc;
			stream->GetDesc(&desc);
			if (desc.Usage != D3D11_USAGE_DYNAMIC)
			{
				stream->Release();
				return;
			}
			const bool blend = blended(context);
			const std::uint64_t key = (static_cast<std::uint64_t>(stride) << 40) | (static_cast<std::uint64_t>(desc.ByteWidth >> 10) << 1) | (blend ? 1u : 0u);
			float nearest = 1e9f, nearest_along = 0.0f;
			bool read = false;
			muzzle_move::move m;
			if (survey_reads < 400 && stride >= 12 && move_now(0, m))
			{
				float points[16][3];
				const int got = read_quads(context, stream, offset, base, stride, count / 6, points);
				read = got > 0;
				++survey_reads;
				for (int q = 0; q < got; ++q)
				{
					const float rel[3] = { points[q][0] - m.muzzle[0], points[q][1] - m.muzzle[1], points[q][2] - m.muzzle[2] };
					const float distance = std::sqrt(rel[0] * rel[0] + rel[1] * rel[1] + rel[2] * rel[2]);
					if (distance < nearest)
					{
						nearest = distance;
						nearest_along = rel[0] * m.axis[0] + rel[1] * m.axis[1] + rel[2] * m.axis[2];
					}
				}
			}
			stream->Release();
			const std::lock_guard guard(survey_lock);
			kind_stats& k = survey[key];
			++k.draws;
			k.blended += blend ? 1 : 0;
			k.read += read ? 1 : 0;
			k.largest_count = std::max(k.largest_count, count);
			if (nearest < k.nearest)
			{
				k.nearest = nearest;
				k.nearest_along = nearest_along;
			}
		}

		// ---- The move (The Great Destroyer's kQuadShader, the same maths as muzzle_move::weight) ----
		constexpr char quad_shader[] = R"(
cbuffer Move : register(b0) { float4 d0, d1, d2, muzzle, axis, reach; uint quads, stride, pad0, pad1; };
RWByteAddressBuffer vertices : register(u0);
[numthreads(64, 1, 1)] void main(uint3 id : SV_DispatchThreadID) {
    if (id.x >= quads)
        return;
    const uint first = id.x * 4 * stride;
    float3 p[4];
    float3 centre = 0;
    for (uint k = 0; k < 4; ++k) {
        p[k] = asfloat(vertices.Load3(first + k * stride));
        centre += p[k] / 4;
    }
    const float3 rel = centre - muzzle.xyz;
    const float along = dot(rel, axis.xyz);
    const float across = length(rel - along * axis.xyz);
    const float w = axis.w * saturate((reach.y - across) / (reach.y - reach.x)) * saturate((reach.w - along) / (reach.w - reach.z)) *
                    saturate((along + muzzle.w + 0.4) / 0.4);
    if (w <= 0)
        return;
    for (uint v = 0; v < 4; ++v) {
        const float3 moved = float3(dot(d0.xyz, p[v]) + d0.w, dot(d1.xyz, p[v]) + d1.w, dot(d2.xyz, p[v]) + d2.w);
        vertices.Store3(first + v * stride, asuint(lerp(p[v], moved, w)));
    }
}
)";
		std::mutex shader_lock;
		ID3D11ComputeShader* shader = nullptr;
		bool shader_failed{};
		struct buffers
		{
			ID3D11Buffer* vertices = nullptr;
			ID3D11Buffer* move = nullptr;
			ID3D11UnorderedAccessView* view = nullptr;
			UINT stride{};
		};
		thread_local buffers own;

		bool ensure(ID3D11Device* device, UINT stride)
		{
			{
				const std::lock_guard guard(shader_lock);
				if (!shader && !shader_failed)
				{
					ID3DBlob* code = nullptr;
					ID3DBlob* errors = nullptr;
					const bool ok = SUCCEEDED(D3DCompile(quad_shader, sizeof(quad_shader) - 1, "muzzle quads", nullptr, nullptr, "main", "cs_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL3,
										0, &code, &errors))
						&& SUCCEEDED(device->CreateComputeShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr, &shader));
					if (errors)
					{
						log::write("muzzle quads: shader: %s", static_cast<const char*>(errors->GetBufferPointer()));
						errors->Release();
					}
					if (code)
					{
						code->Release();
					}
					shader_failed = !ok;
				}
				if (!shader)
				{
					return false;
				}
			}
			if (own.view && own.stride == stride)
			{
				return true;
			}
			for (IUnknown* p : { static_cast<IUnknown*>(own.vertices), static_cast<IUnknown*>(own.move), static_cast<IUnknown*>(own.view) })
			{
				if (p)
				{
					p->Release();
				}
			}
			own = {};
			const UINT bytes = quads_most * 4 * stride;
			D3D11_BUFFER_DESC vertices{ bytes, D3D11_USAGE_DEFAULT, D3D11_BIND_VERTEX_BUFFER | D3D11_BIND_UNORDERED_ACCESS, 0, D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS, 0 };
			D3D11_BUFFER_DESC move{ 112, D3D11_USAGE_DYNAMIC, D3D11_BIND_CONSTANT_BUFFER, D3D11_CPU_ACCESS_WRITE, 0, 0 };
			D3D11_UNORDERED_ACCESS_VIEW_DESC uav{};
			uav.Format = DXGI_FORMAT_R32_TYPELESS;
			uav.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
			uav.Buffer.NumElements = bytes / 4;
			uav.Buffer.Flags = D3D11_BUFFER_UAV_FLAG_RAW;
			if (SUCCEEDED(device->CreateBuffer(&vertices, nullptr, &own.vertices)) && SUCCEEDED(device->CreateBuffer(&move, nullptr, &own.move))
				&& SUCCEEDED(device->CreateUnorderedAccessView(own.vertices, &uav, &own.view)))
			{
				own.stride = stride;
				return true;
			}
			return false;
		}

		bool quad_batch(ID3D11DeviceContext* context, UINT count, UINT start, UINT stride_wanted)
		{
			if (!count || count % 6 != 0 || start % 6 != 0 || count / 6 > quads_most)
			{
				return false;
			}
			ID3D11Buffer* stream = nullptr;
			UINT stride = 0, offset = 0;
			context->IAGetVertexBuffers(0, 1, &stream, &stride, &offset);
			if (!stream)
			{
				return false;
			}
			D3D11_BUFFER_DESC desc;
			stream->GetDesc(&desc);
			stream->Release();
			return stride == stride_wanted && desc.Usage == D3D11_USAGE_DYNAMIC && desc.ByteWidth >= ring_bytes && blended(context);
		}

		bool draw_moved(ID3D11DeviceContext* context, UINT count, UINT start, INT base, ULONGLONG ago, draw_indexed_fn original)
		{
			muzzle_move::move m;
			if (!move_now(ago, m))
			{
				return false;
			}
			const UINT stride_wanted = quad_stride.load(std::memory_order_relaxed);
			ID3D11Device* device = nullptr;
			context->GetDevice(&device);
			const bool ready = device && ensure(device, stride_wanted);
			if (device)
			{
				device->Release();
			}
			ID3D11Buffer* stream = nullptr;
			UINT stride = 0, offset = 0;
			context->IAGetVertexBuffers(0, 1, &stream, &stride, &offset);
			D3D11_MAPPED_SUBRESOURCE mapped{};
			if (!ready || !stream || FAILED(context->Map(own.move, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
			{
				if (stream)
				{
					stream->Release();
				}
				++not_moved;
				return false;
			}
			const UINT quads = count / 6;
			auto* cb = static_cast<float*>(mapped.pData);
			std::memcpy(cb, m.rows, 12 * sizeof(float));
			const muzzle_move::shape whole;
			const float grown = std::min(static_cast<float>(GetTickCount64() - opened_at.load(std::memory_order_relaxed)) / grow_ms, 1.0f);
			const auto grow = [grown](float from, float to) { from = std::min(from, to); return from + (to - from) * grown; };
			cb[12] = m.muzzle[0];
			cb[13] = m.muzzle[1];
			cb[14] = m.muzzle[2];
			cb[15] = whole.behind;
			cb[16] = m.axis[0];
			cb[17] = m.axis[1];
			cb[18] = m.axis[2];
			cb[19] = fade(ago);
			const float across = grow(0.15f, whole.across_full);
			const float along = grow(1.0f, whole.along_full);
			cb[20] = across;
			cb[21] = across + grow(0.15f, whole.across_none - whole.across_full);
			cb[22] = along;
			cb[23] = along + grow(0.5f, whole.along_none - whole.along_full);
			auto* counts = reinterpret_cast<UINT*>(cb + 24);
			counts[0] = quads;
			counts[1] = stride_wanted;
			counts[2] = counts[3] = 0;
			context->Unmap(own.move, 0);
			// Each batch's vertices start at its stream offset plus base; its indices from 0 (quads 0 1 2, 4 5 6, ...).
			const UINT left = offset + static_cast<UINT>(base) * stride_wanted;
			const D3D11_BOX box{ left, 0, 0, left + quads * 4 * stride_wanted, 1, 1 };
			D3D11_BUFFER_DESC stream_desc;
			stream->GetDesc(&stream_desc);
			if (box.right > stream_desc.ByteWidth || base < 0)
			{
				stream->Release();
				++not_moved;
				return false;
			}
			context->CopySubresourceRegion(own.vertices, 0, 0, 0, 0, stream, 0, &box);
			ID3D11ComputeShader* cs = nullptr;
			ID3D11UnorderedAccessView* uav = nullptr;
			ID3D11Buffer* cbuffer = nullptr;
			context->CSGetShader(&cs, nullptr, nullptr);
			context->CSGetUnorderedAccessViews(0, 1, &uav);
			context->CSGetConstantBuffers(0, 1, &cbuffer);
			context->CSSetShader(shader, nullptr, 0);
			const UINT keep = UINT_MAX;
			context->CSSetUnorderedAccessViews(0, 1, &own.view, &keep);
			context->CSSetConstantBuffers(0, 1, &own.move);
			context->Dispatch((quads + 63) / 64, 1, 1);
			ID3D11UnorderedAccessView* none = nullptr;
			context->CSSetUnorderedAccessViews(0, 1, &none, &keep);
			context->CSSetShader(cs, nullptr, 0);
			context->CSSetUnorderedAccessViews(0, 1, &uav, &keep);
			context->CSSetConstantBuffers(0, 1, &cbuffer);
			for (IUnknown* p : { static_cast<IUnknown*>(cs), static_cast<IUnknown*>(uav), static_cast<IUnknown*>(cbuffer) })
			{
				if (p)
				{
					p->Release();
				}
			}
			const UINT zero = 0;
			context->IASetVertexBuffers(0, 1, &own.vertices, &stride_wanted, &zero);
			original(context, count, start, 0); // the batch's vertices now start the copy
			context->IASetVertexBuffers(0, 1, &stream, &stride, &offset);
			stream->Release();
			++moved_batches;
			return true;
		}
	}

	bool draw(ID3D11DeviceContext* context, UINT count, UINT start, INT base, draw_indexed_fn original)
	{
		if (GetTickCount64() < survey_until.load(std::memory_order_relaxed))
		{
			survey_draw(context, count, start, base);
		}
		if (!enabled.load(std::memory_order_relaxed) || !original)
		{
			return false;
		}
		const ULONGLONG ago = firing_ago();
		if (ago >= after_shot_ms || !quad_batch(context, count, start, quad_stride.load(std::memory_order_relaxed)))
		{
			return false;
		}
		++candidates;
		return draw_moved(context, count, start, base, ago, original);
	}

	void set_on(bool on)
	{
		enabled = on;
	}

	std::string pipe_command(const std::string& argument)
	{
		unsigned value = 0;
		if (sscanf_s(argument.c_str(), "survey %u", &value) == 1)
		{
			{
				const std::lock_guard guard(survey_lock);
				survey.clear();
				survey_reads = 0;
			}
			survey_until = GetTickCount64() + value;
			return "flash survey for " + std::to_string(value) + " ms";
		}
		if (sscanf_s(argument.c_str(), "stride %u", &value) == 1)
		{
			quad_stride = value;
			return "flash stride " + std::to_string(value);
		}
		char line[256];
		std::snprintf(line, sizeof(line), "flash_at_gun=%d stride=%u candidates=%llu moved=%llu not_moved=%llu firing_ago_ms=%llu", enabled.load() ? 1 : 0, quad_stride.load(),
			static_cast<unsigned long long>(candidates.load()), static_cast<unsigned long long>(moved_batches.load()), static_cast<unsigned long long>(not_moved.load()),
			static_cast<unsigned long long>(firing_ago()));
		std::string out = line;
		body::muzzle_frame game, drawn;
		if (body::muzzle_frames(game, drawn))
		{
			std::snprintf(line, sizeof(line), "\ngame_aim=(%.3f %.3f %.3f) drawn_aim=(%.3f %.3f %.3f)", game.position[0], game.position[1], game.position[2], drawn.position[0],
				drawn.position[1], drawn.position[2]);
			out += line;
		}
		const std::lock_guard guard(survey_lock);
		for (const auto& [key, k] : survey)
		{
			std::snprintf(line, sizeof(line), "\nkind stride=%u buffer_kb=%llu blended=%llu draws=%llu read=%llu largest_count=%u nearest_m=%.2f along_m=%.2f",
				static_cast<UINT>(key >> 40), static_cast<unsigned long long>((key >> 1) & 0x7FFFFFFFFFull), static_cast<unsigned long long>(k.blended),
				static_cast<unsigned long long>(k.draws), static_cast<unsigned long long>(k.read), k.largest_count, k.nearest, k.nearest_along);
			out += line;
		}
		return out;
	}
}
