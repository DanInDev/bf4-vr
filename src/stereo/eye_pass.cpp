#include "stereo/eye_pass.hpp"

#include <windows.h>
#include <smmintrin.h>
#include <d3d11_1.h>
#include <wrl/client.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>
#include <memory>
#include <mutex>
#include <unordered_map>

#include <MinHook.h>

#include "common/log.hpp"
#include "safety/mp_guard.hpp"
#include "stereo/constant_patch.hpp"
#include "stereo/eye_math.hpp"
#include "stereo/scope.hpp"
#include "stereo/scope_compose.hpp"
#include "stereo/scope_math.hpp"
#include "stereo/scope_scissor.hpp"

using Microsoft::WRL::ComPtr;

namespace bfvr::stereo
{
	namespace
	{
		// void per_view_render(WorldRenderer*, ViewSlot*, SlotContext*), only caller the world
		// renderer frame 0x140CE56F0, once per slot (count +0x214, slot 0 at +0x280).
		constexpr std::uint64_t per_view_render_address = 0x140ce7ea0;
		// push rbp; push rsi; push rdi; push r12; push r13; push r14; push r15; lea rbp,[rsp-1A70h]; mov eax,1B80h
		constexpr std::uint8_t per_view_render_prologue[] = { 0x40, 0x55, 0x56, 0x57, 0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57,
			0x48, 0x8d, 0xac, 0x24, 0x90, 0xe5, 0xff, 0xff, 0xb8, 0x80, 0x1b, 0x00 };
		using per_view_render_t = void(__fastcall*)(void* renderer, std::uint8_t* slot, void* context);
		per_view_render_t original_per_view_render{};

		// RenderView::update(view, flags): rebuilds the matrices whose dirty bits (+0xE0) are set.
		constexpr std::uint64_t view_update_address = 0x140b8c180;
		// mov [rsp+8],rbx; push rdi; sub rsp,20h; mov eax,edx; mov edi,edx; mov rbx,rcx
		constexpr std::uint8_t view_update_prologue[] = { 0x48, 0x89, 0x5c, 0x24, 0x08, 0x57, 0x48, 0x83, 0xec, 0x20, 0x8b, 0xc2,
			0x8b, 0xfa, 0x48, 0x8b, 0xd9 };
		using view_update_t = void(__fastcall*)(void* view, unsigned flags);
		view_update_t view_update{};

		// View slot layout (filled by 0x140CF1D00 each frame): RenderViews at +0x630 and +0xB10,
		// sub-pass copies at +0x186A0 and +0x19060 (stride 0x4E0, count +0x33FB8).
		constexpr std::size_t slot_size = 0x34000;
		constexpr std::size_t slot_views[] = { 0x630, 0xb10 };
		constexpr std::size_t sub_views[] = { 0x186a0, 0x19060 };
		constexpr std::size_t sub_view_stride = 0x4e0;
		constexpr std::size_t sub_view_count = 0x33fb8;
		constexpr std::size_t transform = 0x40;
		constexpr std::size_t dirty_flags = 0xe0;
		constexpr std::size_t view_fov_y = 0xb4;  // radians
		constexpr std::size_t view_aspect = 0xc4;
		constexpr std::size_t view_fov_x = 0x250; // radians, kept in step by setFovY / setAspect

		std::atomic<int> mode{};
		std::atomic<float> shift{ 0.064f };
		std::atomic<std::uint64_t> frames{};
		std::atomic<std::uint64_t> pairs{};
		std::atomic<std::uint64_t> rejected{};
		std::atomic<std::uint64_t> abandoned{};
		std::atomic<float> extra_ms{};
		eye_math::eye_pair pair; // render thread only
		std::unique_ptr<std::uint8_t[]> snapshot;

		std::atomic<ID3D11Resource*> back_buffer{};
		std::mutex image_lock;
		ComPtr<ID3D11Texture2D> image;
		ComPtr<ID3D11Texture2D> image_second; // the second eye at the same point of the frame

