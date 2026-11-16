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
		std::atomic<UINT> quad_stride{ 48 };       // the particle vertex, BF4 as BF3 (survey 2026-11-16: quads 3 cm from the muzzle)
		constexpr UINT quads_most = 4096;
		constexpr UINT ring_bytes = 512 * 1024;    // the particles' dynamic vertex buffer is at least this big (BF4's 768 KB)
		constexpr ULONGLONG after_shot_ms = 3000;  // smoke still rising
		constexpr ULONGLONG live_ms = 200;         // the move follows the gun this long after the last shot, then holds
		constexpr float grow_ms = 600.0f;          // the region grows from the first flash's size to its whole size
		constexpr float flash_along = 0.9f;        // FireEffects1p offset along Wep_Aim (SCAR-H 0.925, AK-12 0.890)

		std::atomic<ULONGLONG> trigger_at{};
		std::atomic<ULONGLONG> opened_at{};
		std::atomic<std::uint64_t> moved_batches{}, not_moved{}, candidates{};
		// pipe "flash check <batches>": how near the moved quads come to the drawn muzzle.
		std::atomic<int> check_left{};
		std::atomic<bool> force_test{};
		std::mutex check_lock;
		float check_before{ 1e9f }, check_after{ 1e9f }, check_after_game{ 1e9f }, check_shift{}, check_weight{}, check_moved{};
		float check_vertices[4][3]{};
		float check_full_before{ 1e9f }, check_full_after_drawn{ 1e9f }, check_full_after_game{ 1e9f };
		int check_indices[12]{ -1 };
		int check_batches{};

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
				body::muzzle_frame aim, game, drawn;
				have_move = body::muzzle_frames(aim, game, drawn)
					&& muzzle_move::make(game.position, game.rotation, drawn.position, drawn.rotation, aim.position, aim.rotation, flash_along, current_move);
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
			float nearest_camera{ 1e9f }; // the same read as camera-relative (world less the tracking origin)
			UINT largest_count{};
		};

		// How near points come to the game's muzzle as world positions (and how far along its barrel), and as positions
		// relative to the camera (BF4 draws some batches about the camera, ~870 m off as world positions).
		void distances(const float (&points)[16][3], int got, const muzzle_move::move& m, float& nearest, float& nearest_along, float& nearest_camera)
		{
			const body::weapon_points wp = body::latest_weapon_points();
			const float camera_muzzle[3] = { m.muzzle[0] - wp.world_origin[0], m.muzzle[1] - wp.world_origin[1], m.muzzle[2] - wp.world_origin[2] };
			for (int i = 0; i < got; ++i)
			{
				const float rel[3] = { points[i][0] - m.muzzle[0], points[i][1] - m.muzzle[1], points[i][2] - m.muzzle[2] };
				const float distance = std::sqrt(rel[0] * rel[0] + rel[1] * rel[1] + rel[2] * rel[2]);
				if (distance < nearest)
				{
					nearest = distance;
					nearest_along = rel[0] * m.axis[0] + rel[1] * m.axis[1] + rel[2] * m.axis[2];
				}
				const float c[3] = { points[i][0] - camera_muzzle[0], points[i][1] - camera_muzzle[1], points[i][2] - camera_muzzle[2] };
				nearest_camera = std::min(nearest_camera, std::sqrt(c[0] * c[0] + c[1] * c[1] + c[2] * c[2]));
			}
		}
		std::mutex survey_lock;
		std::map<std::uint64_t, kind_stats> survey;
		std::uint64_t survey_reads{};

		// The first float3 of each quad's first vertex, up to 16 quads (a stall: research only).
		// Research: every quad's first vertex in a batch (up to quads_most), the index of the one nearest to point and
		// its distance (a read-back stall).
		int nearest_in_batch(ID3D11DeviceContext* context, ID3D11Buffer* buffer, UINT left, UINT stride, UINT quads, const float (&point)[3], float& distance)
		{
			distance = 1e9f;
			D3D11_BUFFER_DESC desc;
			buffer->GetDesc(&desc);
			const UINT bytes = quads * 4 * stride;
			if (!bytes || left + bytes > desc.ByteWidth)
			{
				return -1;
			}
			ID3D11Device* device = nullptr;
			context->GetDevice(&device);
			if (!device)
			{
				return -1;
			}
			int best = -1;
			D3D11_BUFFER_DESC staging{ (bytes + 15) & ~15u, D3D11_USAGE_STAGING, 0, D3D11_CPU_ACCESS_READ, 0, 0 };
			ID3D11Buffer* copy = nullptr;
			if (SUCCEEDED(device->CreateBuffer(&staging, nullptr, &copy)))
			{
				const D3D11_BOX box{ left, 0, 0, left + bytes, 1, 1 };
				context->CopySubresourceRegion(copy, 0, 0, 0, 0, buffer, 0, &box);
				D3D11_MAPPED_SUBRESOURCE mapped{};
				if (SUCCEEDED(context->Map(copy, 0, D3D11_MAP_READ, 0, &mapped)))
				{
					for (UINT q = 0; q < quads; ++q)
					{
						float p[3];
						std::memcpy(p, static_cast<const std::uint8_t*>(mapped.pData) + q * 4 * stride, sizeof(p));
						const float d[3] = { p[0] - point[0], p[1] - point[1], p[2] - point[2] };
						const float dist = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
						if (dist < distance)
						{
							distance = dist;
							best = static_cast<int>(q);
						}
					}
					context->Unmap(copy, 0);
				}
				copy->Release();
			}
			device->Release();
			return best;
		}

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
			float nearest = 1e9f, nearest_along = 0.0f, nearest_camera = 1e9f;
			bool read = false;
			muzzle_move::move m;
			if (survey_reads < 3000 && stride >= 12 && move_now(0, m))
			{
				float points[16][3];
				const int got = read_quads(context, stream, offset, base, stride, count / 6, points);
				read = got > 0;
				++survey_reads;
				distances(points, got, m, nearest, nearest_along, nearest_camera);
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
			k.nearest_camera = std::min(k.nearest_camera, nearest_camera);
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
			UINT bytes{};
		};
		thread_local buffers own;

		bool ensure(ID3D11Device* device, UINT stride, UINT needed)
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
			if (own.view && own.stride == stride && own.bytes >= needed)
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
			const UINT bytes = (needed + 15) & ~15u; // the whole particle buffer: one batch's quads lie all over it
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
				own.bytes = bytes;
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
			ID3D11Buffer* stream = nullptr;
			UINT stride = 0, offset = 0;
			context->IAGetVertexBuffers(0, 1, &stream, &stride, &offset);
			D3D11_BUFFER_DESC stream_desc{};
			if (stream)
			{
				stream->GetDesc(&stream_desc);
			}
			ID3D11Device* device = nullptr;
			context->GetDevice(&device);
			const bool ready = device && stream && ensure(device, stride_wanted, stream_desc.ByteWidth);
			if (device)
			{
				device->Release();
			}
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
			// BF4's indices are absolute vertex numbers and one batch's quads lie all over the buffer (2026-11-16): the whole
			// buffer is copied and every quad in it near the game's barrel moved (4 vertices a quad from its start).
			const UINT quads = stream_desc.ByteWidth / (4 * stride_wanted);
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
			if (force_test.load(std::memory_order_relaxed))
			{
				// Research (pipe "flash force"): every quad lifted 1 m, whole.
				const float lift[12] = { 1, 0, 0, 0, 0, 1, 0, 1, 0, 0, 1, 0 };
				std::memcpy(cb, lift, sizeof(lift));
				cb[15] = 100.0f;
				cb[19] = 1.0f;
				cb[20] = 100.0f;
				cb[21] = 200.0f;
				cb[22] = 100.0f;
				cb[23] = 200.0f;
			}
			auto* counts = reinterpret_cast<UINT*>(cb + 24);
			counts[0] = quads;
			counts[1] = stride_wanted;
			counts[2] = counts[3] = 0;
			context->Unmap(own.move, 0);
			const D3D11_BOX box{ 0, 0, 0, stream_desc.ByteWidth, 1, 1 };
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
			if (check_left.load(std::memory_order_relaxed) > 0)
			{
				// Research (pipe "flash check"): the nearest quad to the game's muzzle before the move and to the
				// drawn muzzle after it, from this batch's first 16 quads (a read-back stall).
				--check_left;
				{
					// The whole batch: its quad nearest the game's muzzle before, and after the move how near any quad
					// comes to the drawn muzzle and still to the game's.
					float full_drawn[3];
					muzzle_move::apply(m, m.muzzle, full_drawn);
					float before_full{}, after_drawn{}, after_game{};
					nearest_in_batch(context, stream, 0, stride_wanted, quads, m.muzzle, before_full);
					nearest_in_batch(context, own.vertices, 0, stride_wanted, quads, full_drawn, after_drawn);
					nearest_in_batch(context, own.vertices, 0, stride_wanted, quads, m.muzzle, after_game);
					const std::lock_guard guard(check_lock);
					check_full_before = std::min(check_full_before, before_full);
					check_full_after_drawn = std::min(check_full_after_drawn, after_drawn);
					check_full_after_game = std::min(check_full_after_game, after_game);
				}
				float before[16][3], after[16][3];
				const int read_before = read_quads(context, stream, offset, base, stride_wanted, quads, before);
				const int read_after = read_quads(context, own.vertices, offset, base, stride_wanted, quads, after);
				float drawn_muzzle[3];
				muzzle_move::apply(m, m.muzzle, drawn_muzzle);
				const auto nearest = [](const float (&points)[16][3], int n, const float (&to)[3]) {
					float best = 1e9f;
					for (int q = 0; q < n; ++q)
					{
						const float d[3] = { points[q][0] - to[0], points[q][1] - to[1], points[q][2] - to[2] };
						best = std::min(best, std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]));
					}
					return best;
				};
				// The CPU's weight for the quad nearest the game's muzzle (its first vertex standing for its centre).
				int nearest_quad = -1;
				float nearest_distance = 1e9f;
				for (int q = 0; q < read_before; ++q)
				{
					const float d[3] = { before[q][0] - m.muzzle[0], before[q][1] - m.muzzle[1], before[q][2] - m.muzzle[2] };
					const float distance = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
					if (distance < nearest_distance)
					{
						nearest_distance = distance;
						nearest_quad = q;
					}
				}
				const std::lock_guard guard(check_lock);
				if (nearest_quad >= 0)
				{
					check_weight = std::max(check_weight, muzzle_move::weight(m, before[nearest_quad], muzzle_move::shape{}, fade(ago)));
					const float moved_by[3] = { after[nearest_quad][0] - before[nearest_quad][0], after[nearest_quad][1] - before[nearest_quad][1],
						after[nearest_quad][2] - before[nearest_quad][2] };
					check_moved = std::max(check_moved, std::sqrt(moved_by[0] * moved_by[0] + moved_by[1] * moved_by[1] + moved_by[2] * moved_by[2]));
				}
				if (nearest_quad >= 0 && check_vertices[0][0] == 0.0f)
				{
					// The nearest quad's four vertices as the shader reads them (positions at each vertex's start).
					D3D11_BUFFER_DESC desc;
					stream->GetDesc(&desc);
					const UINT at = offset + static_cast<UINT>(base) * stride_wanted + static_cast<UINT>(nearest_quad) * 4 * stride_wanted;
					float quad_points[16][3]{};
					if (read_quads(context, stream, at, 0, stride_wanted / 4, 4, quad_points) == 4)
					{
						for (int v = 0; v < 4; ++v)
						{
							for (int axis = 0; axis < 3; ++axis)
							{
								check_vertices[v][axis] = quad_points[v][axis] - m.muzzle[axis];
							}
						}
					}
				}
				if (check_indices[0] < 0)
				{
					// The batch's first 12 indices (research: how its quads use the vertices).
					ID3D11Buffer* indices = nullptr;
					DXGI_FORMAT format{};
					UINT index_offset = 0;
					context->IAGetIndexBuffer(&indices, &format, &index_offset);
					if (indices)
					{
						const UINT size = format == DXGI_FORMAT_R32_UINT ? 4 : 2;
						float raw[16][3]{};
						// read_quads copies 12 bytes per "quad" of 4 "vertices": a 3-byte stride makes the pieces follow each other.
						if (read_quads(context, indices, index_offset + start * size, 0, 3, size, raw) == static_cast<int>(size))
						{
							const auto* bytes = reinterpret_cast<const std::uint8_t*>(raw);
							for (int i = 0; i < 12; ++i)
							{
								check_indices[i] = size == 4 ? static_cast<int>(reinterpret_cast<const std::uint32_t*>(bytes)[i])
									: static_cast<int>(reinterpret_cast<const std::uint16_t*>(bytes)[i]);
							}
						}
						indices->Release();
					}
				}
				check_before = std::min(check_before, nearest(before, read_before, m.muzzle));
				check_after = std::min(check_after, nearest(after, read_after, drawn_muzzle));
				check_after_game = std::min(check_after_game, nearest(after, read_after, m.muzzle));
				const float shift[3] = { drawn_muzzle[0] - m.muzzle[0], drawn_muzzle[1] - m.muzzle[1], drawn_muzzle[2] - m.muzzle[2] };
				check_shift = std::sqrt(shift[0] * shift[0] + shift[1] * shift[1] + shift[2] * shift[2]);
				++check_batches;
			}
			context->IASetVertexBuffers(0, 1, &own.vertices, &stride_wanted, &offset);
			original(context, count, start, base); // the copy has the stream's layout
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

	void note_instanced(ID3D11DeviceContext* context, UINT count, UINT instances, UINT first_instance)
	{
		if (GetTickCount64() >= survey_until.load(std::memory_order_relaxed) || !instances)
		{
			return;
		}
		ID3D11Buffer* streams[2]{};
		UINT strides[2]{}, offsets[2]{};
		context->IAGetVertexBuffers(0, 2, streams, strides, offsets);
		const int slot = streams[1] ? 1 : 0; // the instance stream, else the only one
		float nearest = 1e9f, nearest_along = 0.0f, nearest_camera = 1e9f;
		bool read = false;
		muzzle_move::move m;
		if (streams[slot] && strides[slot] >= 12 && survey_reads < 3000 && move_now(0, m))
		{
			// read_quads takes "quads" of 4 vertices: a quarter stride makes each read one instance.
			float points[16][3];
			const UINT stride = strides[slot];
			const int got = stride % 4 == 0 ? read_quads(context, streams[slot], offsets[slot] + first_instance * stride, 0, stride / 4, std::min<UINT>(instances, 16), points) : 0;
			read = got > 0;
			++survey_reads;
			distances(points, got, m, nearest, nearest_along, nearest_camera);
		}
		D3D11_BUFFER_DESC desc{};
		if (streams[slot])
		{
			streams[slot]->GetDesc(&desc);
		}
		const bool blend = blended(context);
		for (ID3D11Buffer* s : streams)
		{
			if (s)
			{
				s->Release();
			}
		}
		// Kind: bit 63 marks instanced; the instance stride, the index count, blending.
		const std::uint64_t key = (1ull << 63) | (static_cast<std::uint64_t>(strides[slot]) << 40) | (static_cast<std::uint64_t>(std::min<UINT>(count, 0xFFFFF)) << 1)
			| (blend ? 1u : 0u);
		const std::lock_guard guard(survey_lock);
		kind_stats& k = survey[key];
		++k.draws;
		k.blended += blend ? 1 : 0;
		k.read += read ? 1 : 0;
		k.largest_count = std::max(k.largest_count, instances);
		if (nearest < k.nearest)
		{
			k.nearest = nearest;
			k.nearest_along = nearest_along;
		}
		(void)desc;
	}

	namespace
	{
		void record(std::uint64_t key, bool blend, bool read, UINT largest, float nearest, float nearest_along, float nearest_camera = 1e9f)
		{
			const std::lock_guard guard(survey_lock);
			kind_stats& k = survey[key];
			++k.draws;
			k.blended += blend ? 1 : 0;
			k.read += read ? 1 : 0;
			k.largest_count = std::max(k.largest_count, largest);
			if (nearest < k.nearest)
			{
				k.nearest = nearest;
				k.nearest_along = nearest_along;
			}
			k.nearest_camera = std::min(k.nearest_camera, nearest_camera);
		}
	}

	void note_plain(ID3D11DeviceContext* context, UINT vertices, UINT instances, UINT start_vertex, UINT)
	{
		if (GetTickCount64() >= survey_until.load(std::memory_order_relaxed) || !vertices)
		{
			return;
		}
		ID3D11Buffer* stream = nullptr;
		UINT stride = 0, offset = 0;
		context->IAGetVertexBuffers(0, 1, &stream, &stride, &offset);
		float nearest = 1e9f, nearest_along = 0.0f, nearest_camera = 1e9f;
		bool read = false;
		muzzle_move::move m;
		if (stream && stride >= 12 && stride % 4 == 0 && survey_reads < 3000 && move_now(0, m))
		{
			float points[16][3];
			const int got = read_quads(context, stream, offset + start_vertex * stride, 0, stride / 4, std::min<UINT>(vertices, 16), points);
			read = got > 0;
			++survey_reads;
			distances(points, got, m, nearest, nearest_along, nearest_camera);
		}
		if (stream)
		{
			stream->Release();
		}
		const bool blend = blended(context);
		// Kind: top bits 01 = plain draw; the vertex stride, the vertex count, blending.
		const std::uint64_t key = (1ull << 62) | (static_cast<std::uint64_t>(stride) << 40) | (static_cast<std::uint64_t>(std::min<UINT>(vertices, 0xFFFFF)) << 1) | (blend ? 1u : 0u);
		record(key, blend, read, instances, nearest, nearest_along, nearest_camera);
	}

	void note_indirect(ID3D11DeviceContext* context, bool indexed)
	{
		if (GetTickCount64() >= survey_until.load(std::memory_order_relaxed))
		{
			return;
		}
		ID3D11Buffer* stream = nullptr;
		UINT stride = 0, offset = 0;
		context->IAGetVertexBuffers(0, 1, &stream, &stride, &offset);
		if (stream)
		{
			stream->Release();
		}
		const bool blend = blended(context);
		// Kind: top bits 11 = indirect; indexed or not, the vertex stride, blending.
		const std::uint64_t key = (3ull << 62) | (static_cast<std::uint64_t>(stride) << 40) | (static_cast<std::uint64_t>(indexed ? 1 : 0) << 1) | (blend ? 1u : 0u);
		record(key, blend, false, 0, 1e9f, 0.0f);
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
		if (argument == "force" || argument == "force off")
		{
			force_test = argument == "force";
			return std::string("flash force ") + (force_test.load() ? "on: every moved quad lifted 1 m" : "off");
		}
		if (sscanf_s(argument.c_str(), "check %u", &value) == 1)
		{
			const std::lock_guard guard(check_lock);
			check_before = check_after = check_after_game = 1e9f;
			check_shift = check_weight = check_moved = 0.0f;
			std::memset(check_vertices, 0, sizeof(check_vertices));
			check_full_before = check_full_after_drawn = check_full_after_game = 1e9f;
			check_indices[0] = -1;
			check_batches = 0;
			check_left = static_cast<int>(value);
			return "flash check: the next " + std::to_string(value) + " moved batches";
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
		body::muzzle_frame aim, game, drawn;
		if (body::muzzle_frames(aim, game, drawn))
		{
			std::snprintf(line, sizeof(line), "\naim=(%.3f %.3f %.3f) root_game=(%.3f %.3f %.3f) root_drawn=(%.3f %.3f %.3f)", aim.position[0], aim.position[1], aim.position[2],
				game.position[0], game.position[1], game.position[2], drawn.position[0], drawn.position[1], drawn.position[2]);
			out += line;
		}
		{
			const std::lock_guard guard(check_lock);
			if (check_batches)
			{
				std::snprintf(line, sizeof(line), "\ncheck batches=%d muzzles_apart_m=%.2f nearest_to_game_before_m=%.2f nearest_to_drawn_after_m=%.2f nearest_to_game_after_m=%.2f cpu_weight=%.2f moved_m=%.3f",
					check_batches, check_shift, check_before, check_after, check_after_game, check_weight, check_moved);
				out += line;
				std::snprintf(line, sizeof(line), "\n  whole batches: nearest to the game's muzzle before %.2f m; after: nearest to the drawn muzzle %.2f m, to the game's %.2f m",
					check_full_before, check_full_after_drawn, check_full_after_game);
				out += line;
				out += "\n  first indices:";
				for (int i = 0; i < 12; ++i)
				{
					out += " " + std::to_string(check_indices[i]);
				}
				for (int v = 0; v < 4; ++v)
				{
					std::snprintf(line, sizeof(line), "\n  nearest quad vertex %d from the game's muzzle: (%.3f %.3f %.3f)", v, check_vertices[v][0], check_vertices[v][1], check_vertices[v][2]);
					out += line;
				}
			}
		}
		const std::lock_guard guard(survey_lock);
		for (const auto& [key, k] : survey)
		{
			if ((key >> 62) == 3)
			{
				std::snprintf(line, sizeof(line), "\nindirect %s stride=%u blended=%llu draws=%llu", ((key >> 1) & 1) ? "indexed" : "plain", static_cast<UINT>((key >> 40) & 0x3FFFFF),
					static_cast<unsigned long long>(k.blended), static_cast<unsigned long long>(k.draws));
			}
			else if ((key >> 62) == 1)
			{
				std::snprintf(line, sizeof(line), "\nplain stride=%u vertices=%llu blended=%llu draws=%llu read=%llu most_instances=%u nearest_m=%.2f along_m=%.2f camera_m=%.2f",
					static_cast<UINT>((key >> 40) & 0x3FFFFF), static_cast<unsigned long long>((key >> 1) & 0xFFFFF), static_cast<unsigned long long>(k.blended),
					static_cast<unsigned long long>(k.draws), static_cast<unsigned long long>(k.read), k.largest_count, k.nearest, k.nearest_along, k.nearest_camera);
			}
			else if (key >> 63)
			{
				std::snprintf(line, sizeof(line), "\ninstanced instance_stride=%u index_count=%llu blended=%llu draws=%llu read=%llu most_instances=%u nearest_m=%.2f along_m=%.2f camera_m=%.2f",
					static_cast<UINT>((key >> 40) & 0x7FFFFF), static_cast<unsigned long long>((key >> 1) & 0xFFFFF), static_cast<unsigned long long>(k.blended),
					static_cast<unsigned long long>(k.draws), static_cast<unsigned long long>(k.read), k.largest_count, k.nearest, k.nearest_along, k.nearest_camera);
			}
			else
			{
				std::snprintf(line, sizeof(line), "\nkind stride=%u buffer_kb=%llu blended=%llu draws=%llu read=%llu largest_count=%u nearest_m=%.2f along_m=%.2f camera_m=%.2f",
					static_cast<UINT>(key >> 40), static_cast<unsigned long long>((key >> 1) & 0x7FFFFFFFFFull), static_cast<unsigned long long>(k.blended),
					static_cast<unsigned long long>(k.draws), static_cast<unsigned long long>(k.read), k.largest_count, k.nearest, k.nearest_along, k.nearest_camera);
			}
			out += line;
		}
		return out;
	}
}