		// Constant-buffer bindings with offsets (ID3D11DeviceContext1, d3d11_1.h order): every draw
		// binds 256-byte blocks of two 1 MB dynamic arenas that job threads filled earlier.
		// Plain bindings and draws, recorded by eyeprobe only (d3d11.h order).
		constexpr int vs_set_constant_buffers_slot = 7;
		constexpr int ps_set_constant_buffers_slot = 16;
		constexpr int draw_indexed_slot = 12;
		constexpr int draw_indexed_instanced_slot = 20;
		using set_constant_buffers_t = void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, UINT, UINT, ID3D11Buffer* const*);
		using draw_indexed_t = void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, UINT, UINT, INT);
		using draw_indexed_instanced_t = void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, UINT, UINT, UINT, INT, UINT);
		set_constant_buffers_t original_vs_set_constant_buffers{};
		set_constant_buffers_t original_ps_set_constant_buffers{};
		draw_indexed_t original_draw_indexed{};
		draw_indexed_instanced_t original_draw_indexed_instanced{};
		constexpr int vs_set_constant_buffers1_slot = 119;
		constexpr int ps_set_constant_buffers1_slot = 123;
		constexpr int cs_set_constant_buffers1_slot = 124;
		constexpr int map_slot = 14;
		using map_t = HRESULT(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, ID3D11Resource*, UINT, D3D11_MAP, UINT, D3D11_MAPPED_SUBRESOURCE*);
		map_t original_map{};
		using set_constant_buffers1_t = void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, UINT, UINT, ID3D11Buffer* const*, const UINT*, const UINT*);
		set_constant_buffers1_t original_vs_set_constant_buffers1{};
		set_constant_buffers1_t original_ps_set_constant_buffers1{};
		set_constant_buffers1_t original_cs_set_constant_buffers1{};

		// Eye-2 constants (flag 8). The arenas are dynamic constant buffers written through a
		// persistent NO_OVERWRITE mapping; their CPU address is taken from Map. While the second eye
		// renders, every binding of an arena block that holds the centre camera is redirected to a
		// copy in our own buffer with the eye's matrices (constant_patch).
		// Arenas are recreated on level loads: each tracked one is referenced (never freed under us) and the
		// one mapped longest ago is dropped when the table is full; only recently mapped ones are read back.
		struct arena
		{
			ComPtr<ID3D11Resource> buffer;
			const std::uint8_t* cpu{};
			UINT size{};
			ULONGLONG last_map{};
		};
		constexpr ULONGLONG arena_idle_ms = 1000;
		std::mutex arena_lock;
		arena arenas[8];
		int arena_count{};
		constexpr UINT min_arena_size = 0x10000;

		constexpr UINT eye_buffer_size = 4u << 20;
		ComPtr<ID3D11Buffer> eye_buffer;
		ComPtr<ID3D11DeviceContext> eye_context;
		UINT eye_offset{};
		bool eye_buffer_discarded{};
		bool patching{}; // render thread, second eye only
		constant_patch patch;
		// (arena, first constant) -> first constant in eye_buffer, or ~0u = nothing to patch.
		std::unordered_map<std::uint64_t, UINT> patched_blocks;
		std::atomic<std::uint64_t> bindings_redirected{};
		std::atomic<std::uint64_t> blocks_patched{};
		std::atomic<std::uint64_t> eye_buffer_overflows{};
		std::atomic<unsigned> patch_windows{};
		std::atomic<float> readback_ms{};

		// Reading the arenas through their mapping crosses the bus (they live in GPU memory: 36 ms per
		// frame, 2026-10-25). After the first eye, the GPU copies each arena into a staging buffer
		// and the CPU reads that instead; the Map waits for the first eye's GPU work.
		struct readback
		{
			ComPtr<ID3D11Resource> arena;
			ComPtr<ID3D11Buffer> staging;
			const std::uint8_t* cpu{};
			UINT size{};
		};
		readback readbacks[8];
		int readback_count{};

		// Before the first eye: the job-written blocks it binds are complete, the render thread's own
		// blocks of eye 1 are not (eye 2 never binds those, it writes its own).
		void begin_readback()
		{
			arena current[8];
			int count{};
			{
				const std::lock_guard guard(arena_lock);
				count = arena_count;
				std::copy(arenas, arenas + count, current);
			}
			ComPtr<ID3D11Device> device;
			eye_context->GetDevice(&device);
			const ULONGLONG now = GetTickCount64();
			for (int i = 0; i < count; ++i)
			{
				readback& r = readbacks[i];
				if (now - current[i].last_map > arena_idle_ms)
				{
					r.arena.Reset();
					continue;
				}
				if (r.arena.Get() != current[i].buffer.Get() || !r.staging)
				{
					D3D11_BUFFER_DESC desc{};
					desc.ByteWidth = current[i].size;
					desc.Usage = D3D11_USAGE_STAGING;
					desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
					r.staging.Reset();
					if (FAILED(device->CreateBuffer(&desc, nullptr, &r.staging)))
					{
						r.arena = nullptr;
						continue;
					}
					r.arena = current[i].buffer;
					r.size = current[i].size;
				}
				eye_context->CopyResource(r.staging.Get(), r.arena.Get());
			}
			readback_count = count;
		}

		// After the first eye: the copies were queued before its draws, so this waits only for them.
		void map_readbacks()
		{
			for (int i = 0; i < readback_count; ++i)
			{
				readback& r = readbacks[i];
				D3D11_MAPPED_SUBRESOURCE mapped{};
				r.cpu = r.arena && r.staging && SUCCEEDED(original_map(eye_context.Get(), r.staging.Get(), 0, D3D11_MAP_READ, 0, &mapped))
					? static_cast<const std::uint8_t*>(mapped.pData) : nullptr;
			}
		}

		void end_readback()
		{
			for (int i = 0; i < readback_count; ++i)
			{
				if (readbacks[i].cpu)
				{
					eye_context->Unmap(readbacks[i].staging.Get(), 0);
					readbacks[i].cpu = nullptr;
				}
			}
		}

		// Which bindings carry camera data: scanned/patched per (stage, slot, size), render thread only.
		struct category
		{
			char stage{};
			UINT slot{};
			UINT bytes{};
			std::uint64_t scanned{};
			std::uint64_t patched{};
		};
		category categories[48];
		int category_count{};

		void count_category(char stage, UINT slot, UINT bytes, bool patched)
		{
			for (int i = 0; i < category_count; ++i)
			{
				category& c = categories[i];
				if (c.stage == stage && c.slot == slot && c.bytes == bytes)
				{
					++c.scanned;
					c.patched += patched ? 1 : 0;
					return;
				}
			}
			if (category_count < 48)
			{
				categories[category_count++] = { stage, slot, bytes, 1, patched ? 1u : 0u };
			}
		}

		void note_map(ID3D11Resource* resource, void* data)
		{
			const std::lock_guard guard(arena_lock);
			for (int i = 0; i < arena_count; ++i)
			{
				if (arenas[i].buffer.Get() == resource)
				{
					arenas[i].cpu = static_cast<const std::uint8_t*>(data);
					arenas[i].last_map = GetTickCount64();
					return;
				}
			}
			D3D11_RESOURCE_DIMENSION dimension{};
			resource->GetType(&dimension);
			if (dimension != D3D11_RESOURCE_DIMENSION_BUFFER)
			{
				return;
			}
			D3D11_BUFFER_DESC desc{};
			static_cast<ID3D11Buffer*>(resource)->GetDesc(&desc);
			if ((desc.BindFlags & D3D11_BIND_CONSTANT_BUFFER) && desc.Usage == D3D11_USAGE_DYNAMIC && desc.ByteWidth >= min_arena_size
				&& resource != eye_buffer.Get())
			{
				int slot_index = arena_count;
				if (arena_count >= 8)
				{
					slot_index = 0;
					for (int i = 1; i < arena_count; ++i)
					{
						slot_index = arenas[i].last_map < arenas[slot_index].last_map ? i : slot_index;
					}
				}
				else
				{
					++arena_count;
				}
				arenas[slot_index] = { resource, static_cast<const std::uint8_t*>(data), desc.ByteWidth, GetTickCount64() };
				log::write("eye pass: constant arena %p, %u bytes", static_cast<void*>(resource), desc.ByteWidth);
			}
		}

		HRESULT STDMETHODCALLTYPE map_detour(ID3D11DeviceContext* c, ID3D11Resource* r, UINT s, D3D11_MAP type, UINT flags, D3D11_MAPPED_SUBRESOURCE* m)
		{
			const HRESULT hr = original_map(c, r, s, type, flags, m);
			if (SUCCEEDED(hr) && m && r && (type == D3D11_MAP_WRITE_NO_OVERWRITE || type == D3D11_MAP_WRITE_DISCARD))
			{
				note_map(r, m->pData);
			}
			return hr;
		}

		// Returns the first constant of the eye copy in eye_buffer, or ~0u to keep the original binding.
		std::uint64_t block_key(ID3D11Buffer* buffer, UINT first)
		{
			return (reinterpret_cast<std::uintptr_t>(buffer) << 20) ^ first;
		}

		// The eye's copy of an arena block, or false when it holds no centre-camera data.
		// Flag 32: every view-projection built with the game's projection moves with the eye, including
		// the first-person (gun and arms) one, which is not a RenderView field (constant_patch). Applied
		// before the table so the world view-projection is not moved twice.
		std::atomic<bool> view_projection_rule{};
		std::atomic<std::uint64_t> view_projections_shifted{};
		std::atomic<std::uint64_t> per_view_blocks_shifted{};
		std::atomic<float> foreground_y_scale{};
		float frame_foreground_y{}; // render thread: the largest per-view y scale of this second eye
		std::atomic<std::uint64_t> foreground_y_at{};
		std::atomic<float> eye_shift_metres{};
		float projection_sx{};
		float projection_sy{};
		// Research (pipe eyezoom): the second eye renders the same camera with this vertical FOV
		// (radians, 0 = off) instead of moving sideways: does the constant patch follow a zoom?
		std::atomic<float> zoom_test_fov{};
		bool zoom_active{}; // render thread, second eye of a zoom test
		// Magnified sights (stereo/scope): a third render from the sight. The gun and arms are drawn
		// with their own per-view block, which is blanked so they do not block the lens's view.
		bool scope_rendering{}; // render thread
		float scope_y_scale{};  // cot(render FOV / 2) of the scope render
		std::atomic<std::uint64_t> scope_renders{};
		std::atomic<std::uint64_t> scope_hidden_blocks{};
		std::atomic<float> scope_ms{};

		float block_shift()
		{
			return zoom_active || scope_rendering ? 0.0f : eye_shift_metres.load(std::memory_order_relaxed);
		}

		// A per-view block (layout checked by shift_per_view_block) of neither world camera (the first
		// eye's, being replaced, or the scope's): another projection (the gun pass at 45 / 17 deg, the
		// first-person shadow view) or rows scaled by the gun size factor.
		bool foreground_block(const float* block)
		{
			const float* view = block + 0x30 / 4;
			const float right = std::sqrt(view[0] * view[0] + view[1] * view[1] + view[2] * view[2]);
			const float sy = block[0x70 / 4 + 5];
			const auto same_scale = [sy](float expected) { return expected > 0.0f && std::fabs(sy - expected) <= 0.01f * expected; };
			return std::fabs(right - 1.0f) > 1e-3f || !(same_scale(scope_y_scale) || same_scale(projection_sy));
		}

		// GPU time of each render (timestamp queries, read a few frames later without waiting):
		// 0 = before the first eye, 1 = after it, 2 = after the second eye, 3 = after the scope.
		struct gpu_frame
		{
			ComPtr<ID3D11Query> disjoint;
			ComPtr<ID3D11Query> stamps[4];
			bool issued{};
			bool scope{};
		};
		constexpr int gpu_frames = 4;
		gpu_frame gpu_ring[gpu_frames];
		int gpu_index{};
		std::atomic<float> gpu_eye_ms[3]{}; // first eye, second eye, scope
		ID3D11DeviceContext* gpu_context{};

		void gpu_stamp(int which)
		{
			gpu_frame& f = gpu_ring[gpu_index];
			if (gpu_context && f.stamps[which])
			{
				gpu_context->End(f.stamps[which].Get());
			}
		}

		void gpu_begin(ID3D11Device* device, ID3D11DeviceContext* context)
		{
			gpu_context = context;
			gpu_index = (gpu_index + 1) % gpu_frames;
			gpu_frame& f = gpu_ring[gpu_index];
			if (f.issued)
			{
				D3D11_QUERY_DATA_TIMESTAMP_DISJOINT disjoint{};
				UINT64 t[4]{};
				bool ready = context->GetData(f.disjoint.Get(), &disjoint, sizeof(disjoint), D3D11_ASYNC_GETDATA_DONOTFLUSH) == S_OK;
				for (int i = 0; ready && i < (f.scope ? 4 : 3); ++i)
				{
					ready = context->GetData(f.stamps[i].Get(), &t[i], sizeof(t[i]), D3D11_ASYNC_GETDATA_DONOTFLUSH) == S_OK;
				}
				if (ready && !disjoint.Disjoint && disjoint.Frequency)
				{
					const auto ms = [&](int a, int b) { return static_cast<float>(1000.0 * static_cast<double>(t[b] - t[a]) / static_cast<double>(disjoint.Frequency)); };
					gpu_eye_ms[0] = ms(0, 1);
					gpu_eye_ms[1] = ms(1, 2);
					gpu_eye_ms[2] = f.scope ? ms(2, 3) : 0.0f;
				}
				f.issued = false;
			}
			if (!f.disjoint)
			{
				D3D11_QUERY_DESC desc{ D3D11_QUERY_TIMESTAMP_DISJOINT, 0 };
				device->CreateQuery(&desc, &f.disjoint);
				desc.Query = D3D11_QUERY_TIMESTAMP;
				for (auto& stamp : f.stamps)
				{
					device->CreateQuery(&desc, &stamp);
				}
			}
			if (f.disjoint)
			{
				context->Begin(f.disjoint.Get());
				f.issued = true;
				f.scope = false;
			}
		}

		void gpu_end()
		{
			gpu_frame& f = gpu_ring[gpu_index];
			if (gpu_context && f.issued)
			{
				gpu_context->End(f.disjoint.Get());
			}
		}

		std::atomic<bool> scope_scissor_on{ true }; // pipe scopescissor: A/B the scissor
		std::atomic<std::uint64_t> scope_prepare_us{}; // worker: patching the scope's blocks
		std::atomic<std::uint64_t> scope_wait_us{};    // render thread: waiting for the worker after eye 2
		std::atomic<std::uint64_t> scope_render_us{};
		std::atomic<std::uint64_t> scope_compose_us{};

		// The scope camera's copy of a block (render thread for late blocks, the worker for the first
		// eye's): the gun's per-view blocks blanked, the world's camera values replaced from table.
		int patch_scope_block(const constant_patch& table, float* block, UINT count)
		{
			// Before the table: the gun's block can hold a value of the world camera's too (its position),
			// which would otherwise count as a change and leave the gun drawn.
			if (count == 32 && constant_patch::shift_per_view_block(block, count * 4, 0.0f) && foreground_block(block))
			{
				// Projection, view-projection and rotation-only view-projection zero: every vertex at w = 0.
				std::memset(block + 0x70 / 4, 0, 16 * sizeof(float));
				std::memset(block + 0xb0 / 4, 0, 32 * sizeof(float));
				++scope_hidden_blocks;
				return 1;
			}
			return table.apply(block, count * 4);
		}

		int patch_block(float* block, UINT count)
		{
			if (scope_rendering)
			{
				return patch_scope_block(patch, block, count);
			}
			int changed = 0;
			if (view_projection_rule.load(std::memory_order_relaxed) && projection_sx > 0.0f && projection_sy > 0.0f)
			{
				const int shifted = constant_patch::shift_view_projections(block, count * 4, projection_sx, projection_sy,
					block_shift());
				view_projections_shifted += static_cast<std::uint64_t>(shifted);
				changed += shifted;
			}
			changed += patch.apply(block, count * 4);
			// Per-view blocks of other cameras (the first-person camera the gun and arms are drawn
			// with) hold no RenderView value; they are recognised by their layout instead.
			if (changed == 0 && count == 32 && constant_patch::shift_per_view_block(block, count * 4, block_shift()))
			{
				++per_view_blocks_shifted;
				changed = 1;
				// The gun pass's own projection: its y scale is cot(fov/2), 45 degrees at the hip and
				// narrower while the game aims down the sights (camera_hook keeps the gun life size).
				// Other cameras share the layout (the first-person shadow view at 45, the world): the
				// narrowest of the frame is the gun's, published after the eye (frame_foreground_y).
				frame_foreground_y = std::fmax(frame_foreground_y, block[0x70 / 4 + 5]);
			}
			return changed;
		}

		// The block as the first eye saw it, from the read-back arenas (read only: the worker uses it too).
		bool read_back(ID3D11Buffer* buffer, UINT first, UINT count, float* block)
		{
			const std::uint8_t* cpu{};
			UINT size{};
			for (int i = 0; i < readback_count; ++i)
			{
				if (readbacks[i].arena.Get() == buffer)
				{
					cpu = readbacks[i].cpu;
					size = readbacks[i].size;
				}
			}
			const UINT bytes = count * 16;
			if (!cpu || first * 16 + bytes > size)
			{
				return false;
			}
			std::memcpy(block, cpu + first * 16, bytes);
			return true;
		}

		bool patched_copy(ID3D11Buffer* buffer, UINT first, UINT count, float* block)
		{
			return read_back(buffer, first, count, block) && patch_block(block, count) > 0;
		}

		// Blocks the first eye bound: the second eye replays the same draw lists, so they are patched
		// and uploaded in one go before it renders.
		struct used_block
		{
			ID3D11Buffer* buffer{};
			UINT first{};
			UINT count{};
		};
		std::vector<used_block> first_eye_blocks;
		bool collecting{};
		std::vector<std::uint8_t> upload;
		std::atomic<std::uint64_t> late_blocks{};
		std::atomic<std::uint64_t> late_patched{};

		void prepare_eye_blocks()
		{
			upload.resize(eye_buffer_size);
			UINT used = 0;
			for (const used_block& b : first_eye_blocks)
			{
				if (!b.buffer || b.count == 0 || b.count > 4096)
				{
					continue;
				}
				const std::uint64_t key = block_key(b.buffer, b.first);
				if (patched_blocks.count(key))
				{
					continue;
				}
				const UINT aligned = (b.count * 16 + 255) & ~255u;
				UINT result = ~0u;
				if (used + aligned > eye_buffer_size)
				{
					++eye_buffer_overflows;
				}
				else if (patched_copy(b.buffer, b.first, b.count, reinterpret_cast<float*>(upload.data() + used)))
				{
					result = used / 16;
					used += aligned;
					++blocks_patched;
				}
				patched_blocks.emplace(key, result);
			}
			D3D11_MAPPED_SUBRESOURCE mapped{};
			if (used && SUCCEEDED(original_map(eye_context.Get(), eye_buffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
			{
				std::memcpy(mapped.pData, upload.data(), used);
				eye_context->Unmap(eye_buffer.Get(), 0);
				eye_buffer_discarded = true;
			}
			eye_offset = used;
		}

		// The arenas live in GPU memory mapped over the bus: streaming loads (SSE4.1) read whole lines.
		bool copy_guarded(void* target, const void* source, std::size_t size)
		{
			__try
			{
				const auto* from = static_cast<const __m128i*>(source);
				auto* to = static_cast<__m128i*>(target);
				for (std::size_t i = 0; i < size / 16; ++i)
				{
					to[i] = _mm_stream_load_si128(const_cast<__m128i*>(from + i));
				}
				return true;
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				return false;
			}
		}

		UINT eye_block(ID3D11Buffer* buffer, UINT first, UINT count)
		{
			if (!buffer || count == 0 || count > 4096)
			{
				return ~0u;
			}
			const std::uint64_t key = block_key(buffer, first);
			if (const auto found = patched_blocks.find(key); found != patched_blocks.end())
			{
				return found->second;
			}
			// Not bound by the first eye: the render thread wrote it during this render, still with the
			// centre camera (monitor check of the image, 2026-10-25). Only the arena's own mapping has
			// it, so it is read there (slow memory, but few blocks) and uploaded on its own.
			++late_blocks;
			const std::uint8_t* cpu{};
			UINT size{};
			{
				const std::lock_guard guard(arena_lock);
				for (int i = 0; i < arena_count; ++i)
				{
					if (arenas[i].buffer.Get() == buffer)
					{
						cpu = arenas[i].cpu;
						size = arenas[i].size;
					}
				}
			}
			const UINT bytes = count * 16;
			const UINT aligned = (bytes + 255) & ~255u;
			UINT result = ~0u;
			alignas(16) static float block[4096 * 4];
			if (cpu && first * 16 + bytes <= size && copy_guarded(block, cpu + first * 16, bytes)
				&& patch_block(block, count) > 0)
			{
				D3D11_MAPPED_SUBRESOURCE mapped{};
				const D3D11_MAP type = eye_buffer_discarded ? D3D11_MAP_WRITE_NO_OVERWRITE : D3D11_MAP_WRITE_DISCARD;
				if (eye_offset + aligned > eye_buffer_size)
				{
					++eye_buffer_overflows;
				}
				else if (SUCCEEDED(original_map(eye_context.Get(), eye_buffer.Get(), 0, type, 0, &mapped)))
				{
					eye_buffer_discarded = true;
					std::memcpy(static_cast<std::uint8_t*>(mapped.pData) + eye_offset, block, bytes);
					eye_context->Unmap(eye_buffer.Get(), 0);
					result = eye_offset / 16;
					eye_offset += aligned;
					++late_patched;
				}
			}
			patched_blocks.emplace(key, result);
			return result;
		}

		// Rewrites one binding call for the second eye; false when nothing changed.
		// eyeprobe: bindings of one first-eye render, then the arenas read back from the GPU.
		struct binding
		{
			char stage{};
			UINT slot{};
			ID3D11Buffer* buffer{};
			UINT first{};
			UINT count{};
		};
		std::vector<binding> probe_bindings;
		std::atomic<bool> probing{};
		std::atomic<bool> probe_requested{};
		std::atomic<bool> probe_second_requested{}; // eyeprobe2: record the second eye instead
		std::string probe_prefix;
		std::string probe_result;
		HANDLE probe_done{};

		// Research (pipe eyepoison): one binding category gets an all-zero block in the second eye, to see
		// which constants draw what.
		std::atomic<char> poison_stage{};
		std::atomic<UINT> poison_slot{};
		std::atomic<UINT> poison_bytes{};
		ComPtr<ID3D11Buffer> poison_buffer;

		bool redirect(char stage, UINT start, UINT count, ID3D11Buffer* const* buffers, const UINT* first, const UINT* number,
			ID3D11Buffer** out_buffers, UINT* out_first, UINT* out_number)
		{
			if (!patching || !buffers || !first || !number || count > D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT)
			{
				return false;
			}
			bool changed = false;
			for (UINT i = 0; i < count; ++i)
			{
				out_buffers[i] = buffers[i];
				out_first[i] = first[i];
				out_number[i] = number[i];
				if (poison_buffer && stage == poison_stage.load(std::memory_order_relaxed) && start + i == poison_slot.load(std::memory_order_relaxed)
					&& (poison_bytes.load(std::memory_order_relaxed) == 0 || number[i] * 16 == poison_bytes.load(std::memory_order_relaxed)))
				{
					out_buffers[i] = poison_buffer.Get();
					out_first[i] = 0;
					changed = true;
					continue;
				}
				const UINT moved = eye_block(buffers[i], first[i], number[i]);
				count_category(stage, start + i, number[i] * 16, moved != ~0u);
				if (moved != ~0u)
				{
					if (probing.load(std::memory_order_relaxed) && probe_bindings.size() < 20000)
					{
						probe_bindings.push_back({ 'R', start + i, eye_buffer.Get(), moved, number[i] });
					}
					out_buffers[i] = eye_buffer.Get();
					out_first[i] = moved;
					changed = true;
					++bindings_redirected;
				}
			}
			return changed;
		}

		template <set_constant_buffers1_t* original, char stage>
		void set_redirected(ID3D11DeviceContext* c, UINT s, UINT n, ID3D11Buffer* const* b, const UINT* f, const UINT* k)
		{
			ID3D11Buffer* buffers[D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT];
			UINT first[D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT];
			UINT number[D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT];
			if (redirect(stage, s, n, b, f, k, buffers, first, number))
			{
				(*original)(c, s, n, buffers, first, number);
				return;
			}
			(*original)(c, s, n, b, f, k);
		}
		bool binding_hooks_tried{};


		void record(char stage, UINT start, UINT count, ID3D11Buffer* const* buffers, const UINT* first, const UINT* number)
		{
			if (collecting && buffers && first && number)
			{
				for (UINT i = 0; i < count; ++i)
				{
					first_eye_blocks.push_back({ buffers[i], first[i], number[i] });
				}
			}
			if (!probing.load(std::memory_order_relaxed))
			{
				return;
			}
			for (UINT i = 0; i < count && probe_bindings.size() < 20000; ++i)
			{
				probe_bindings.push_back({ stage, start + i, buffers ? buffers[i] : nullptr, first ? first[i] : 0, number ? number[i] : 0 });
			}
		}

		void STDMETHODCALLTYPE vs_set_constant_buffers_detour(ID3D11DeviceContext* c, UINT s, UINT n, ID3D11Buffer* const* b)
		{
			record('v', s, n, b, nullptr, nullptr);
			original_vs_set_constant_buffers(c, s, n, b);
		}

		void STDMETHODCALLTYPE ps_set_constant_buffers_detour(ID3D11DeviceContext* c, UINT s, UINT n, ID3D11Buffer* const* b)
		{
			record('p', s, n, b, nullptr, nullptr);
			original_ps_set_constant_buffers(c, s, n, b);
		}

		// Research (pipe eyeskip): draws [skip_from, skip_to) of the second eye are not issued.
		std::atomic<UINT> skip_from{};
		std::atomic<UINT> skip_to{};
		UINT eye2_draws{};

		bool skip_draw()
		{
			if (!patching)
			{
				return false;
			}
			const UINT index = eye2_draws++;
			return index >= skip_from.load(std::memory_order_relaxed) && index < skip_to.load(std::memory_order_relaxed);
		}

		// A draw marker in the probe (stage 'D', slot = index count) ties bindings to draws.
		void STDMETHODCALLTYPE draw_indexed_detour(ID3D11DeviceContext* c, UINT count, UINT start, INT base)
		{
			if (skip_draw())
			{
				return;
			}
			if (probing.load(std::memory_order_relaxed) && probe_bindings.size() < 20000)
			{
				probe_bindings.push_back({ 'D', count, nullptr, 0, 0 });
			}
			original_draw_indexed(c, count, start, base);
		}

		void STDMETHODCALLTYPE draw_indexed_instanced_detour(ID3D11DeviceContext* c, UINT count, UINT instances, UINT start, INT base, UINT first)
		{
			if (skip_draw())
			{
				return;
			}
			if (probing.load(std::memory_order_relaxed) && probe_bindings.size() < 20000)
			{
				probe_bindings.push_back({ 'D', count, nullptr, 0, instances });
			}
			original_draw_indexed_instanced(c, count, instances, start, base, first);
		}

		void STDMETHODCALLTYPE vs_set_constant_buffers1_detour(ID3D11DeviceContext* c, UINT s, UINT n, ID3D11Buffer* const* b, const UINT* f, const UINT* k)
		{
			record('V', s, n, b, f, k);
			set_redirected<&original_vs_set_constant_buffers1, 'V'>(c, s, n, b, f, k);
		}

		void STDMETHODCALLTYPE ps_set_constant_buffers1_detour(ID3D11DeviceContext* c, UINT s, UINT n, ID3D11Buffer* const* b, const UINT* f, const UINT* k)
		{
			record('P', s, n, b, f, k);
			set_redirected<&original_ps_set_constant_buffers1, 'P'>(c, s, n, b, f, k);
		}

		void STDMETHODCALLTYPE cs_set_constant_buffers1_detour(ID3D11DeviceContext* c, UINT s, UINT n, ID3D11Buffer* const* b, const UINT* f, const UINT* k)
		{
			record('C', s, n, b, f, k);
			set_redirected<&original_cs_set_constant_buffers1, 'C'>(c, s, n, b, f, k);
		}

		// Render thread: the immediate context is only known once a frame has been presented.
		void hook_bindings(ID3D11Device* device)
		{
			binding_hooks_tried = true;
			ComPtr<ID3D11DeviceContext> context;
			device->GetImmediateContext(&context);
			ComPtr<ID3D11DeviceContext1> context1;
			if (FAILED(context.As(&context1)))
			{
				log::write("eye pass: no ID3D11DeviceContext1; binding hooks not installed");
				return;
			}
			void** const vtable = *reinterpret_cast<void***>(context1.Get());
			const struct { int slot; void* detour; void** original; } table[] = {
				{ vs_set_constant_buffers1_slot, &vs_set_constant_buffers1_detour, reinterpret_cast<void**>(&original_vs_set_constant_buffers1) },
				{ ps_set_constant_buffers1_slot, &ps_set_constant_buffers1_detour, reinterpret_cast<void**>(&original_ps_set_constant_buffers1) },
				{ cs_set_constant_buffers1_slot, &cs_set_constant_buffers1_detour, reinterpret_cast<void**>(&original_cs_set_constant_buffers1) },
				{ map_slot, &map_detour, reinterpret_cast<void**>(&original_map) },
				{ vs_set_constant_buffers_slot, &vs_set_constant_buffers_detour, reinterpret_cast<void**>(&original_vs_set_constant_buffers) },
				{ ps_set_constant_buffers_slot, &ps_set_constant_buffers_detour, reinterpret_cast<void**>(&original_ps_set_constant_buffers) },
				{ draw_indexed_slot, &draw_indexed_detour, reinterpret_cast<void**>(&original_draw_indexed) },
				{ draw_indexed_instanced_slot, &draw_indexed_instanced_detour, reinterpret_cast<void**>(&original_draw_indexed_instanced) },
			};
			for (const auto& entry : table)
			{
				MH_STATUS status = MH_CreateHook(vtable[entry.slot], entry.detour, entry.original);
				if (status == MH_OK)
				{
					status = MH_EnableHook(vtable[entry.slot]);
				}
				log::write("eye pass: binding hook slot %d: %s", entry.slot, MH_StatusToString(status));
			}
			D3D11_BUFFER_DESC desc{};
			desc.ByteWidth = eye_buffer_size;
			desc.Usage = D3D11_USAGE_DYNAMIC;
			desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
			desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
			if (FAILED(device->CreateBuffer(&desc, nullptr, &eye_buffer)))
			{
				log::write("eye pass: eye constant buffer creation failed");
			}
			eye_context = context;
			std::vector<std::uint8_t> zeros(65536);
			D3D11_BUFFER_DESC poison_desc{};
			poison_desc.ByteWidth = 65536;
			poison_desc.Usage = D3D11_USAGE_IMMUTABLE;
			poison_desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
			D3D11_SUBRESOURCE_DATA poison_data{ zeros.data() };
			device->CreateBuffer(&poison_desc, &poison_data, &poison_buffer);
		}

		std::string write_file(const std::string& path, const void* data, std::size_t size)
		{
			std::ofstream file(path, std::ios::binary);
			file.write(static_cast<const char*>(data), static_cast<std::streamsize>(size));
			return file ? std::string() : "cannot write " + path;
		}

		// Render thread, after the first eye: bindings as text, each bound buffer read back, the slot.
		std::string write_probe(ID3D11Device* device, const std::uint8_t* slot)
		{
			ComPtr<ID3D11DeviceContext> context;
			device->GetImmediateContext(&context);
			std::ofstream text(probe_prefix + "-bindings.txt");
			std::vector<ID3D11Buffer*> buffers;
			for (const binding& b : probe_bindings)
			{
				char line[96];
				std::snprintf(line, sizeof(line), "%c %u %p 0x%x %u\n", b.stage, b.slot, static_cast<void*>(b.buffer), b.first * 16, b.count * 16);
				text << line;
				if (b.buffer && std::find(buffers.begin(), buffers.end(), b.buffer) == buffers.end())
				{
					buffers.push_back(b.buffer);
				}
			}
			std::string error;
			for (ID3D11Buffer* buffer : buffers)
			{
				D3D11_BUFFER_DESC desc{};
				buffer->GetDesc(&desc);
				desc.Usage = D3D11_USAGE_STAGING;
				desc.BindFlags = 0;
				desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
				desc.MiscFlags = 0;
				desc.StructureByteStride = 0;
				ComPtr<ID3D11Buffer> staging;
				D3D11_MAPPED_SUBRESOURCE mapped{};
				if (FAILED(device->CreateBuffer(&desc, nullptr, &staging)))
				{
					error = "staging buffer creation failed";
					continue;
				}
				context->CopyResource(staging.Get(), buffer);
				if (FAILED(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped)))
				{
					error = "map failed";
					continue;
				}
				char name[64];
				std::snprintf(name, sizeof(name), "-buffer-%p.bin", static_cast<void*>(buffer));
				const std::string written = write_file(probe_prefix + name, mapped.pData, desc.ByteWidth);
				context->Unmap(staging.Get(), 0);
				if (!written.empty())
				{
					error = written;
				}
			}
			if (const std::string written = write_file(probe_prefix + "-slot.bin", slot, slot_size); !written.empty())
			{
				error = written;
			}
			return error.empty() ? "ok bindings=" + std::to_string(probe_bindings.size()) + " buffers=" + std::to_string(buffers.size()) : "error: " + error;
		}

		void shift_view(std::uint8_t* view, float metres)
		{
			const auto* right = reinterpret_cast<const float*>(view + transform);
			auto* position = reinterpret_cast<float*>(view + transform + 0x30);
			for (int axis = 0; axis < 3; ++axis)
			{
				position[axis] += right[axis] * metres;
			}
			*reinterpret_cast<std::uint32_t*>(view + dirty_flags) |= 0x7;
			view_update(view, 0xf);
		}

		void shift_slot(std::uint8_t* slot, float metres)
		{
			for (const std::size_t offset : slot_views)
			{
				shift_view(slot + offset, metres);
			}
			const std::uint32_t count = *reinterpret_cast<const std::uint32_t*>(slot + sub_view_count);
			for (std::uint32_t i = 0; i < count && i < 4; ++i)
			{
				for (const std::size_t offset : sub_views)
				{
					shift_view(slot + offset + i * sub_view_stride, metres);
				}
			}
		}

		// Zoom test: the slot's two RenderViews get vertical FOV fov (radians); fov 0 restores saved.
		void zoom_slot(std::uint8_t* slot, float fov, float (&saved)[2][2])
		{
			for (int v = 0; v < 2; ++v)
			{
				std::uint8_t* const view = slot + slot_views[v];
				auto* const fov_y = reinterpret_cast<float*>(view + view_fov_y);
				auto* const fov_x = reinterpret_cast<float*>(view + view_fov_x);
				if (fov > 0.0f)
				{
					saved[v][0] = *fov_y;
					saved[v][1] = *fov_x;
					const float aspect = *reinterpret_cast<const float*>(view + view_aspect);
					*fov_y = fov;
					*fov_x = 2.0f * std::atan(std::tan(0.5f * fov) * aspect);
				}
				else
				{
					*fov_y = saved[v][0];
					*fov_x = saved[v][1];
				}
				*reinterpret_cast<std::uint32_t*>(view + dirty_flags) |= 0xf;
				view_update(view, 0xf);
			}
		}

		// Render thread, after the first eye's tonemap into the back buffer.
		void copy_eye(ComPtr<ID3D11Texture2D>& target)
		{
			ID3D11Resource* const source = back_buffer.load();
			if (!source)
			{
				return;
			}
			ComPtr<ID3D11Texture2D> texture;
			if (FAILED(source->QueryInterface(IID_PPV_ARGS(&texture))))
			{
				return;
			}
			ComPtr<ID3D11Device> device;
			texture->GetDevice(&device);
			D3D11_TEXTURE2D_DESC desc{};
			texture->GetDesc(&desc);
			const std::lock_guard guard(image_lock);
			D3D11_TEXTURE2D_DESC existing{};
			if (target)
			{
				target->GetDesc(&existing);
			}
			if (!target || existing.Width != desc.Width || existing.Height != desc.Height || existing.Format != desc.Format)
			{
				desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
				desc.MiscFlags = 0;
				desc.CPUAccessFlags = 0;
				desc.Usage = D3D11_USAGE_DEFAULT;
				target.Reset();
				if (FAILED(device->CreateTexture2D(&desc, nullptr, &target)))
				{
					return;
				}
			}
			ComPtr<ID3D11DeviceContext> context;
			device->GetImmediateContext(&context);
			context->CopyResource(target.Get(), source);
		}

		// The scope's constant patching runs on a worker while the second eye renders: the first eye's
		// blocks are read back and mapped by then, and the scope camera is built on copies of the
		// RenderViews, so the render thread only uploads the result afterwards.
		alignas(16) std::uint8_t scope_views[2][0x4e0];
		constant_patch scope_patch;
		std::vector<std::uint8_t> scope_upload;
		std::vector<std::pair<std::uint64_t, UINT>> scope_results; // block key -> first constant, ~0u = unchanged
		UINT scope_used{};
		HANDLE scope_start{};
		HANDLE scope_done{};
		bool scope_job_running{}; // render thread

		double microseconds(const LARGE_INTEGER& a, const LARGE_INTEGER& b)
		{
			LARGE_INTEGER frequency{};
			QueryPerformanceFrequency(&frequency);
			return 1e6 * static_cast<double>(b.QuadPart - a.QuadPart) / static_cast<double>(frequency.QuadPart);
		}

		void scope_job()
		{
			LARGE_INTEGER start{}, end{};
			QueryPerformanceCounter(&start);
			scope_upload.resize(eye_buffer_size);
			scope_results.clear();
			std::unordered_map<std::uint64_t, UINT> seen;
			UINT used = 0;
			for (const used_block& b : first_eye_blocks)
			{
				if (!b.buffer || b.count == 0 || b.count > 4096)
				{
					continue;
				}
				const std::uint64_t key = block_key(b.buffer, b.first);
				if (!seen.emplace(key, 0u).second)
				{
					continue;
				}
				const UINT aligned = (b.count * 16 + 255) & ~255u;
				UINT result = ~0u;
				float* const block = reinterpret_cast<float*>(scope_upload.data() + used);
				if (used + aligned > eye_buffer_size)
				{
					++eye_buffer_overflows;
				}
				else if (read_back(b.buffer, b.first, b.count, block) && patch_scope_block(scope_patch, block, b.count) > 0)
				{
					result = used / 16;
					used += aligned;
				}
				scope_results.emplace_back(key, result);
			}
			scope_used = used;
			QueryPerformanceCounter(&end);
			scope_prepare_us = static_cast<std::uint64_t>(microseconds(start, end));
		}

		DWORD WINAPI scope_worker(void*)
		{
			for (;;)
			{
				WaitForSingleObject(scope_start, INFINITE);
				scope_job();
				SetEvent(scope_done);
			}
		}

		// Before the second eye: the scope camera on copies of the first eye's RenderViews (centre), its
		// patch table, and the worker started. False when the scope cannot run this frame.
		bool start_scope_job(const scope_request& request, const float (&centre)[2][0x4e0 / 4])
		{
			if (!(request.render_fov_y > 0.0f && request.render_fov_y < 3.0f))
			{
				return false;
			}
			if (!scope_start)
			{
				scope_start = CreateEventW(nullptr, FALSE, FALSE, nullptr);
				scope_done = CreateEventW(nullptr, FALSE, FALSE, nullptr);
				HANDLE thread = scope_start && scope_done ? CreateThread(nullptr, 0, scope_worker, nullptr, 0, nullptr) : nullptr;
				if (!thread)
				{
					log::write("eye pass: scope worker not started");
					return false;
				}
				CloseHandle(thread);
			}
			for (int v = 0; v < 2; ++v)
			{
				std::uint8_t* const view = scope_views[v];
				std::memcpy(view, centre[v], sizeof(scope_views[v]));
				float rows[16];
				std::memcpy(rows, view + transform, sizeof(rows));
				float moved[16];
				scope_math::compose_rows(rows, request.relative, moved);
				std::memcpy(view + transform, moved, sizeof(moved));
				const float aspect = *reinterpret_cast<const float*>(view + view_aspect);
				*reinterpret_cast<float*>(view + view_fov_y) = request.render_fov_y;
				*reinterpret_cast<float*>(view + view_fov_x) = 2.0f * std::atan(std::tan(0.5f * request.render_fov_y) * aspect);
				*reinterpret_cast<std::uint32_t*>(view + dirty_flags) |= 0xf;
				view_update(view, 0xf);
			}
			scope_y_scale = 1.0f / std::tan(0.5f * request.render_fov_y);
			scope_patch.clear();
			for (int v = 0; v < 2; ++v)
			{
				scope_patch.add_view(centre[v], reinterpret_cast<const float*>(scope_views[v]), 0x4e0 / 4);
			}
			scope_job_running = true;
			SetEvent(scope_start);
			return true;
		}

		// The worker must be done before the read-back arenas are unmapped.
		void finish_scope_job()
		{
			if (scope_job_running)
			{
				WaitForSingleObject(scope_done, INFINITE);
				scope_job_running = false;
			}
		}

		// After the second eye: the slot's RenderViews take the scope camera, the worker's blocks are
		// uploaded, the world is rendered once more, the lens is cut out of the back buffer, and the back
		// buffer and slot are put back.
		void render_scope(void* renderer, std::uint8_t* slot, void* context, const scope_request& request)
		{
			LARGE_INTEGER start{}, waited{}, rendered{}, composed_at{};
			QueryPerformanceCounter(&start);
			finish_scope_job();
			QueryPerformanceCounter(&waited);
			ID3D11Resource* const buffer = back_buffer.load();
			ComPtr<ID3D11Texture2D> texture;
			if (!buffer || FAILED(buffer->QueryInterface(IID_PPV_ARGS(&texture))))
			{
				return;
			}
			alignas(16) static std::uint8_t saved[2][0x4e0];
			for (int v = 0; v < 2; ++v)
			{
				std::memcpy(saved[v], slot + slot_views[v], sizeof(saved[v]));
				std::memcpy(slot + slot_views[v], scope_views[v], sizeof(scope_views[v]));
			}
			const float aspect = *reinterpret_cast<const float*>(slot + slot_views[0] + view_aspect);
			// Blocks the render thread writes during the scope render are patched with the scope's table.
			std::swap(patch, scope_patch);
			patched_blocks.clear();
			for (const auto& [key, result] : scope_results)
			{
				patched_blocks.emplace(key, result);
			}
			eye_offset = 0;
			eye_buffer_discarded = false;
			D3D11_MAPPED_SUBRESOURCE mapped{};
			if (scope_used && SUCCEEDED(original_map(eye_context.Get(), eye_buffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
			{
				std::memcpy(mapped.pData, scope_upload.data(), scope_used);
				eye_context->Unmap(eye_buffer.Get(), 0);
				eye_buffer_discarded = true;
				eye_offset = scope_used;
			}
			// Only the lens circle is used: pixel work outside it (with a margin for blurs) is scissored away.
			const bool scissor = scope_scissor_on.load(std::memory_order_relaxed) && eye_context;
			if (scissor)
			{
				install_scope_scissor(eye_context.Get());
				D3D11_TEXTURE2D_DESC desc{};
				texture->GetDesc(&desc);
				constexpr float margin = 1.3f;
				begin_scope_scissor(eye_context.Get(), desc.Width, desc.Height, 0.5f * request.crop * margin / aspect, 0.5f * request.crop * margin);
			}
			scope_rendering = true;
			patching = true;
			original_per_view_render(renderer, slot, context);
			patching = false;
			scope_rendering = false;
			if (scissor)
			{
				end_scope_scissor(eye_context.Get());
			}
			QueryPerformanceCounter(&rendered);

			ComPtr<ID3D11Device> device;
			texture->GetDevice(&device);
			ComPtr<ID3D11DeviceContext> immediate;
			device->GetImmediateContext(&immediate);
			compose_scope(immediate.Get(), texture.Get(), aspect, request);
			{
				// The monitor and the UI pass keep the second eye's image.
				const std::lock_guard guard(image_lock);
				if (image_second)
				{
					immediate->CopyResource(texture.Get(), image_second.Get());
				}
			}
			for (int v = 0; v < 2; ++v)
			{
				std::memcpy(slot + slot_views[v], saved[v], sizeof(saved[v]));
			}
			++scope_renders;
			QueryPerformanceCounter(&composed_at);
			scope_wait_us = static_cast<std::uint64_t>(microseconds(start, waited));
			scope_render_us = static_cast<std::uint64_t>(microseconds(waited, rendered));
			scope_compose_us = static_cast<std::uint64_t>(microseconds(rendered, composed_at));
			scope_ms = static_cast<float>(microseconds(start, composed_at) / 1000.0);
		}

		void __fastcall per_view_render_detour(void* renderer, std::uint8_t* slot, void* context)
		{
			const int flags = mode.load(std::memory_order_relaxed);
			if (!(flags & 1) || !slot || safety::mod_disabled() || !safety::campaign_level_loaded())
			{
				original_per_view_render(renderer, slot, context);
				return;
			}
			if (!binding_hooks_tried)
			{
				if (ID3D11Resource* const buffer = back_buffer.load())
				{
					ComPtr<ID3D11Device> device;
					buffer->GetDevice(&device);
					hook_bindings(device.Get());
				}
			}
			const std::uint64_t frame = ++frames;
			const std::uint64_t abandoned_before = pair.abandoned();
			pair.begin(frame);
			abandoned += pair.abandoned() - abandoned_before;

			const bool patch_constants = (flags & 8) != 0 && eye_buffer && original_map;
			const bool shifted_last = (flags & 2) != 0 || patch_constants;
			const bool restore = (flags & 4) != 0;
			const float metres = shift.load(std::memory_order_relaxed);
			if (restore)
			{
				if (!snapshot)
				{
					snapshot = std::make_unique<std::uint8_t[]>(slot_size);
				}
				std::memcpy(snapshot.get(), slot, slot_size);
			}

			// First eye: the shifted one unless it should end up on screen.
			if (!shifted_last)
			{
				shift_slot(slot, metres);
			}
			const bool probe = probe_requested.exchange(false);
			if (probe)
			{
				probe_bindings.clear();
				probing = true;
			}
			if (patch_constants)
			{
				first_eye_blocks.clear();
				collecting = true;
				begin_readback();
				ComPtr<ID3D11Device> device;
				eye_context->GetDevice(&device);
				gpu_begin(device.Get(), eye_context.Get());
				gpu_stamp(0);
			}
			original_per_view_render(renderer, slot, context);
			if (patch_constants)
			{
				gpu_stamp(1);
			}
			collecting = false;
			probing = false;
			pair.rendered(0, frame);
			copy_eye(image);
			if (probe)
			{
				ID3D11Resource* const buffer = back_buffer.load();
				ComPtr<ID3D11Device> device;
				if (buffer)
				{
					buffer->GetDevice(&device);
				}
				probe_result = device ? write_probe(device.Get(), slot) : "error: no device yet";
				SetEvent(probe_done);
			}

			if (restore)
			{
				std::memcpy(slot, snapshot.get(), slot_size);
			}
			else if (!shifted_last)
			{
				shift_slot(slot, -metres);
			}
			// Eye-2 constants: every RenderView window that changes with the move (constant_patch).
			alignas(16) static float centre_views[2][0x4e0 / 4];
			if (patch_constants)
			{
				for (int v = 0; v < 2; ++v)
				{
					std::memcpy(centre_views[v], slot + slot_views[v], sizeof(centre_views[v]));
				}
			}
			const float zoom_fov = patch_constants ? zoom_test_fov.load(std::memory_order_relaxed) : 0.0f;
			zoom_active = zoom_fov > 0.0f;
			scope_request scope;
			bool scope_this_frame = false;
			float zoom_saved[2][2]{};
			if (zoom_active)
			{
				zoom_slot(slot, zoom_fov, zoom_saved);
			}
			else if (shifted_last)
			{
				shift_slot(slot, metres);
			}
			if (patch_constants)
			{
				patch.clear();
				for (int v = 0; v < 2; ++v)
				{
					patch.add_view(centre_views[v], reinterpret_cast<const float*>(slot + slot_views[v]), 0x4e0 / 4);
				}
				patch_windows = static_cast<unsigned>(patch.matrices() + patch.positions());
				projection_sx = centre_views[0][0x320 / 4];
				projection_sy = centre_views[0][0x320 / 4 + 5];
				patched_blocks.clear();
				frame_foreground_y = 0.0f;
				eye_offset = 0;
				eye_buffer_discarded = false;
				eye2_draws = 0;
				LARGE_INTEGER read_start{}, read_end{}, read_frequency{};
				QueryPerformanceCounter(&read_start);
				map_readbacks();
				if (!zoom_active)
				{
					scope = latest_scope_request();
					scope_this_frame = scope.active && GetTickCount64() - scope.at < 250 && start_scope_job(scope, centre_views);
				}
				prepare_eye_blocks();
				QueryPerformanceCounter(&read_end);
				QueryPerformanceFrequency(&read_frequency);
				readback_ms = static_cast<float>(1000.0 * static_cast<double>(read_end.QuadPart - read_start.QuadPart) / static_cast<double>(read_frequency.QuadPart));
				patching = true;
			}

			const bool probe_second = probe_second_requested.exchange(false);
			if (probe_second)
			{
				probe_bindings.clear();
				probing = true;
			}
			LARGE_INTEGER start{}, end{}, frequency{};
			QueryPerformanceCounter(&start);
			original_per_view_render(renderer, slot, context);
			patching = false;
			if (frame_foreground_y > 0.0f)
			{
				foreground_y_scale = frame_foreground_y;
				foreground_y_at = GetTickCount64();
				frame_foreground_y = 0.0f;
			}
			probing = false;
			copy_eye(image_second);
			if (probe_second)
			{
				ID3D11Resource* const buffer = back_buffer.load();
				ComPtr<ID3D11Device> device;
				if (buffer)
				{
					buffer->GetDevice(&device);
				}
				probe_result = device ? write_probe(device.Get(), slot) : "error: no device yet";
				SetEvent(probe_done);
			}
			QueryPerformanceCounter(&end);
			QueryPerformanceFrequency(&frequency);
			extra_ms = static_cast<float>(1000.0 * static_cast<double>(end.QuadPart - start.QuadPart) / static_cast<double>(frequency.QuadPart));
			if (patch_constants)
			{
				gpu_stamp(2);
			}
			if (scope_this_frame)
			{
				render_scope(renderer, slot, context, scope);
				gpu_stamp(3);
				gpu_ring[gpu_index].scope = true;
			}
			if (patch_constants)
			{
				gpu_end();
				finish_scope_job();
				end_readback();
				if (zoom_active)
				{
					zoom_slot(slot, 0.0f, zoom_saved);
				}
				else
				{
					shift_slot(slot, -metres);
				}
			}
			zoom_active = false;

			const std::uint64_t rejected_before = pair.rejected();
			pair.rendered(1, frame);
			rejected += pair.rejected() - rejected_before;
			if (pair.complete())
			{
				++pairs;
			}
		}

		bool matches(std::uint64_t address, const std::uint8_t* expected, std::size_t size)
		{
			std::uint8_t bytes[32]{};
			return size <= sizeof(bytes)
				&& ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<LPCVOID>(static_cast<std::uintptr_t>(address)), bytes, size, nullptr)
				&& std::memcmp(bytes, expected, size) == 0;
		}

		std::string install()
		{
#if defined(_M_X64)
			static bool installed = false;
			if (installed)
			{
				return {};
			}
			if (!matches(per_view_render_address, per_view_render_prologue, sizeof(per_view_render_prologue))
				|| !matches(view_update_address, view_update_prologue, sizeof(view_update_prologue)))
			{
				return "error: per-view render or view update does not match build 179547";
			}
			view_update = reinterpret_cast<view_update_t>(static_cast<std::uintptr_t>(view_update_address));
			auto* const target = reinterpret_cast<void*>(static_cast<std::uintptr_t>(per_view_render_address));
			MH_STATUS status = MH_CreateHook(target, reinterpret_cast<void*>(&per_view_render_detour), reinterpret_cast<void**>(&original_per_view_render));
			if (status == MH_OK)
			{
				status = MH_EnableHook(target);
			}
			log::write("eye pass: hook on %p: %s", target, MH_StatusToString(status));
			if (status != MH_OK)
			{
				return std::string("error: ") + MH_StatusToString(status);
			}
			installed = true;
			return {};
#else
			return "error: x64 only";
#endif
		}
	}

	std::string set_eye_pass(int new_mode, float shift_m)
	{
		if (const std::string error = install(); !error.empty())
		{
			return error;
		}
		if (new_mode >= 0)
		{
			shift = shift_m;
			mode = new_mode & 47;
			view_projection_rule = (new_mode & 32) != 0;
			eye_shift_metres = shift_m;
		}
		std::string text = "eyepass mode=" + std::to_string(mode.load()) + " shift=" + std::to_string(shift.load()) + " " + eye_pass_report();
		// Racy read of render-thread counters: research output only.
		for (int i = 0; i < category_count; ++i)
		{
			const category& c = categories[i];
			char line[96];
			std::snprintf(line, sizeof(line), "\n  %c slot %u %u bytes: scanned %llu patched %llu", c.stage, c.slot, c.bytes, c.scanned, c.patched);
			text += line;
		}
		return text;
	}

	std::string probe_eye_pass(const std::string& prefix, bool second_eye)
	{
		if (prefix.empty())
		{
			return "error: usage: eyeprobe <path prefix>";
		}
		if (!(mode.load() & 1))
		{
			return "error: eyepass is off";
		}
		probe_prefix = prefix;
		probe_result.clear();
		probe_done = CreateEventW(nullptr, TRUE, FALSE, nullptr);
		(second_eye ? probe_second_requested : probe_requested) = true;
		const DWORD waited = WaitForSingleObject(probe_done, 5000);
		probe_requested = false;
		probe_second_requested = false;
		CloseHandle(probe_done);
		probe_done = nullptr;
		return waited == WAIT_OBJECT_0 ? probe_result : "error: no eye-pass frame within 5 s";
	}

	void set_eye_pass_shift(float metres)
	{
		if (std::isfinite(metres) && metres > 0.0f && metres < 0.2f)
		{
			shift = metres;
			eye_shift_metres = metres;
		}
	}

	std::string set_scope_scissor(int on)
	{
		if (on == 0 || on == 1)
		{
			scope_scissor_on = on == 1;
		}
		return std::string("scopescissor ") + (scope_scissor_on.load() ? "1 " : "0 ") + scope_scissor_report();
	}

	std::string set_eye_zoom(float degrees)
	{
		const bool on = std::isfinite(degrees) && degrees >= 1.0f && degrees <= 120.0f;
		zoom_test_fov = on ? degrees * 3.14159265f / 180.0f : 0.0f;
		return on ? "eyezoom " + std::to_string(degrees) + " deg (second eye; eyeshot2 to compare)" : "eyezoom off";
	}

	std::uint64_t eye_pass_pairs()
	{
		return pairs.load();
	}

	std::string set_eye_skip(UINT from, UINT to)
	{
		skip_to = 0;
		skip_from = from;
		skip_to = to;
		return "eyeskip " + std::to_string(from) + ".." + std::to_string(to) + " (last eye-2 draw count " + std::to_string(eye2_draws) + ")";
	}

	std::string set_eye_poison(const std::string& spec)
	{
		char stage{};
		UINT slot{}, bytes{};
		if (spec == "off" || sscanf_s(spec.c_str(), "%c %u %u", &stage, 1u, &slot, &bytes) < 2)
		{
			poison_stage = 0;
			return "eyepoison off";
		}
		poison_slot = slot;
		poison_bytes = bytes;
		poison_stage = stage;
		return std::string("eyepoison ") + stage + " slot " + std::to_string(slot) + " bytes " + std::to_string(bytes);
	}

	float foreground_half_fov(float fallback)
	{
		const float y = foreground_y_scale.load(std::memory_order_relaxed);
		const std::uint64_t at = foreground_y_at.load(std::memory_order_relaxed);
		const std::uint64_t now = GetTickCount64();
		if ((now > at && now - at > 500) || !(y > 1.0f && y < 30.0f))
		{
			return fallback;
		}
		return std::atan(1.0f / y);
	}

	std::string eye_pass_report()
	{
		char text[768];
		std::snprintf(text, sizeof(text), "eye_frames=%llu eye_pairs=%llu eye_rejected=%llu eye_abandoned=%llu eye_ms=%.2f eye_readback_ms=%.2f eye_windows=%u "
			"eye_redirected=%llu eye_blocks=%llu eye_overflows=%llu eye_late=%llu eye_late_patched=%llu eye_vp_shifted=%llu eye_view_blocks=%llu "
			"gpu_ms eye1=%.2f eye2=%.2f scope=%.2f scope_renders=%llu scope_hidden=%llu scope_ms=%.2f (worker %llu us, wait %llu us, render %llu us, compose %llu us)",
			frames.load(), pairs.load(), rejected.load(), abandoned.load(), extra_ms.load(), readback_ms.load(), patch_windows.load(), bindings_redirected.load(),
			blocks_patched.load(), eye_buffer_overflows.load(), late_blocks.load(), late_patched.load(), view_projections_shifted.load(), per_view_blocks_shifted.load(),
			gpu_eye_ms[0].load(), gpu_eye_ms[1].load(), gpu_eye_ms[2].load(), scope_renders.load(), scope_hidden_blocks.load(), scope_ms.load(), scope_prepare_us.load(), scope_wait_us.load(), scope_render_us.load(), scope_compose_us.load());
		return text;
	}

	void eye_pass_on_present(IDXGISwapChain* swap_chain)
	{
		ComPtr<ID3D11Resource> buffer;
		if (SUCCEEDED(swap_chain->GetBuffer(0, IID_PPV_ARGS(&buffer))))
		{
			// The swap chain keeps the buffer alive; the raw pointer is only compared and copied from.
			back_buffer = buffer.Get();
		}
	}

	ID3D11Texture2D* eye_pass_image(int eye)
	{
		const std::lock_guard guard(image_lock);
		return eye == 0 ? image.Get() : image_second.Get();
	}
}
