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
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>
#include <memory>
#include <mutex>
#include <unordered_map>

#include <d3dcompiler.h>
#include <MinHook.h>

#include "common/log.hpp"
#include "safety/mp_guard.hpp"
#include "body/arm_pose.hpp"
#include "stereo/constant_patch.hpp"
#include "stereo/eye_math.hpp"
#include "stereo/extra_mesh.hpp"
#include "stereo/muzzle_quads.hpp"
#include "stereo/scope.hpp"
#include "stereo/scope_compose.hpp"
#include "stereo/scope_math.hpp"
#include "stereo/scope_scissor.hpp"
#include "stereo/foveation.hpp"
#include "stereo/lens_math.hpp"

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
		constexpr int draw_slot = 13;
		constexpr int draw_instanced_slot = 21;
		using draw_t = void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, UINT, UINT);
		using draw_instanced_t = void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, UINT, UINT, UINT, UINT);
		draw_t original_draw{};
		draw_instanced_t original_draw_instanced{};
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
			unsigned eye_discards{}; // maps with DISCARD since the current eye render began (#39 stray draws)
			unsigned generation{};   // every map with DISCARD (never reset): which version of the arena a binding saw (#162)
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

		// #162/#164: the read-back is copied before the first eye, so a block the first eye binds after its arena was
		// discarded mid-render (about two discards per first-eye render) is not in it: the second eye patched stale data
		// at that offset and drew that piece with another camera (the stray gun in the second eye). Such blocks are
		// copied on the GPU at bind time, in command order, into fresh_staging, and patched from there (pipe eyefresh).
		constexpr UINT fresh_size = 4u << 20;
		ComPtr<ID3D11Buffer> fresh_staging;
		std::unordered_map<std::uint64_t, UINT> fresh_seen; // (block, discards, size) -> offset, this render
		const std::uint8_t* fresh_cpu{};
		UINT fresh_used{};
		bool eye1_discarded{}; // render thread: an arena was discarded in this first-eye render
		std::atomic<bool> fresh_fix{ true };
		std::atomic<std::uint64_t> fresh_copies{};
		std::atomic<std::uint64_t> fresh_overflows{};

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
			D3D11_MAPPED_SUBRESOURCE mapped{};
			fresh_cpu = fresh_used && fresh_staging && SUCCEEDED(original_map(eye_context.Get(), fresh_staging.Get(), 0, D3D11_MAP_READ, 0, &mapped))
				? static_cast<const std::uint8_t*>(mapped.pData) : nullptr;
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
			if (fresh_cpu)
			{
				eye_context->Unmap(fresh_staging.Get(), 0);
				fresh_cpu = nullptr;
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

		// #39: second-eye bindings of an arena block after this render discarded the arena, which a copy made
		// before the discard would have answered with another block (the stray gun pieces); discard_fix keys
		// them apart (pipe eyefix).
		std::atomic<std::uint64_t> stale_after_discard{};
		std::atomic<bool> discard_fix{ true };
		// Research (#39, stray gun pieces in the second eye): arena maps with DISCARD inside each eye render.
		std::atomic<std::uint64_t> arena_discards_eye1{};
		std::atomic<std::uint64_t> arena_discards_eye2{};
		bool in_eye_render{}; // render thread: inside one of the eye renders

		bool note_map(ID3D11Resource* resource, void* data, bool discard)
		{
			const std::lock_guard guard(arena_lock);
			for (int i = 0; i < arena_count; ++i)
			{
				if (arenas[i].buffer.Get() == resource)
				{
					arenas[i].cpu = static_cast<const std::uint8_t*>(data);
					arenas[i].last_map = GetTickCount64();
					arenas[i].eye_discards += discard && in_eye_render ? 1u : 0u;
					arenas[i].generation += discard ? 1u : 0u;
					return true;
				}
			}
			D3D11_RESOURCE_DIMENSION dimension{};
			resource->GetType(&dimension);
			if (dimension != D3D11_RESOURCE_DIMENSION_BUFFER)
			{
				return false;
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
				return true;
			}
			return false;
		}

		// The start of an eye render: no arena discarded in it yet.
		void reset_eye_discards()
		{
			const std::lock_guard guard(arena_lock);
			for (int i = 0; i < arena_count; ++i)
			{
				arenas[i].eye_discards = 0;
			}
		}

		HRESULT STDMETHODCALLTYPE map_detour(ID3D11DeviceContext* c, ID3D11Resource* r, UINT s, D3D11_MAP type, UINT flags, D3D11_MAPPED_SUBRESOURCE* m)
		{
			const HRESULT hr = original_map(c, r, s, type, flags, m);
			if (SUCCEEDED(hr) && m && r && (type == D3D11_MAP_WRITE_NO_OVERWRITE || type == D3D11_MAP_WRITE_DISCARD))
			{
				const bool arena = note_map(r, m->pData, type == D3D11_MAP_WRITE_DISCARD);
				if (arena && type == D3D11_MAP_WRITE_DISCARD && in_eye_render)
				{
					++(patching ? arena_discards_eye2 : arena_discards_eye1);
					eye1_discarded = eye1_discarded || !patching;
				}
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
		std::mutex projection_lock;
		float world_projection_copy[16]{}; // the world view's projection (RenderView +0x320), for stereo/aim_depth
		bool world_projection_valid{};
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
			// #39: with life size through the gun pass's own FOV its rows are unit and, at gun_scale 1, its
			// projection is the world's; its camera is still the first-person one, in soldier space (the eye
			// ~1.6 m above the feet), where the world cameras sit at the level's coordinates.
			const float* camera = block + 0x140 / 4;
			const bool soldier_space = camera[0] * camera[0] + camera[1] * camera[1] + camera[2] * camera[2] < 4.0f * 4.0f;
			return std::fabs(right - 1.0f) > 1e-3f || !(same_scale(scope_y_scale) || same_scale(projection_sy)) || soldier_space;
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
			UINT fresh{ ~0u }; // byte offset in fresh_staging: bound after its arena's discard in this render
			unsigned generation{}; // the arena's generation when bound
		};

		// #162: a block is the arena's offset in one version of it (its generation); a discard makes every offset
		// another block. Keys made this way never let a block of one version answer a binding of another.
		std::uint64_t generation_key(ID3D11Buffer* buffer, UINT first, unsigned generation)
		{
			const std::uint64_t plain = block_key(buffer, first);
			return discard_fix.load(std::memory_order_relaxed) ? plain ^ (static_cast<std::uint64_t>(generation & 0xfff) << 52) : plain;
		}

		// The arena's discards in this eye render and its generation (0, 0 for a buffer that is no arena).
		void arena_state(ID3D11Buffer* buffer, unsigned& discards, unsigned& generation)
		{
			discards = generation = 0;
			const std::lock_guard guard(arena_lock);
			for (int a = 0; a < arena_count; ++a)
			{
				if (arenas[a].buffer.Get() == buffer)
				{
					discards = arenas[a].eye_discards;
					generation = arenas[a].generation;
				}
			}
		}
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
				const std::uint64_t key = generation_key(b.buffer, b.first, b.generation);
				const bool fresh = b.fresh != ~0u && fresh_cpu;
				if (patched_blocks.count(key))
				{
					continue;
				}
				const UINT aligned = (b.count * 16 + 255) & ~255u;
				UINT result = ~0u;
				float* const block = reinterpret_cast<float*>(upload.data() + used);
				if (used + aligned > eye_buffer_size)
				{
					++eye_buffer_overflows;
				}
				else if (fresh)
				{
					std::memcpy(block, fresh_cpu + b.fresh, b.count * 16);
					if (patch_block(block, b.count) > 0)
					{
						result = used / 16;
						used += aligned;
						++blocks_patched;
					}
				}
				else if (patched_copy(b.buffer, b.first, b.count, block))
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

		// An arena block through its CPU mapping (slow memory: a few blocks per frame only), for extra_mesh.
		bool read_arena_block(ID3D11Buffer* buffer, UINT first, UINT count, float* out)
		{
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
			const std::size_t bytes = static_cast<std::size_t>(count) * 16;
			return cpu && static_cast<std::size_t>(first) * 16 + bytes <= size && copy_guarded(out, cpu + first * 16, bytes);
		}

		UINT eye_block(ID3D11Buffer* buffer, UINT first, UINT count)
		{
			if (!buffer || count == 0 || count > 4096)
			{
				return ~0u;
			}
			// #39: after this render discarded the arena, an offset holds a block written since then, not the one the
			// first eye bound there: the copies made before (prepared from the first eye, or cached earlier in this
			// render) must not answer it. The discards are part of the key (none: the prepared keys).
			unsigned discards = 0, generation = 0;
			arena_state(buffer, discards, generation);
			const std::uint64_t plain_key = block_key(buffer, first);
			if (discards && patched_blocks.count(plain_key))
			{
				++stale_after_discard;
			}
			const std::uint64_t key = generation_key(buffer, first, generation);
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
					used_block b{ buffers[i], first[i], number[i] };
					unsigned discards = 0;
					arena_state(b.buffer, discards, b.generation);
					if (eye1_discarded && b.buffer && b.count && b.count <= 4096 && fresh_staging && fresh_fix.load(std::memory_order_relaxed))
					{
						// The same block after the same discard is bound many times (once per draw): copied once.
						const std::uint64_t key = block_key(b.buffer, b.first) ^ (static_cast<std::uint64_t>(discards & 0xff) << 52)
							^ (static_cast<std::uint64_t>(b.count) << 40);
						if (const auto seen = fresh_seen.find(key); discards && seen != fresh_seen.end())
						{
							b.fresh = seen->second;
						}
						else if (discards)
						{
							const UINT bytes = b.count * 16;
							if (fresh_used + bytes > fresh_size)
							{
								++fresh_overflows;
							}
							else
							{
								const D3D11_BOX box{ b.first * 16, 0, 0, b.first * 16 + bytes, 1, 1 };
								eye_context->CopySubresourceRegion(fresh_staging.Get(), 0, fresh_used, 0, 0, b.buffer, 0, &box);
								b.fresh = fresh_used;
								fresh_seen.emplace(key, fresh_used);
								fresh_used += bytes;
								++fresh_copies;
							}
						}
					}
					first_eye_blocks.push_back(b);
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
			extra_mesh::on_vs_bindings(s, n, b, nullptr, nullptr);
			original_vs_set_constant_buffers(c, s, n, b);
		}

		void STDMETHODCALLTYPE ps_set_constant_buffers_detour(ID3D11DeviceContext* c, UINT s, UINT n, ID3D11Buffer* const* b)
		{
			record('p', s, n, b, nullptr, nullptr);
			extra_mesh::on_ps_bindings(s, n, b, nullptr, nullptr);
			original_ps_set_constant_buffers(c, s, n, b);
		}

		// Research (pipe eyeskip): draws [skip_from, skip_to) of the second eye are not issued.
		std::atomic<UINT> skip_from{};
		std::atomic<UINT> skip_to{};
		UINT eye2_draws{};

		// Research (pipe eyehide): draws with these pixel shaders are not issued in either eye; "eyehide
		// plain" skips every non-indexed draw, "eyehide list" reports the non-indexed draws of the last
		// first-eye render (pixel shader, vertex count, draws).
		constexpr int hide_shader_count = 64;
		std::atomic<std::uintptr_t> hide_shaders[hide_shader_count]{};
		std::atomic<bool> hide_plain{};
		struct plain_draw
		{
			std::uintptr_t shader{};
			UINT vertices{};
			UINT draws{};
		};
		std::mutex plain_lock;
		std::vector<plain_draw> plain_draws;      // the render being recorded
		std::vector<plain_draw> plain_draws_last; // the last complete first-eye render

		std::atomic<bool> hide_null{}; // eyehide null: draws without a pixel shader (depth/stencil only)

		bool shader_hidden(ID3D11DeviceContext* c)
		{
			const bool by_null = hide_null.load(std::memory_order_relaxed);
			if (!in_eye_render || (!hide_shaders[0].load(std::memory_order_relaxed) && !by_null))
			{
				return false;
			}
			ComPtr<ID3D11PixelShader> shader;
			c->PSGetShader(&shader, nullptr, nullptr);
			const auto bound = reinterpret_cast<std::uintptr_t>(shader.Get());
			if (by_null && !bound)
			{
				return true;
			}
			for (const std::atomic<std::uintptr_t>& hidden : hide_shaders)
			{
				const std::uintptr_t value = hidden.load(std::memory_order_relaxed);
				if (!value)
				{
					break;
				}
				if (bound && value == bound)
				{
					return true;
				}
			}
			return false;
		}

		bool skip_plain_draw(ID3D11DeviceContext* c, UINT vertices)
		{
			if (!in_eye_render)
			{
				return false;
			}
			if (!patching)
			{
				ComPtr<ID3D11PixelShader> shader;
				c->PSGetShader(&shader, nullptr, nullptr);
				const auto bound = reinterpret_cast<std::uintptr_t>(shader.Get());
				const std::lock_guard guard(plain_lock);
				auto found = std::find_if(plain_draws.begin(), plain_draws.end(), [&](const plain_draw& d) { return d.shader == bound && d.vertices == vertices; });
				if (found == plain_draws.end() && plain_draws.size() < 256)
				{
					plain_draws.push_back({ bound, vertices, 1 });
				}
				else if (found != plain_draws.end())
				{
					++found->draws;
				}
			}
			return hide_plain.load(std::memory_order_relaxed) || shader_hidden(c);
		}

		void STDMETHODCALLTYPE draw_detour(ID3D11DeviceContext* c, UINT vertices, UINT start)
		{
			if (skip_plain_draw(c, vertices))
			{
				return;
			}
			muzzle_quads::note_plain(c, vertices, 1, start, 0);
			original_draw(c, vertices, start);
		}

		// #65 research: the indirect draws (GPU-driven particles?), counted by the flash survey.
		constexpr int draw_indexed_instanced_indirect_slot = 39;
		constexpr int draw_instanced_indirect_slot = 40;
		using draw_indirect_t = void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, ID3D11Buffer*, UINT);
		draw_indirect_t original_draw_indexed_instanced_indirect{};
		draw_indirect_t original_draw_instanced_indirect{};

		void STDMETHODCALLTYPE draw_indexed_instanced_indirect_detour(ID3D11DeviceContext* c, ID3D11Buffer* args, UINT offset)
		{
			muzzle_quads::note_indirect(c, true);
			original_draw_indexed_instanced_indirect(c, args, offset);
		}

		void STDMETHODCALLTYPE draw_instanced_indirect_detour(ID3D11DeviceContext* c, ID3D11Buffer* args, UINT offset)
		{
			muzzle_quads::note_indirect(c, false);
			original_draw_instanced_indirect(c, args, offset);
		}

		void STDMETHODCALLTYPE draw_instanced_detour(ID3D11DeviceContext* c, UINT vertices, UINT instances, UINT start, UINT first)
		{
			if (skip_plain_draw(c, vertices))
			{
				return;
			}
			muzzle_quads::note_plain(c, vertices, instances, start, first);
			original_draw_instanced(c, vertices, instances, start, first);
		}

		bool skip_draw(ID3D11DeviceContext* c)
		{
			if (shader_hidden(c))
			{
				if (patching)
				{
					++eye2_draws;
				}
				return true;
			}
			if (!patching)
			{
				return false;
			}
			const UINT index = eye2_draws++;
			return index >= skip_from.load(std::memory_order_relaxed) && index < skip_to.load(std::memory_order_relaxed);
		}

		// sight_reticle (#39): with the zoom mesh shown the game draws an optic's reticle as one quad at the lens
		// (6 indices, stencil reference 2, the frame's last draw), and its pixel shader places the picture by
		// the pixel's position times the inverse screen size of the per-view block: at the screen's centre.
		// So the reticle follows the head. Drawn with a copy of that block whose inverse size puts the anchor
		// on the sight line, it stays on the sight, the same in both eyes (re/notes/weapon.md).
		constexpr UINT reticle_indices = 6;
		constexpr UINT reticle_stencil = 2;
		std::atomic<bool> reticle_valid{};
		std::atomic<float> reticle_tan[2]{};     // the sight line in the rendered eye's view space: x/-z, y/-z
		std::atomic<int> reticle_forced{};       // research: 1 = reticle_forced_tan instead
		std::atomic<float> reticle_forced_tan[2]{};
		std::atomic<std::uint64_t> reticle_draws{};
		std::atomic<std::uint64_t> reticle_moved{};

		// Research (pipe "reticle dump <file>"): every indexed draw of one first-eye render that has the
		// stencil test on, with its depth-stencil, blend, scissor and viewport state.
		std::atomic<int> state_dump{}; // 1 = armed, 2 = collecting, 3 = done
		std::mutex state_dump_lock;
		std::vector<std::string> state_dump_lines;
		UINT state_dump_index{};

		void begin_state_dump()
		{
			int armed = 1;
			if (state_dump.compare_exchange_strong(armed, 2))
			{
				const std::lock_guard guard(state_dump_lock);
				state_dump_lines.clear();
				state_dump_index = 0;
			}
		}

		void end_state_dump()
		{
			int running = 2;
			state_dump.compare_exchange_strong(running, 3);
		}

		void dump_draw_state(ID3D11DeviceContext* c, UINT count, UINT start)
		{
			if (state_dump.load(std::memory_order_relaxed) != 2 || !in_eye_render)
			{
				return;
			}
			const UINT index = state_dump_index++;
			ComPtr<ID3D11DepthStencilState> state;
			UINT reference = 0;
			c->OMGetDepthStencilState(&state, &reference);
			D3D11_DEPTH_STENCIL_DESC d{};
			if (state)
			{
				state->GetDesc(&d);
			}
			if (!state || !d.StencilEnable)
			{
				return;
			}
			ComPtr<ID3D11BlendState> blend;
			float factor[4]{};
			UINT mask = 0;
			c->OMGetBlendState(&blend, factor, &mask);
			D3D11_BLEND_DESC b{};
			if (blend)
			{
				blend->GetDesc(&b);
			}
			ComPtr<ID3D11RasterizerState> raster;
			c->RSGetState(&raster);
			D3D11_RASTERIZER_DESC r{};
			if (raster)
			{
				raster->GetDesc(&r);
			}
			D3D11_VIEWPORT viewport{};
			UINT viewports = 1;
			c->RSGetViewports(&viewports, &viewport);
			D3D11_RECT scissor{};
			UINT scissors = 1;
			c->RSGetScissorRects(&scissors, &scissor);
			ComPtr<ID3D11PixelShader> shader;
			c->PSGetShader(&shader, nullptr, nullptr);
			ComPtr<ID3D11RenderTargetView> target;
			ComPtr<ID3D11DepthStencilView> depth;
			c->OMGetRenderTargets(1, &target, &depth);
			char text[512];
			std::snprintf(text, sizeof(text),
				"%u idx %u start %u ps %p rtv %p dsv %p | ref %u depth en %d write %d func %d | stencil rmask %02x wmask %02x front func %d pass %d fail %d zfail %d back func %d pass %d"
				" | blend %d src %d dst %d op %d wm %x | cull %d scissor %d (%ld %ld %ld %ld) | vp %.0f %.0f %.0f %.0f z %.3f %.3f",
				index, count, start, static_cast<void*>(shader.Get()), static_cast<void*>(target.Get()), static_cast<void*>(depth.Get()), reference, d.DepthEnable ? 1 : 0,
				static_cast<int>(d.DepthWriteMask), static_cast<int>(d.DepthFunc), d.StencilReadMask, d.StencilWriteMask, static_cast<int>(d.FrontFace.StencilFunc),
				static_cast<int>(d.FrontFace.StencilPassOp), static_cast<int>(d.FrontFace.StencilFailOp), static_cast<int>(d.FrontFace.StencilDepthFailOp),
				static_cast<int>(d.BackFace.StencilFunc), static_cast<int>(d.BackFace.StencilPassOp), b.RenderTarget[0].BlendEnable ? 1 : 0,
				static_cast<int>(b.RenderTarget[0].SrcBlend), static_cast<int>(b.RenderTarget[0].DestBlend), static_cast<int>(b.RenderTarget[0].BlendOp),
				static_cast<unsigned>(b.RenderTarget[0].RenderTargetWriteMask), static_cast<int>(r.CullMode), r.ScissorEnable ? 1 : 0, scissor.left, scissor.top, scissor.right,
				scissor.bottom, viewport.TopLeftX, viewport.TopLeftY, viewport.Width, viewport.Height, viewport.MinDepth, viewport.MaxDepth);
			const std::lock_guard guard(state_dump_lock);
			if (state_dump_lines.size() < 4000)
			{
				state_dump_lines.emplace_back(text);
			}
		}

		// The reticle's quad: 6 indices, blended, colour written, no depth write, stencil reference 2.
		bool reticle_draw(ID3D11DeviceContext* c, UINT count)
		{
			if (!in_eye_render || scope_rendering || count != reticle_indices)
			{
				return false;
			}
			ComPtr<ID3D11DepthStencilState> state;
			UINT reference = 0;
			c->OMGetDepthStencilState(&state, &reference);
			if (!state || reference != reticle_stencil)
			{
				return false;
			}
			D3D11_DEPTH_STENCIL_DESC desc{};
			state->GetDesc(&desc);
			ComPtr<ID3D11BlendState> blend;
			float factor[4]{};
			UINT sample_mask = 0;
			c->OMGetBlendState(&blend, factor, &sample_mask);
			if (!desc.StencilEnable || desc.DepthWriteMask != D3D11_DEPTH_WRITE_MASK_ZERO || !blend)
			{
				return false;
			}
			D3D11_BLEND_DESC blending{};
			blend->GetDesc(&blending);
			// #93: only the lens blend, ONE / INV_SRC_ALPHA, which every optic's own reticle quad uses (#137 S1). Every gun
			// also draws two additive (ONE / ONE) 6-index quads with stencil ref 2, iron sights included: drawn with the
			// lens shader they showed a red square on the sight while aiming.
			const D3D11_RENDER_TARGET_BLEND_DESC& target = blending.RenderTarget[0];
			return target.BlendEnable && target.RenderTargetWriteMask != 0 && target.SrcBlend == D3D11_BLEND_ONE && target.DestBlend == D3D11_BLEND_INV_SRC_ALPHA;
		}

		// The quad drawn with its own copy of the per-view block (VS and PS slot 2) in which the inverse
		// screen size (+0x18, +0x1C) puts the picture's anchor, 0.5 / inverse, on the sight line.
		constexpr UINT per_view_slot = 2;
		constexpr UINT per_view_bytes = 512;
		constexpr UINT inverse_size_offset = 0x18;
		ComPtr<ID3D11Buffer> reticle_block;  // the copy
		ComPtr<ID3D11Buffer> reticle_values; // the two floats, as a copy source

		bool draw_reticle_anchored(ID3D11DeviceContext* c, UINT count, UINT start, INT base, float tx, float ty)
		{
			ComPtr<ID3D11DeviceContext1> c1;
			D3D11_VIEWPORT viewport{};
			UINT viewports = 1;
			c->RSGetViewports(&viewports, &viewport);
			if (FAILED(c->QueryInterface(IID_PPV_ARGS(&c1))) || viewports != 1 || viewport.Width < 16.0f || viewport.Height < 16.0f
				|| !original_vs_set_constant_buffers1 || !original_ps_set_constant_buffers1)
			{
				return false;
			}
			ComPtr<ID3D11Buffer> vs_buffer, ps_buffer;
			UINT vs_first = 0, vs_count = 0, ps_first = 0, ps_count = 0;
			c1->VSGetConstantBuffers1(per_view_slot, 1, &vs_buffer, &vs_first, &vs_count);
			c1->PSGetConstantBuffers1(per_view_slot, 1, &ps_buffer, &ps_first, &ps_count);
			if (!ps_buffer || ps_count * 16 < per_view_bytes)
			{
				return false;
			}
			D3D11_BUFFER_DESC source{};
			ps_buffer->GetDesc(&source);
			if (ps_first * 16 + per_view_bytes > source.ByteWidth)
			{
				return false;
			}
			if (!reticle_block || !reticle_values)
			{
				ComPtr<ID3D11Device> device;
				c->GetDevice(&device);
				D3D11_BUFFER_DESC desc{};
				desc.ByteWidth = per_view_bytes;
				desc.Usage = D3D11_USAGE_DEFAULT;
				desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
				D3D11_BUFFER_DESC values = desc;
				values.ByteWidth = 16;
				values.BindFlags = D3D11_BIND_VERTEX_BUFFER;
				if (FAILED(device->CreateBuffer(&desc, nullptr, &reticle_block)) || FAILED(device->CreateBuffer(&values, nullptr, &reticle_values)))
				{
					reticle_block.Reset();
					reticle_values.Reset();
					return false;
				}
			}
			// The anchor moves by the sight line's place on screen: x right, y down.
			const float dx = tx * projection_sx * 0.5f * viewport.Width;
			const float dy = -ty * projection_sy * 0.5f * viewport.Height;
			const float inverse[4]{ 1.0f / std::fmax(viewport.Width + 2.0f * dx, 16.0f), 1.0f / std::fmax(viewport.Height + 2.0f * dy, 16.0f), 0.0f, 0.0f };
			c->UpdateSubresource(reticle_values.Get(), 0, nullptr, inverse, 0, 0);
			const UINT from = ps_first * 16;
			const D3D11_BOX head{ from, 0, 0, from + inverse_size_offset, 1, 1 };
			const D3D11_BOX two{ 0, 0, 0, 8, 1, 1 };
			const D3D11_BOX tail{ from + inverse_size_offset + 8, 0, 0, from + per_view_bytes, 1, 1 };
			c->CopySubresourceRegion(reticle_block.Get(), 0, 0, 0, 0, ps_buffer.Get(), 0, &head);
			c->CopySubresourceRegion(reticle_block.Get(), 0, inverse_size_offset, 0, 0, reticle_values.Get(), 0, &two);
			c->CopySubresourceRegion(reticle_block.Get(), 0, inverse_size_offset + 8, 0, 0, ps_buffer.Get(), 0, &tail);
			ID3D11Buffer* own = reticle_block.Get();
			const UINT zero = 0, whole = per_view_bytes / 16;
			const bool same = vs_buffer.Get() == ps_buffer.Get() && vs_first == ps_first;
			original_ps_set_constant_buffers1(c, per_view_slot, 1, &own, &zero, &whole);
			if (same)
			{
				original_vs_set_constant_buffers1(c, per_view_slot, 1, &own, &zero, &whole);
			}
			original_draw_indexed(c, count, start, base);
			ID3D11Buffer* ps_back = ps_buffer.Get();
			original_ps_set_constant_buffers1(c, per_view_slot, 1, &ps_back, &ps_first, &ps_count);
			if (same)
			{
				ID3D11Buffer* vs_back = vs_buffer.Get();
				original_vs_set_constant_buffers1(c, per_view_slot, 1, &vs_back, &vs_first, &vs_count);
			}
			return true;
		}

		// #39 step 3 (setting scope_in_frame): the magnifier's picture drawn in the game's frame at its lens, just
		// before the game's reticle quad, with that quad's stencil test (the lens mask: reference 2), so the glass
		// cuts it and it sits where each eye's own image has the optic. Before, an OpenXR quad showed it, composited
		// apart from the frame (11-20 degrees off in the right eye in the headset, 2026-11-12).
		std::atomic<bool> scope_in_frame_on{ true };
		std::mutex lens_eye_lock;
		pose lens_eye;            // the first eye as this frame renders it (camera_hook), OpenXR LOCAL
		bool lens_eye_valid{};
		std::atomic<std::uint64_t> lens_draws{};
		std::atomic<std::uint64_t> lens_skipped{};
		std::mutex lens_axes_lock;
		bool lens_axes_valid{};
		float lens_along[3]{}, lens_up[3]{}; // view space of the rendered eyes (both share the orientation)
		std::atomic<bool> scope_glass_on{ true };
		std::atomic<std::uint64_t> glass_masks{};
		std::atomic<std::uint64_t> glass_composed{};
		std::atomic<std::uint64_t> glass_no_picture{};
		std::atomic<float> glass_power_seen{};
		std::atomic<float> glass_scale_seen{};
		std::atomic<float> glass_line_uv[2]{}; // where the sight line falls in the scope picture (0.5, 0.5: its centre)
		std::atomic<int> glass_debug{}; // research (pipe glassdebug 1): the mask's quads in colour; 2: the reticle alone
		std::atomic<int> glass_quads_seen{};
		int glass_quads{};    // render thread: lens quads drawn into the mask in this eye render
		// Which lens draws are the glass (lens_math::choose_glass): every lens draw shows only where no first-person
		// geometry is (stencil test against 0x20), so its shape is not the glass; the ones the sight line passes
		// through are. Each draw's pixels, and its pixels at the line's point, are counted by occlusion queries, per eye
		// render (glass_eye, set where each eye render starts); a count is read back when the same query is issued again
		// (the same draw of the same eye a frame later).
		constexpr int glass_max_quads = glass_max_draws;
		int glass_eye{};      // render thread: the eye render in progress (0 first, 1 second)
		std::uint64_t glass_area[2][glass_max_quads]{};
		std::uint64_t glass_on_line[2][glass_max_quads]{};
		glass_choice glass_kept[2];
		std::uint64_t open_glass_optic{}; // render thread, #145: the optic last seen with its own glass on the line
		int glass_on_line_run{};          // render thread: the sighting eye's renders in a row with its glass on the line
		std::atomic<std::uint64_t> open_glass_seen{}; // the report's copy
		std::atomic<float> glass_field_seen{};
		std::atomic<float> scope_eye_box_m{ glass_eye_box }; // setting scope_eye_box (#139, owner 2026-11-24)
		std::atomic<std::uint64_t> glass_fallbacks{};
		std::atomic<ULONGLONG> glass_shown_at{};
		std::atomic<int> glass_pick_seen{ -1 };
		std::atomic<std::uint64_t> glass_area_seen[glass_max_quads]{};
		constexpr float glass_min_power = 1.25f;
		constexpr float virtual_eyepiece_radius = 0.02f; // metres: a closed-eyepiece scope gets this disc on its line
		// Owner 2026-11-24: the optic's own reticle in its glass. Each reticle quad of a magnifying optic drawn in an eye
		// render (the ACOG: its lines, then its chevron; a scope: one art) is kept with its texture and placement; the glass
		// samples them by the slope off the sight line, magnified with the world, in the art's own colours. Render thread.
		constexpr int glass_max_arts = 3;
		struct glass_art
		{
			ComPtr<ID3D11ShaderResourceView> view;
			float span{};      // the art's height as a slope through the glass
			float aspect{ 1.0f };
			float anchor[2]{ 0.5f, 0.5f };
		};
		glass_art glass_arts[2][glass_max_arts];
		int glass_art_count[2]{};
		std::atomic<int> glass_arts_seen{};
		std::atomic<float> glass_art_seen[glass_max_arts][4]{}; // size-free report: span, aspect, anchor

		// Research (pipe scope): the last reticle quads' material blocks (ps cb1, 20 floats) as read at their draw.
		std::mutex material_seen_lock;
		float material_seen[4][20]{};
		std::string material_context[4]; // the art's size, the pixel shader, every bound vs / ps block's first 16 floats
		unsigned material_seen_next{};

		void note_material_seen(ID3D11DeviceContext* c, ID3D11DeviceContext1* c1, const float (&block)[20])
		{
			std::string text;
			char line[160];
			ComPtr<ID3D11ShaderResourceView> view;
			c->PSGetShaderResources(3, 1, &view);
			if (view)
			{
				ComPtr<ID3D11Resource> resource;
				view->GetResource(&resource);
				ComPtr<ID3D11Texture2D> texture;
				if (resource && SUCCEEDED(resource.As(&texture)))
				{
					D3D11_TEXTURE2D_DESC desc{};
					texture->GetDesc(&desc);
					std::snprintf(line, sizeof(line), " t3=%ux%u", desc.Width, desc.Height);
					text += line;
				}
			}
			ComPtr<ID3D11PixelShader> shader;
			c->PSGetShader(&shader, nullptr, nullptr);
			std::snprintf(line, sizeof(line), " ps=%p", static_cast<void*>(shader.Get()));
			text += line;
			for (int stage = 0; stage < 2; ++stage)
			{
				for (UINT slot = 0; slot < 4; ++slot)
				{
					ComPtr<ID3D11Buffer> buffer;
					UINT first = 0, number = 0;
					if (stage == 0)
					{
						c1->VSGetConstantBuffers1(slot, 1, &buffer, &first, &number);
					}
					else
					{
						c1->PSGetConstantBuffers1(slot, 1, &buffer, &first, &number);
					}
					float values[32]{};
					if (!buffer || !read_arena_block(buffer.Get(), first, 8, values))
					{
						continue;
					}
					std::snprintf(line, sizeof(line), "\n    %s cb%u:", stage == 0 ? "vs" : "ps", slot);
					text += line;
					for (float value : values)
					{
						std::snprintf(line, sizeof(line), " %.4g", value);
						text += line;
					}
				}
			}
			const std::lock_guard guard(material_seen_lock);
			std::memcpy(material_seen[material_seen_next % 4], block, sizeof(block));
			material_context[material_seen_next % 4] = text;
			++material_seen_next;
		}

		bool copy_buffer_bytes(ID3D11DeviceContext* c, ID3D11Buffer* buffer, UINT offset, UINT bytes, std::vector<std::uint8_t>& out);

		// A scope reticle's quad (lens_math::scope_art_from_quad), read from its index and vertex buffers once per buffer
		// place (a read back stalls the GPU; a new gun or optic brings new places).
		struct scope_quad_entry
		{
			ID3D11Buffer* vertices{};
			ID3D11Buffer* indices{};
			UINT vertex_offset{}, index_offset{}, start{};
			INT base{};
			bool valid{};
			reticle_material art;
		};
		scope_quad_entry scope_quads[16];
		unsigned scope_quads_next{};
		std::atomic<std::uint64_t> scope_quad_reads{};
		std::atomic<std::uint64_t> scope_arts_noted{};

		bool scope_art_for_draw(ID3D11DeviceContext* c, UINT count, UINT start, INT base, reticle_material& art)
		{
			if (count != 6)
			{
				return false;
			}
			ComPtr<ID3D11Buffer> index_buffer;
			DXGI_FORMAT format{};
			UINT index_offset = 0;
			c->IAGetIndexBuffer(&index_buffer, &format, &index_offset);
			ComPtr<ID3D11Buffer> vertex_buffer;
			UINT stride = 0, vertex_offset = 0;
			c->IAGetVertexBuffers(0, 1, &vertex_buffer, &stride, &vertex_offset);
			if (!index_buffer || !vertex_buffer || stride < 28)
			{
				return false;
			}
			for (const scope_quad_entry& entry : scope_quads)
			{
				if (entry.vertices == vertex_buffer.Get() && entry.indices == index_buffer.Get() && entry.vertex_offset == vertex_offset && entry.index_offset == index_offset
					&& entry.start == start && entry.base == base)
				{
					art = entry.art;
					return entry.valid;
				}
			}
			scope_quad_entry& entry = scope_quads[scope_quads_next++ % 16];
			entry = {};
			entry.vertices = vertex_buffer.Get();
			entry.indices = index_buffer.Get();
			entry.vertex_offset = vertex_offset;
			entry.index_offset = index_offset;
			entry.start = start;
			entry.base = base;
			++scope_quad_reads;
			const UINT index_size = format == DXGI_FORMAT_R32_UINT ? 4 : 2;
			std::vector<std::uint8_t> index_bytes, vertex_bytes;
			if (!copy_buffer_bytes(c, index_buffer.Get(), index_offset + start * index_size, count * index_size, index_bytes))
			{
				return false;
			}
			UINT corners[4]{};
			int found = 0;
			for (UINT i = 0; i < count && found < 4; ++i)
			{
				UINT value = 0;
				std::memcpy(&value, index_bytes.data() + i * index_size, index_size);
				if (std::find(corners, corners + found, value) == corners + found)
				{
					corners[found++] = value;
				}
			}
			const UINT highest = *std::max_element(corners, corners + found);
			if (found != 4 || base < 0 || highest > 64
				|| !copy_buffer_bytes(c, vertex_buffer.Get(), vertex_offset + static_cast<UINT>(base) * stride, (highest + 1) * stride, vertex_bytes))
			{
				return false;
			}
			float position[4][2]{}, uv[4][2]{};
			for (int k = 0; k < 4; ++k)
			{
				std::uint16_t words[4]{}, uvs[2]{};
				std::memcpy(words, vertex_bytes.data() + corners[k] * stride, sizeof(words));
				std::memcpy(uvs, vertex_bytes.data() + corners[k] * stride + 24, sizeof(uvs));
				// A flat quad in its model (z 0, w 1).
				if (half_to_float(words[2]) != 0.0f || half_to_float(words[3]) != 1.0f)
				{
					return false;
				}
				position[k][0] = half_to_float(words[0]);
				position[k][1] = half_to_float(words[1]);
				uv[k][0] = half_to_float(uvs[0]);
				uv[k][1] = half_to_float(uvs[1]);
			}
			entry.valid = scope_art_from_quad(position, uv, entry.art);
			art = entry.art;
			return entry.valid;
		}

		void reset_glass_arts(int eye)
		{
			for (glass_art& slot : glass_arts[eye])
			{
				slot = {};
			}
			glass_art_count[eye] = 0;
		}

		void note_glass_art(ID3D11DeviceContext* c, const reticle_material& art, float span)
		{
			const int eye = glass_eye == 1 ? 1 : 0;
			if (glass_art_count[eye] >= glass_max_arts || span <= 0.0f)
			{
				return;
			}
			ComPtr<ID3D11ShaderResourceView> view;
			c->PSGetShaderResources(3, 1, &view);
			if (!view)
			{
				return;
			}
			glass_art& slot = glass_arts[eye][glass_art_count[eye]++];
			slot.view = view;
			slot.span = span;
			slot.aspect = art.aspect;
			slot.anchor[0] = art.anchor[0];
			slot.anchor[1] = art.anchor[1];
		}
		struct lens_reticle_resources
		{
			ComPtr<ID3D11Device> device;
			ComPtr<ID3D11PixelShader> ps;
			ComPtr<ID3D11PixelShader> mask_ps;
			ComPtr<ID3D11Buffer> constants;
			ComPtr<ID3D11SamplerState> sampler;
			ComPtr<ID3D11BlendState> mask_blend;
			ComPtr<ID3D11Texture2D> mask;         // #139: which lens quads cover each pixel (r, bit k = quad k), the reticle (a)
			ComPtr<ID3D11Query> area[2][glass_max_quads];
			bool area_issued[2][glass_max_quads]{};
			ComPtr<ID3D11Query> on_line[2][glass_max_quads]; // the quad's pixels at the sight line's point
			bool on_line_issued[2][glass_max_quads]{};
			ComPtr<ID3D11PixelShader> probe_ps;
			ComPtr<ID3D11BlendState> probe_blend;
			ComPtr<ID3D11RenderTargetView> mask_target;
			ComPtr<ID3D11ShaderResourceView> mask_view;
			bool failed{};
		} lens_reticle;
		// Research (pipe lensfix <dx eye 1> <dx eye 2>): metres added to each eye's view x; and what the last
		// draw of each eye used, for the report.
		std::atomic<float> lens_extra_dx[2]{};
		std::mutex lens_debug_lock;
		float lens_debug_eye[2][3]{};
		float lens_debug_centre[2][4]{}; // the lens centre in view space, and its ndc x
		float lens_debug_quad[3]{};
		const char lens_shader[] = R"(
cbuffer corners : register(b0)
{
	float4 clip[4];
	// #139 scope_glass, ps_glass only:
	float4 view;   // the eye's image: x, y, width, height
	float4 proj;   // x scale, y scale, mask pixels per image pixel (x, y)
	float4 right;  // the gun's right, up and sight line in this eye's view space; right.w: the power
	float4 up;
	float4 along;  // w: the field stop (slope off the line), closing as the eye leaves the line
	float4 glass;  // picture uv per world tangent, the lens quads that are the glass (bits), 1 = dark (not the sighting eye)
	float4 colour; // the reticle's light marks
	float4 arts[3]; // the optic's own reticle arts: their point on the line (uv), height span (slope), aspect
	float4 art_count;  // x: arts; yzw: the picture's right (#158)
	float4 picture_up; // xyz: the picture's up, in this eye's view space
};
Texture2D picture : register(t0);
Texture2D mask : register(t1);
Texture2D art0 : register(t2);
Texture2D art1 : register(t3);
Texture2D art2 : register(t4);
SamplerState linear_clamp : register(s0);
float4 art_mark(Texture2D art, float4 place, float2 slope)
{
	float2 uv = place.xy + float2(slope.x / (place.z * place.w), -slope.y / place.z);
	return all(uv >= 0.0) && all(uv <= 1.0) ? art.SampleLevel(linear_clamp, uv, 0) : float4(0.0, 0.0, 0.0, 0.0);
}
// Over the seen picture in the art's own colours: dark lines dark, light marks (white art) in the sight's colour.
float3 over_art(float3 seen, float4 a)
{
	return seen * (1.0 - a.a) + a.rgb * colour.rgb * a.a;
}
struct v2p { float4 position : SV_Position; float2 uv : TEXCOORD0; };
v2p vs(uint id : SV_VertexID)
{
	v2p o;
	o.position = clip[id];
	o.uv = float2(id & 1, id >> 1);
	return o;
}
float4 ps(v2p i) : SV_Target { return picture.Sample(linear_clamp, i.uv); }
v2p vs_full(uint id : SV_VertexID)
{
	v2p o;
	o.uv = float2((id << 1) & 2, id & 2);
	o.position = float4(o.uv * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
	return o;
}
// Inside the optic's glass (every lens quad of this eye render covers it): Great Destroyer's lens, what is seen at a
// slope off the sight line is the world at slope / power, read from the scope picture (centred on the sight line);
// past the picture's disc black, the tube; the reticle over it.
float4 ps_glass(v2p i) : SV_Target
{
	float4 m = mask.Load(int3(i.position.xy * proj.zw, 0));
	uint quads = (uint)round(m.r * 255.0);
	float2 ndc = float2((i.position.x - view.x) / view.z * 2.0 - 1.0, 1.0 - (i.position.y - view.y) / view.w * 2.0);
	if (glass.w > 1.5) // research (pipe glassdebug 2): the glass black, the reticle art in it pure green (its drawn size)
	{
		uint glass_bits = glass.y < -0.5 ? 0xffu : (uint)glass.y;
		if ((quads & glass_bits) == 0)
			discard;
		return float4(0.0, m.a, 0.0, 1.0);
	}
	if (glass.w > 0.5) // research (pipe glassdebug): quad 0 red, 1 green, 2 blue; the sight line's point magenta
	{
		float2 line_at = float2(along.x / -along.z * proj.x, along.y / -along.z * proj.y);
		if (along.z < 0.0 && length((ndc - line_at) * view.zw * 0.5) < 10.0)
			return float4(1.0, 0.0, 1.0, 1.0); // magenta
		if (quads == 0)
			discard;
		return float4(quads & 1, (quads >> 1) & 1, (quads >> 2) & 1, 1.0) * 0.6;
	}
	// glass.y >= 0: the lens draws (bits) that are the glass; -1: a virtual eyepiece, a disc colour.w (slope) round the
	// sight line, for an optic whose own glass is closed (the SRS's hip model).
	bool virtual_eyepiece = glass.y < -0.5;
	if (!virtual_eyepiece && (quads & (uint)glass.y) == 0)
		discard;
	float3 ray = float3(ndc.x / proj.x, ndc.y / proj.y, -1.0);
	float ahead = dot(ray, along.xyz);
	if (virtual_eyepiece && (ahead <= 0.0 || length(float2(dot(ray, right.xyz), dot(ray, up.xyz)) / ahead) > colour.w))
		discard;
	float3 seen = float3(0.0, 0.0, 0.0);
	if (ahead > 0.0 && glass.z < 0.5)
	{
		float2 slope = float2(dot(ray, right.xyz), dot(ray, up.xyz)) / ahead;
		if (virtual_eyepiece && art_count.x < 0.5 && length(slope) < 0.0035) // an aim point when no reticle art was seen
			return float4(colour.rgb, 1.0);
		// The world at slope / power off the line (right.w: the power), found in the picture, which is rendered along
		// the scope camera, the gun's line (#158: its axes in this eye's view; glass.x: uv per world tangent).
		float3 way = along.xyz + (right.xyz * slope.x + up.xyz * slope.y) / right.w;
		float3 picture_right = art_count.yzw;
		float3 picture_ahead = cross(picture_up.xyz, picture_right);
		float forward = dot(way, picture_ahead);
		if (forward > 0.0)
		{
			float2 uv = 0.5 + float2(dot(way, picture_right), -dot(way, picture_up.xyz)) / forward * glass.x;
			if (length(uv - 0.5) <= 0.5)
				seen = picture.SampleLevel(linear_clamp, uv, 0).rgb;
		}
		float field = 1.0 - smoothstep(along.w * 0.8, along.w, length(slope)); // field stop
		if (art_count.x > 0.5)
		{
			// The optic's own reticle (the focal plane: magnified with the world, dark with it past the field stop).
			seen = over_art(seen, art_mark(art0, arts[0], slope));
			if (art_count.x > 1.5)
				seen = over_art(seen, art_mark(art1, arts[1], slope));
			if (art_count.x > 2.5)
				seen = over_art(seen, art_mark(art2, arts[2], slope));
			return float4(seen * field, 1.0);
		}
		seen *= field;
	}
	return float4(seen * (1.0 - m.a) + colour.rgb * m.a, 1.0);
}
)";
		struct lens_resources
		{
			ComPtr<ID3D11Device> device;
			ComPtr<ID3D11VertexShader> vs;
			ComPtr<ID3D11PixelShader> ps;
			ComPtr<ID3D11VertexShader> glass_vs; // #139
			ComPtr<ID3D11PixelShader> glass_ps;
			ComPtr<ID3D11Buffer> corners;
			ComPtr<ID3D11SamplerState> sampler;
			ComPtr<ID3D11BlendState> blend;
			ComPtr<ID3D11RasterizerState> raster;
			ID3D11Resource* target{};
			ComPtr<ID3D11RenderTargetView> target_view;
			ID3D11Texture2D* texture{};
			ComPtr<ID3D11ShaderResourceView> view;
			bool failed{};
		} lens;

		bool lens_pipeline(ID3D11DeviceContext* c)
		{
			ComPtr<ID3D11Device> device;
			c->GetDevice(&device);
			if (lens.failed || !device)
			{
				return false;
			}
			if (lens.device.Get() == device.Get() && lens.ps)
			{
				return true;
			}
			lens = {};
			lens.device = device;
			ComPtr<ID3DBlob> vs, ps, glass_vs, glass_ps, messages;
			if (FAILED(D3DCompile(lens_shader, sizeof(lens_shader) - 1, "lens", nullptr, nullptr, "vs", "vs_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &vs, &messages))
				|| FAILED(D3DCompile(lens_shader, sizeof(lens_shader) - 1, "lens", nullptr, nullptr, "ps", "ps_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &ps, &messages))
				|| FAILED(D3DCompile(lens_shader, sizeof(lens_shader) - 1, "lens", nullptr, nullptr, "vs_full", "vs_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &glass_vs, &messages))
				|| FAILED(D3DCompile(lens_shader, sizeof(lens_shader) - 1, "lens", nullptr, nullptr, "ps_glass", "ps_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &glass_ps, &messages)))
			{
				log::write("eye pass: lens shader failed: %s", messages ? static_cast<const char*>(messages->GetBufferPointer()) : "");
				lens.failed = true;
				return false;
			}
			D3D11_BUFFER_DESC buffer{};
			buffer.ByteWidth = 16 * 16; // VSSetConstantBuffers1 binds whole multiples of 16 constants
			buffer.Usage = D3D11_USAGE_DEFAULT;
			buffer.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
			D3D11_SAMPLER_DESC sampler{};
			sampler.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
			sampler.AddressU = sampler.AddressV = sampler.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
			sampler.MaxLOD = D3D11_FLOAT32_MAX;
			D3D11_BLEND_DESC blend{};
			blend.RenderTarget[0].BlendEnable = TRUE; // the picture is premultiplied (scope_compose)
			blend.RenderTarget[0].SrcBlend = D3D11_BLEND_ONE;
			blend.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
			blend.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
			blend.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
			blend.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
			blend.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
			blend.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_RED | D3D11_COLOR_WRITE_ENABLE_GREEN | D3D11_COLOR_WRITE_ENABLE_BLUE;
			D3D11_RASTERIZER_DESC raster{};
			raster.FillMode = D3D11_FILL_SOLID;
			raster.CullMode = D3D11_CULL_NONE;
			raster.DepthClipEnable = FALSE;
			if (FAILED(device->CreateVertexShader(vs->GetBufferPointer(), vs->GetBufferSize(), nullptr, &lens.vs))
				|| FAILED(device->CreatePixelShader(ps->GetBufferPointer(), ps->GetBufferSize(), nullptr, &lens.ps))
				|| FAILED(device->CreateVertexShader(glass_vs->GetBufferPointer(), glass_vs->GetBufferSize(), nullptr, &lens.glass_vs))
				|| FAILED(device->CreatePixelShader(glass_ps->GetBufferPointer(), glass_ps->GetBufferSize(), nullptr, &lens.glass_ps))
				|| FAILED(device->CreateBuffer(&buffer, nullptr, &lens.corners)) || FAILED(device->CreateSamplerState(&sampler, &lens.sampler))
				|| FAILED(device->CreateBlendState(&blend, &lens.blend)) || FAILED(device->CreateRasterizerState(&raster, &lens.raster)))
			{
				log::write("eye pass: lens pipeline failed");
				lens.failed = true;
				return false;
			}
			return true;
		}

		// v = conjugate(q) * (p - origin): a LOCAL point in the axes of a view at origin with orientation q.
		void to_view(const pose& eye, const float (&p)[3], float (&v)[3])
		{
			const float d[3]{ p[0] - eye.position[0], p[1] - eye.position[1], p[2] - eye.position[2] };
			const float* q = eye.orientation;
			const float ux = -q[0], uy = -q[1], uz = -q[2], w = q[3];
			const float cx = uy * d[2] - uz * d[1], cy = uz * d[0] - ux * d[2], cz = ux * d[1] - uy * d[0];
			v[0] = d[0] + 2.0f * (w * cx + uy * cz - uz * cy);
			v[1] = d[1] + 2.0f * (w * cy + uz * cx - ux * cz);
			v[2] = d[2] + 2.0f * (w * cz + ux * cy - uy * cx);
		}

		void rotate_by(const float (&q)[4], const float (&v)[3], float (&out)[3])
		{
			const float cx = q[1] * v[2] - q[2] * v[1], cy = q[2] * v[0] - q[0] * v[2], cz = q[0] * v[1] - q[1] * v[0];
			out[0] = v[0] + 2.0f * (q[3] * cx + q[1] * cz - q[2] * cy);
			out[1] = v[1] + 2.0f * (q[3] * cy + q[2] * cx - q[0] * cz);
			out[2] = v[2] + 2.0f * (q[3] * cz + q[0] * cy - q[1] * cx);
		}

		// Render thread, right after an eye render (which: 0 first, 1 second), before its image is copied out: the
		// picture onto that image. True when it was drawn.
		bool draw_lens_picture(ID3D11DeviceContext* c, int which)
		{
			// #139: an optic's glass was marked in this eye render: the picture goes into it, not onto a flat quad; the
			// mask is cleared for the next eye render either way.
			const int quads = glass_quads;
			glass_quads = 0;
			const int ring = which == 1 ? 1 : 0;
			const std::uint64_t* area = glass_area[ring];
			const unsigned bits = choose_glass(glass_kept[ring], area, glass_on_line[ring], quads);
			const int pick = bits ? glass_kept[ring].smallest : -1;
			glass_pick_seen = pick;
			const bool masked = quads > 0 && (bits != 0 || glass_debug.load(std::memory_order_relaxed) == 1); // debug: every lens draw
			struct clear_mask
			{
				ID3D11DeviceContext* c;
				bool on;
				~clear_mask()
				{
					if (on && c && lens_reticle.mask_target)
					{
						const float zero[4]{};
						c->ClearRenderTargetView(lens_reticle.mask_target.Get(), zero);
					}
				}
			} clear_after{ c, quads > 0 };
			if (!c || !original_draw || projection_sx <= 0.0f || projection_sy <= 0.0f)
			{
				return false;
			}
			const scope_image lens_image = latest_scope_image();
			if (!lens_image.texture || GetTickCount64() - lens_image.at > 200)
			{
				return false;
			}
			// With the glass on, a magnified optic shows the picture only through the glass: its lens draws on the sight
			// line, else (its own glass closed, the SRS's hip model) a virtual eyepiece on the line; only to the eye behind
			// the sight (a real scope shows nothing to the eye beside it). Never the flat picture then.
			const bool glass_mode = scope_glass_on.load(std::memory_order_relaxed) && lens_image.magnification >= glass_min_power;
			// #145 (headset 2026-11-24): the virtual eyepiece is only for an optic whose own glass is closed. A flip magnifier,
			// or an optic whose glass has been on the line since it was raised, has its own glass: with the eye a little off
			// the line it showed a second, flat disc 15 cm ahead of the eye in front of the real optic.
			const std::uint64_t optic = optic_id_now();
			if (lens_image.eye == which)
			{
				// Fifteen eye renders in a row with a lens draw on the line: a lens draw crossing it for a moment as the gun
				// moves (the SRS's) is no open glass.
				glass_on_line_run = glass_mode && masked && bits != 0 ? glass_on_line_run + 1 : 0;
				if (glass_on_line_run >= 15)
				{
					open_glass_optic = optic;
					open_glass_seen = optic;
				}
			}
			const bool closed_eyepiece = optic != 0 && optic != open_glass_optic && !optic_is_magnifier_now();
			const bool virtual_eyepiece = glass_mode && !masked && lens_image.eye == which && closed_eyepiece;
			const bool glass = (glass_mode && masked) || virtual_eyepiece || (masked && glass_debug.load(std::memory_order_relaxed) == 1);
			if (virtual_eyepiece)
			{
				++glass_fallbacks;
			}
			if (!glass && (glass_mode || !scope_in_frame_on.load(std::memory_order_relaxed)))
			{
				++lens_skipped;
				return false;
			}
			pose eye;
			{
				const std::lock_guard guard(lens_eye_lock);
				if (!lens_eye_valid)
				{
					return false;
				}
				eye = lens_eye;
			}
			// The lens's corners (the image top along the lens's up), in this eye's view; the second eye's camera is the
			// first one moved right by the eye shift.
			float right[3]{}, up[3]{};
			const float x_axis[3]{ 1.0f, 0.0f, 0.0f }, y_axis[3]{ 0.0f, 1.0f, 0.0f };
			const float(&q)[4] = lens_image.quad.orientation;
			rotate_by(q, x_axis, right);
			rotate_by(q, y_axis, up);
			const float half = 0.5f * lens_image.quad_size;
			const float sides[4][2]{ { -1.0f, 1.0f }, { 1.0f, 1.0f }, { -1.0f, -1.0f }, { 1.0f, -1.0f } };
			const float eye_shift = (which == 1 ? block_shift() : 0.0f) - lens_extra_dx[which].load(std::memory_order_relaxed);
			{
				float centre[3]{};
				to_view(eye, lens_image.quad.position, centre);
				centre[0] -= eye_shift;
				const std::lock_guard guard(lens_debug_lock);
				std::memcpy(lens_debug_eye[which], eye.position, sizeof(lens_debug_eye[which]));
				std::memcpy(lens_debug_centre[which], centre, sizeof(centre));
				lens_debug_centre[which][3] = centre[2] < -0.01f ? projection_sx * centre[0] / -centre[2] : 0.0f;
				std::memcpy(lens_debug_quad, lens_image.quad.position, sizeof(lens_debug_quad));
			}
			float clip[16][4]{}; // the 4 corners, padded to the 16 constants a binding takes
			for (int k = 0; k < 4; ++k)
			{
				float corner[3]{}, v[3]{};
				for (int i = 0; i < 3; ++i)
				{
					corner[i] = lens_image.quad.position[i] + (sides[k][0] * right[i] + sides[k][1] * up[i]) * half;
				}
				to_view(eye, corner, v);
				v[0] -= eye_shift;
				if (v[2] > -0.01f && !glass) // the glass reads the picture by angle, not through this quad
				{
					++lens_skipped;
					return false;
				}
				clip[k][0] = projection_sx * v[0];
				clip[k][1] = projection_sy * v[1];
				clip[k][2] = 0.5f * -v[2];
				clip[k][3] = -v[2];
			}
			if (!lens_pipeline(c))
			{
				return false;
			}
			if (lens.texture != lens_image.texture)
			{
				lens.view.Reset();
				lens.texture = lens_image.texture;
				if (FAILED(lens.device->CreateShaderResourceView(lens_image.texture, nullptr, &lens.view)))
				{
					lens.texture = nullptr;
					return false;
				}
			}
			// Onto the eye's finished image (tonemapped, as the scope picture is): the back buffer, whole.
			ID3D11Resource* const target = back_buffer.load();
			if (!target)
			{
				return false;
			}
			if (lens.target != target)
			{
				lens.target_view.Reset();
				lens.target = target;
				if (FAILED(lens.device->CreateRenderTargetView(target, nullptr, &lens.target_view)))
				{
					lens.target = nullptr;
					return false;
				}
			}
			ComPtr<ID3D11Texture2D> target_texture;
			D3D11_TEXTURE2D_DESC target_desc{};
			if (FAILED(target->QueryInterface(IID_PPV_ARGS(&target_texture))))
			{
				return false;
			}
			target_texture->GetDesc(&target_desc);
			if (glass)
			{
				D3D11_TEXTURE2D_DESC mask_desc{};
				lens_reticle.mask->GetDesc(&mask_desc);
				float along[3]{}, line_up[3]{}, r[3]{}, u[3]{};
				{
					const std::lock_guard guard(lens_axes_lock);
					std::memcpy(along, lens_along, sizeof(along));
					std::memcpy(line_up, lens_up, sizeof(line_up));
				}
				// The picture: rendered along this eye's axis, its disc lens_tan wide (glass_picture_uv).
				const float scale = lens_image.lens_tan > 1e-4f && lens_image.magnification >= 1.0f ? 0.5f / lens_image.lens_tan : 0.0f;
				if (!lens_axes(along, line_up, r, u) || scale <= 0.0f)
				{
					return false;
				}
				{
					const float zero[2]{};
					float line_uv[2]{};
					glass_picture_uv(along, r, u, zero, lens_image.magnification, lens_image.lens_tan, lens_image.picture, line_uv);
					glass_line_uv[0] = line_uv[0];
					glass_line_uv[1] = line_uv[1];
				}
				glass_scale_seen = scale;
				// The eye's distance off the sight line closes the field stop (a scope's eye box).
				float eye_off = 0.0f;
				{
					const body::weapon_points points = body::latest_weapon_points();
					if (points.has_sight)
					{
						float p[3]{};
						to_view(eye, points.sight_eye, p);
						p[0] -= eye_shift;
						const float a = p[0] * along[0] + p[1] * along[1] + p[2] * along[2];
						const float d[3]{ p[0] - a * along[0], p[1] - a * along[1], p[2] - a * along[2] };
						eye_off = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
					}
				}
				const float field = glass_field_stop(eye_off, scope_eye_box_m.load(std::memory_order_relaxed));
				if (virtual_eyepiece && field <= 0.0f)
				{
					++lens_skipped; // the eye is not behind the closed scope's eyepiece: nothing to see through it
					return false;
				}
				// The virtual eyepiece sits where the flat picture's lens would (14.8 cm ahead of the sight's eye point).
				float quad_view[3]{};
				to_view(eye, lens_image.quad.position, quad_view);
				quad_view[0] -= eye_shift;
				const float centre_distance = std::sqrt(quad_view[0] * quad_view[0] + quad_view[1] * quad_view[1] + quad_view[2] * quad_view[2]);
				glass_field_seen = field;
				const float width = static_cast<float>(target_desc.Width), height = static_cast<float>(target_desc.Height);
				const int art_eye = which == 1 ? 1 : 0;
				const int arts = glass_art_count[art_eye];
				glass_arts_seen = arts;
				// #158: the picture's axes in this eye's view (both eyes share the orientation).
				float picture_right[3]{}, picture_up[3]{};
				rotate_by(lens_image.picture, x_axis, picture_right);
				rotate_by(lens_image.picture, y_axis, picture_up);
				const float glass_constants[12][4]{
					{ 0.0f, 0.0f, width, height },
					{ projection_sx, projection_sy, static_cast<float>(mask_desc.Width) / width, static_cast<float>(mask_desc.Height) / height },
					{ r[0], r[1], r[2], lens_image.magnification },
					{ u[0], u[1], u[2], 0.0f },
					{ along[0], along[1], along[2], field },
					{ scale, virtual_eyepiece ? -1.0f : static_cast<float>(bits), lens_image.eye == which ? 0.0f : 1.0f, static_cast<float>(glass_debug.load(std::memory_order_relaxed)) },
					{ 1.0f, 0.12f, 0.06f, virtual_eyepiece ? virtual_eyepiece_radius / (std::max)(0.05f, centre_distance) : 1.0f },
					{ glass_arts[art_eye][0].anchor[0], glass_arts[art_eye][0].anchor[1], glass_arts[art_eye][0].span, glass_arts[art_eye][0].aspect },
					{ glass_arts[art_eye][1].anchor[0], glass_arts[art_eye][1].anchor[1], glass_arts[art_eye][1].span, glass_arts[art_eye][1].aspect },
					{ glass_arts[art_eye][2].anchor[0], glass_arts[art_eye][2].anchor[1], glass_arts[art_eye][2].span, glass_arts[art_eye][2].aspect },
					{ static_cast<float>(arts), picture_right[0], picture_right[1], picture_right[2] },
					{ picture_up[0], picture_up[1], picture_up[2], 0.0f },
				};
				std::memcpy(&clip[4][0], glass_constants, sizeof(glass_constants));
				for (int k = 0; k < glass_max_arts; ++k)
				{
					glass_art_seen[k][0] = k < arts ? glass_arts[art_eye][k].span : 0.0f;
					glass_art_seen[k][1] = k < arts ? glass_arts[art_eye][k].aspect : 0.0f;
					glass_art_seen[k][2] = k < arts ? glass_arts[art_eye][k].anchor[0] : 0.0f;
					glass_art_seen[k][3] = k < arts ? glass_arts[art_eye][k].anchor[1] : 0.0f;
				}
				glass_quads_seen = quads;
				for (int k = 0; k < glass_max_quads; ++k)
				{
					glass_area_seen[k] = k < quads ? area[k] : 0;
				}
			}
			// The game's state, put back afterwards.
			ComPtr<ID3D11RenderTargetView> old_targets[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT];
			ComPtr<ID3D11DepthStencilView> old_depth_view;
			{
				ID3D11RenderTargetView* targets[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT]{};
				ID3D11DepthStencilView* depth_view = nullptr;
				c->OMGetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, targets, &depth_view);
				for (UINT i = 0; i < D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT; ++i)
				{
					old_targets[i].Attach(targets[i]);
				}
				old_depth_view.Attach(depth_view);
			}
			D3D11_VIEWPORT old_viewports[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE]{};
			UINT viewport_count = D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;
			c->RSGetViewports(&viewport_count, old_viewports);
			ComPtr<ID3D11DepthStencilState> depth;
			UINT reference = 0;
			c->OMGetDepthStencilState(&depth, &reference);
			ComPtr<ID3D11BlendState> blend;
			float factor[4]{};
			UINT sample_mask = 0;
			c->OMGetBlendState(&blend, factor, &sample_mask);
			ComPtr<ID3D11RasterizerState> raster;
			c->RSGetState(&raster);
			D3D11_PRIMITIVE_TOPOLOGY topology{};
			c->IAGetPrimitiveTopology(&topology);
			ComPtr<ID3D11InputLayout> layout;
			c->IAGetInputLayout(&layout);
			ComPtr<ID3D11VertexShader> vs;
			ComPtr<ID3D11PixelShader> ps;
			c->VSGetShader(&vs, nullptr, nullptr);
			c->PSGetShader(&ps, nullptr, nullptr);
			ComPtr<ID3D11DeviceContext1> c1;
			if (FAILED(c->QueryInterface(IID_PPV_ARGS(&c1))))
			{
				return false;
			}
			ComPtr<ID3D11Buffer> vs_cb, ps_cb;
			UINT vs_first = 0, vs_count = 0, ps_first = 0, ps_count = 0;
			c1->VSGetConstantBuffers1(0, 1, &vs_cb, &vs_first, &vs_count);
			c1->PSGetConstantBuffers1(0, 1, &ps_cb, &ps_first, &ps_count);
			ComPtr<ID3D11ShaderResourceView> srv;
			c->PSGetShaderResources(0, 1, &srv);
			ComPtr<ID3D11SamplerState> sampler;
			c->PSGetSamplers(0, 1, &sampler);
			ComPtr<ID3D11ShaderResourceView> srv_mask;
			c->PSGetShaderResources(1, 1, &srv_mask);
			ID3D11ShaderResourceView* old_arts[glass_max_arts]{};
			c->PSGetShaderResources(2, glass_max_arts, old_arts);

			c->UpdateSubresource(lens.corners.Get(), 0, nullptr, clip, 0, 0);
			c->IASetPrimitiveTopology(glass ? D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST : D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
			c->IASetInputLayout(nullptr);
			c->VSSetShader(glass ? lens.glass_vs.Get() : lens.vs.Get(), nullptr, 0);
			c->PSSetShader(glass ? lens.glass_ps.Get() : lens.ps.Get(), nullptr, 0);
			ID3D11Buffer* corners = lens.corners.Get();
			const UINT zero = 0, sixteen = 16;
			original_vs_set_constant_buffers1(c, 0, 1, &corners, &zero, &sixteen);
			if (glass)
			{
				original_ps_set_constant_buffers1(c, 0, 1, &corners, &zero, &sixteen);
				ID3D11ShaderResourceView* mask = lens_reticle.mask_view.Get();
				c->PSSetShaderResources(1, 1, &mask);
				const int art_eye = which == 1 ? 1 : 0;
				ID3D11ShaderResourceView* art_views[glass_max_arts]{};
				for (int k = 0; k < glass_art_count[art_eye]; ++k)
				{
					art_views[k] = glass_arts[art_eye][k].view.Get();
				}
				c->PSSetShaderResources(2, glass_max_arts, art_views);
			}
			ID3D11ShaderResourceView* view = lens.view.Get();
			c->PSSetShaderResources(0, 1, &view);
			ID3D11SamplerState* linear = lens.sampler.Get();
			c->PSSetSamplers(0, 1, &linear);
			ID3D11RenderTargetView* own_target = lens.target_view.Get();
			c->OMSetRenderTargets(1, &own_target, nullptr);
			const D3D11_VIEWPORT whole{ 0.0f, 0.0f, static_cast<float>(target_desc.Width), static_cast<float>(target_desc.Height), 0.0f, 1.0f };
			c->RSSetViewports(1, &whole);
			c->OMSetBlendState(lens.blend.Get(), nullptr, 0xffffffff);
			c->OMSetDepthStencilState(nullptr, 0);
			c->RSSetState(lens.raster.Get());
			if (glass)
			{
				original_draw(c, 3, 0);
				++glass_composed;
				glass_shown_at = GetTickCount64();
				ID3D11ShaderResourceView* back_mask = srv_mask.Get();
				c->PSSetShaderResources(1, 1, &back_mask);
				c->PSSetShaderResources(2, glass_max_arts, old_arts);
			}
			else
			{
				original_draw(c, 4, 0);
				++lens_draws;
			}
			for (ID3D11ShaderResourceView* old_art : old_arts)
			{
				if (old_art)
				{
					old_art->Release();
				}
			}
			{
				ID3D11RenderTargetView* targets[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT]{};
				for (UINT i = 0; i < D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT; ++i)
				{
					targets[i] = old_targets[i].Get();
				}
				c->OMSetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, targets, old_depth_view.Get());
			}
			c->RSSetViewports(viewport_count, old_viewports);

			c->IASetPrimitiveTopology(topology);
			c->IASetInputLayout(layout.Get());
			c->VSSetShader(vs.Get(), nullptr, 0);
			c->PSSetShader(ps.Get(), nullptr, 0);
			ID3D11Buffer* back_vs = vs_cb.Get();
			ID3D11Buffer* back_ps = ps_cb.Get();
			if (vs_count)
			{
				original_vs_set_constant_buffers1(c, 0, 1, &back_vs, &vs_first, &vs_count);
			}
			else
			{
				original_vs_set_constant_buffers1(c, 0, 1, &back_vs, nullptr, nullptr);
			}
			if (ps_count)
			{
				original_ps_set_constant_buffers1(c, 0, 1, &back_ps, &ps_first, &ps_count);
			}
			else
			{
				original_ps_set_constant_buffers1(c, 0, 1, &back_ps, nullptr, nullptr);
			}
			ID3D11ShaderResourceView* back_srv = srv.Get();
			c->PSSetShaderResources(0, 1, &back_srv);
			ID3D11SamplerState* back_sampler = sampler.Get();
			c->PSSetSamplers(0, 1, &back_sampler);
			c->OMSetBlendState(blend.Get(), factor, sample_mask);
			c->OMSetDepthStencilState(depth.Get(), reference);
			c->RSSetState(raster.Get());
			return true;
		}

		// sight_lens (#39, the BF3 single-player mod's lens, on the game's own reticle quad): the quad the game draws
		// on the optic's glass keeps its vertex shader, geometry, depth and stencil; only its pixel shader is ours.
		// Per pixel: the ray from this eye through it, its slope off the gun's sight line in the gun's own axes, and
		// the game's reticle art (the material's t3 alpha) looked up by that slope. So the reticle keeps its true
		// angular size at any distance from the eye, turns with the gun (not the head), and stays on what the sight
		// points at from anywhere behind the glass. Its size: the material's own size (ps cb1 float 8: 0.038 of the
		// screen height for the EOTech) at its zoom level's view (#140, lens_math::reticle_level_fov; red dots and holos
		// 40 degrees: 1.13 degrees of ring; sight_lens_fov of 10 or more overrides).
		std::atomic<bool> sight_lens_on{ true };
		std::atomic<float> sight_lens_fov{ 0.0f }; // #140: 0 = the zoom level's own view (lens_math::reticle_fov)
		std::atomic<float> sight_lens_brightness{ 4.0f };
		std::atomic<std::uint64_t> lens_reticles{};
		std::atomic<float> lens_size_seen{};
		std::atomic<float> lens_span_seen{}; // #140: the art's span (slope) last used
		// #139 scope_glass: an optic whose selected level magnifies (a scope, a flipped magnifier; the power from the
		// game's zoom data, #138) shows the scope picture in its own glass. The game's lens quad of that optic draws
		// only a mask here (its own geometry, depth and stencil: so the rim and the hands cover it as they cover the
		// glass), with the reticle art in its second channel; right after the eye render the picture is composed into
		// the eye's image inside that mask (draw_lens_picture). The picture cannot come from the eye's own image: the
		// first-person gun is in the G-buffer (stencil 32) and lit together with the world, so every copy of the lit
		// image already holds the optic's own housing (S3 monitor check, re/notes/weapon.md). A reflex sight (power 1)
		// keeps the reticle-only lens above.
		const char lens_reticle_shader[] = R"(
cbuffer lens : register(b13)
{
	float4 view;   // viewport x, y, width, height
	float4 proj;   // x scale, y scale, the reticle's span (slope across the art), brightness
	float4 right;  // the gun's right, up and sight line in the eye's view space
	float4 up;
	float4 along;
	float4 colour;
	float4 place; // the art's point on the sight line (uv), its aspect (width / height) (lens_math::reticle_art_uv)
};
Texture2D art : register(t3);
SamplerState linear_clamp : register(s15);
float4 art_at(float4 position, out bool ahead)
{
	float2 ndc = float2((position.x - view.x) / view.z * 2.0 - 1.0, 1.0 - (position.y - view.y) / view.w * 2.0);
	float3 ray = float3(ndc.x / proj.x, ndc.y / proj.y, -1.0);
	float forward = dot(ray, along.xyz);
	ahead = forward > 0.0;
	float2 slope = float2(dot(ray, right.xyz), dot(ray, up.xyz)) / max(forward, 1e-6);
	float2 uv = place.xy + float2(slope.x / (proj.z * place.z), -slope.y / proj.z);
	return ahead && all(uv >= 0.0) && all(uv <= 1.0) ? art.SampleLevel(linear_clamp, uv, 0) : float4(0.0, 0.0, 0.0, 0.0);
}
// The art's own colour: its dark lines stay dark (the ACOG's crosshair, every scope's), its light marks (a holo's ring, the
// ACOG's chevron: white art) take the sight's colour.
float4 ps(float4 position : SV_Position) : SV_Target
{
	bool ahead;
	float4 a = art_at(position, ahead);
	if (a.a <= 0.0)
		discard;
	return float4(a.rgb * colour.rgb * a.a * proj.w, a.a);
}
float4 mask(float4 position : SV_Position) : SV_Target
{
	bool ahead;
	float mark = art_at(position, ahead).a;
	if (up.w > 0.5) // a lens draw without the reticle art (the glass of a scope)
		mark = 0.0;
	if (!ahead)
		discard;
	return float4(exp2(colour.w) / 255.0, 0.0, 0.0, mark); // r: this quad's bit (blend add), a: the reticle (blend max)
}
// The quad's pixels within 3 of the sight line's point (counted by an occlusion query; writes nothing).
float4 probe(float4 position : SV_Position) : SV_Target
{
	if (along.z >= 0.0)
		discard;
	float2 at = float2(along.x / -along.z * proj.x, along.y / -along.z * proj.y);
	float2 pixel = float2(view.x + (at.x * 0.5 + 0.5) * view.z, view.y + (0.5 - at.y * 0.5) * view.w);
	if (length(position.xy - pixel) > 3.0)
		discard;
	return float4(0.0, 0.0, 0.0, 0.0);
}
)";

		// The optic's lens quad blends ONE / INV_SRC_ALPHA (every optic, #137 S1); the two additive 6-index quads
		// every gun also draws (ONE / ONE) are not a lens and get no glass.
		bool lens_quad_blend(ID3D11DeviceContext* c)
		{
			ComPtr<ID3D11BlendState> blend;
			float factor[4]{};
			UINT mask = 0;
			c->OMGetBlendState(&blend, factor, &mask);
			if (!blend)
			{
				return false;
			}
			D3D11_BLEND_DESC desc{};
			blend->GetDesc(&desc);
			return desc.RenderTarget[0].BlendEnable && desc.RenderTarget[0].SrcBlend == D3D11_BLEND_ONE && desc.RenderTarget[0].DestBlend == D3D11_BLEND_INV_SRC_ALPHA;
		}

		// #139: another lens draw of the optic than the reticle quad (S1: the ACOG's 30-index and the SRS's 24-index glass):
		// stencil-tested against ref 2, no depth write, the lens blend; cheap checks first (every small indexed draw).
		bool glass_lens_draw(ID3D11DeviceContext* c)
		{
			if (!scope_glass_on.load(std::memory_order_relaxed) || optic_power_now() < glass_min_power)
			{
				return false;
			}
			ComPtr<ID3D11DepthStencilState> state;
			UINT reference = 0;
			c->OMGetDepthStencilState(&state, &reference);
			if (!state || reference != reticle_stencil)
			{
				return false;
			}
			D3D11_DEPTH_STENCIL_DESC desc{};
			state->GetDesc(&desc);
			return desc.StencilEnable && desc.DepthWriteMask == D3D11_DEPTH_WRITE_MASK_ZERO && lens_quad_blend(c);
		}

		// The mask target, the size of the bound target 0 (the game's depth buffer matches it).
		bool glass_mask_target(ID3D11DeviceContext* c)
		{
			ComPtr<ID3D11RenderTargetView> target;
			c->OMGetRenderTargets(1, &target, nullptr);
			if (!target)
			{
				return false;
			}
			ComPtr<ID3D11Resource> resource;
			target->GetResource(&resource);
			ComPtr<ID3D11Texture2D> texture;
			if (!resource || FAILED(resource.As(&texture)))
			{
				return false;
			}
			D3D11_TEXTURE2D_DESC desc{};
			texture->GetDesc(&desc);
			if (desc.SampleDesc.Count != 1)
			{
				return false;
			}
			D3D11_TEXTURE2D_DESC have{};
			if (lens_reticle.mask)
			{
				lens_reticle.mask->GetDesc(&have);
			}
			if (lens_reticle.mask && have.Width == desc.Width && have.Height == desc.Height)
			{
				return true;
			}
			ComPtr<ID3D11Device> device;
			c->GetDevice(&device);
			D3D11_TEXTURE2D_DESC mask{};
			mask.Width = desc.Width;
			mask.Height = desc.Height;
			mask.MipLevels = 1;
			mask.ArraySize = 1;
			mask.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
			mask.SampleDesc.Count = 1;
			mask.Usage = D3D11_USAGE_DEFAULT;
			mask.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
			lens_reticle.mask.Reset();
			lens_reticle.mask_target.Reset();
			lens_reticle.mask_view.Reset();
			if (!device || FAILED(device->CreateTexture2D(&mask, nullptr, &lens_reticle.mask))
				|| FAILED(device->CreateRenderTargetView(lens_reticle.mask.Get(), nullptr, &lens_reticle.mask_target))
				|| FAILED(device->CreateShaderResourceView(lens_reticle.mask.Get(), nullptr, &lens_reticle.mask_view)))
			{
				log::write("eye pass: glass mask %ux%u failed", desc.Width, desc.Height);
				lens_reticle.mask.Reset();
				lens_reticle.mask_target.Reset();
				lens_reticle.mask_view.Reset();
				return false;
			}
			const float clear[4]{};
			c->ClearRenderTargetView(lens_reticle.mask_target.Get(), clear);
			log::write("eye pass: glass mask %ux%u", desc.Width, desc.Height);
			return true;
		}

		// The lens quad once more, into the mask: the game's vertex shader, geometry, depth and stencil, our mask PS.
		void draw_glass_mask(ID3D11DeviceContext* c, UINT count, UINT start, INT base, int quad)
		{
			// Each query is read back when it is issued again (the same quad two eye renders later).
			const auto prepare = [c](ComPtr<ID3D11Query>& query, bool& issued, std::uint64_t& result) {
				if (!query)
				{
					ComPtr<ID3D11Device> device;
					c->GetDevice(&device);
					const D3D11_QUERY_DESC desc{ D3D11_QUERY_OCCLUSION, 0 };
					if (device)
					{
						device->CreateQuery(&desc, &query);
					}
					issued = false;
				}
				UINT64 pixels = 0;
				if (query && issued && c->GetData(query.Get(), &pixels, sizeof(pixels), D3D11_ASYNC_GETDATA_DONOTFLUSH) == S_OK)
				{
					result = pixels;
				}
			};
			ComPtr<ID3D11Query>& query = lens_reticle.area[glass_eye][quad];
			bool& issued = lens_reticle.area_issued[glass_eye][quad];
			ComPtr<ID3D11Query>& line_query = lens_reticle.on_line[glass_eye][quad];
			bool& line_issued = lens_reticle.on_line_issued[glass_eye][quad];
			prepare(query, issued, glass_area[glass_eye][quad]);
			prepare(line_query, line_issued, glass_on_line[glass_eye][quad]);
			ComPtr<ID3D11RenderTargetView> old_targets[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT];
			ComPtr<ID3D11DepthStencilView> old_depth_view;
			{
				ID3D11RenderTargetView* targets[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT]{};
				ID3D11DepthStencilView* depth_view = nullptr;
				c->OMGetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, targets, &depth_view);
				for (UINT i = 0; i < D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT; ++i)
				{
					old_targets[i].Attach(targets[i]);
				}
				old_depth_view.Attach(depth_view);
			}
			ComPtr<ID3D11BlendState> blend;
			float factor[4]{};
			UINT sample_mask = 0;
			c->OMGetBlendState(&blend, factor, &sample_mask);
			ID3D11RenderTargetView* mask = lens_reticle.mask_target.Get();
			c->OMSetRenderTargets(1, &mask, old_depth_view.Get());
			c->OMSetBlendState(lens_reticle.mask_blend.Get(), nullptr, 0xffffffff);
			c->PSSetShader(lens_reticle.mask_ps.Get(), nullptr, 0);
			if (query)
			{
				c->Begin(query.Get());
			}
			original_draw_indexed(c, count, start, base);
			if (query)
			{
				c->End(query.Get());
				issued = true;
			}
			if (line_query)
			{
				c->OMSetBlendState(lens_reticle.probe_blend.Get(), nullptr, 0xffffffff);
				c->PSSetShader(lens_reticle.probe_ps.Get(), nullptr, 0);
				c->Begin(line_query.Get());
				original_draw_indexed(c, count, start, base);
				c->End(line_query.Get());
				line_issued = true;
			}
			ID3D11RenderTargetView* targets[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT]{};
			for (UINT i = 0; i < D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT; ++i)
			{
				targets[i] = old_targets[i].Get();
			}
			c->OMSetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, targets, old_depth_view.Get());
			c->OMSetBlendState(blend.Get(), factor, sample_mask);
		}

		// art: the game's 6-index reticle quad (our reticle lens draws it); else another lens draw of the optic (a scope's
		// glass: drawn as the game draws it, and only marked into the glass mask; false when it is not wanted there).
		bool draw_reticle_lens(ID3D11DeviceContext* c, UINT count, UINT start, INT base, bool art = true)
		{
			if (!art && !(scope_glass_on.load(std::memory_order_relaxed) && optic_power_now() >= glass_min_power && glass_quads < glass_max_quads))
			{
				return false;
			}
			float along[3]{}, up[3]{};
			{
				const std::lock_guard guard(lens_axes_lock);
				if (!lens_axes_valid)
				{
					return false;
				}
				std::memcpy(along, lens_along, sizeof(along));
				std::memcpy(up, lens_up, sizeof(up));
			}
			if (projection_sx <= 0.0f || projection_sy <= 0.0f)
			{
				return false;
			}
			ComPtr<ID3D11Device> device;
			c->GetDevice(&device);
			if (lens_reticle.failed || !device)
			{
				return false;
			}
			if (lens_reticle.device.Get() != device.Get() || !lens_reticle.ps)
			{
				lens_reticle = {};
				lens_reticle.device = device;
				ComPtr<ID3DBlob> code, mask_code, probe_code, messages;
				D3D11_BLEND_DESC probe_blend{};
				probe_blend.RenderTarget[0].RenderTargetWriteMask = 0;
				D3D11_BLEND_DESC mask_blend{};
				mask_blend.RenderTarget[0].BlendEnable = TRUE;
				mask_blend.RenderTarget[0].SrcBlend = D3D11_BLEND_ONE;
				mask_blend.RenderTarget[0].DestBlend = D3D11_BLEND_ONE;
				mask_blend.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
				mask_blend.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
				mask_blend.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ONE;
				mask_blend.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_MAX;
				mask_blend.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_RED | D3D11_COLOR_WRITE_ENABLE_ALPHA;
				D3D11_BUFFER_DESC buffer{};
				buffer.ByteWidth = 16 * 16;
				buffer.Usage = D3D11_USAGE_DEFAULT;
				buffer.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
				D3D11_SAMPLER_DESC sampler{};
				sampler.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
				sampler.AddressU = sampler.AddressV = sampler.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
				sampler.MaxLOD = D3D11_FLOAT32_MAX;
				if (FAILED(D3DCompile(lens_reticle_shader, sizeof(lens_reticle_shader) - 1, "lens_reticle", nullptr, nullptr, "ps", "ps_5_0",
						D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &code, &messages))
					|| FAILED(device->CreatePixelShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr, &lens_reticle.ps))
					|| FAILED(D3DCompile(lens_reticle_shader, sizeof(lens_reticle_shader) - 1, "lens_reticle", nullptr, nullptr, "mask", "ps_5_0",
						D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &mask_code, &messages))
					|| FAILED(device->CreatePixelShader(mask_code->GetBufferPointer(), mask_code->GetBufferSize(), nullptr, &lens_reticle.mask_ps))
					|| FAILED(device->CreateBlendState(&mask_blend, &lens_reticle.mask_blend))
					|| FAILED(D3DCompile(lens_reticle_shader, sizeof(lens_reticle_shader) - 1, "lens_reticle", nullptr, nullptr, "probe", "ps_5_0",
						D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &probe_code, &messages))
					|| FAILED(device->CreatePixelShader(probe_code->GetBufferPointer(), probe_code->GetBufferSize(), nullptr, &lens_reticle.probe_ps))
					|| FAILED(device->CreateBlendState(&probe_blend, &lens_reticle.probe_blend))
					|| FAILED(device->CreateBuffer(&buffer, nullptr, &lens_reticle.constants))
					|| FAILED(device->CreateSamplerState(&sampler, &lens_reticle.sampler)))
				{
					log::write("eye pass: lens reticle shader failed: %s", messages ? static_cast<const char*>(messages->GetBufferPointer()) : "");
					lens_reticle.failed = true;
					return false;
				}
			}
			ComPtr<ID3D11DeviceContext1> c1;
			if (FAILED(c->QueryInterface(IID_PPV_ARGS(&c1))))
			{
				return false;
			}
			// The art as the material has it (its height as a fraction of the screen height, aspect, the point on the sight
			// line), from its ps cb1.
			reticle_material material_art;
			{
				ComPtr<ID3D11Buffer> material;
				UINT first = 0, number = 0;
				c1->PSGetConstantBuffers1(1, 1, &material, &first, &number);
				float block[20]{};
				if (material && read_arena_block(material.Get(), first, 5, block))
				{
					material_art = read_reticle_material(block);
					if (art && glass_eye == 0 && glass_debug.load(std::memory_order_relaxed) != 0) // research only (pipe glassdebug)
					{
						note_material_seen(c, c1.Get(), block);
					}
				}
			}
			const float size = material_art.size;
			if (art)
			{
				lens_size_seen = size;
			}
			// A scope's own reticle (no material: the quad of its model). Seen only through its glass, so it is kept for the
			// glass and not drawn on the lens quads (where it would sit at a made-up size on the scope's front).
			if (art && !material_art.known && scope_glass_on.load(std::memory_order_relaxed))
			{
				const float scope_power = optic_power_now();
				reticle_material scope_art;
				if (scope_power >= glass_min_power && scope_art_for_draw(c, count, start, base, scope_art))
				{
					const float level_fov = optic_level_fov_now();
					note_glass_art(c, scope_art, glass_reticle_span(scope_art_span(scope_art.aspect, level_fov > 0.5f ? level_fov : 10.0f), scope_power));
					++scope_arts_noted;
					return true;
				}
			}
			D3D11_VIEWPORT viewport{};
			UINT viewports = 1;
			c->RSGetViewports(&viewports, &viewport);
			if (viewports != 1 || viewport.Width < 16.0f)
			{
				return false;
			}
			float r[3]{}, u[3]{};
			if (!lens_axes(along, up, r, u))
			{
				return false;
			}
			// #139: a magnifying level with a fresh scope picture: this quad only marks the glass (draw_glass_mask).
			const float power = optic_power_now();
			glass_power_seen = power;
			bool glass = scope_glass_on.load(std::memory_order_relaxed) && power >= glass_min_power && lens_quad_blend(c);
			if (glass)
			{
				const scope_image picture = latest_scope_image();
				if (!picture.texture || GetTickCount64() - picture.at > 200 || !glass_mask_target(c))
				{
					glass = false;
					++glass_no_picture;
				}
			}
			if (glass && glass_quads >= glass_max_quads)
			{
				glass = false;
			}
			if (!art && !glass)
			{
				return false;
			}
			if (!art)
			{
				original_draw_indexed(c, count, start, base); // the game's own glass, as it draws it
			}
			// #140: the art sized for the selected zoom level's own view (the setting overrides).
			const float span = reticle_span(size, reticle_fov(sight_lens_fov.load(std::memory_order_relaxed), optic_level_fov_now()));
			if (art)
			{
				lens_span_seen = span;
			}
			float constants[16][4]{
				{ viewport.TopLeftX, viewport.TopLeftY, viewport.Width, viewport.Height },
				{ projection_sx, projection_sy, span, sight_lens_brightness.load(std::memory_order_relaxed) },
				{ r[0], r[1], r[2], 0.0f },
				{ u[0], u[1], u[2], art ? 0.0f : 1.0f }, // w: 1 = no reticle art in this draw
				{ along[0], along[1], along[2], 0.0f },
				{ 1.0f, 0.12f, 0.06f, glass ? static_cast<float>(glass_quads) : 1.0f }, // w: the mask bit of this quad
				{ material_art.anchor[0], material_art.anchor[1], material_art.aspect, 0.0f },
			};
			// The optic's own reticle art, for the glass's picture (draw_lens_picture samples it by the same slope).
			if (art && material_art.known && power >= glass_min_power)
			{
				note_glass_art(c, material_art, glass_reticle_span(span, power));
			}
			c->UpdateSubresource(lens_reticle.constants.Get(), 0, nullptr, constants, 0, 0);
			ComPtr<ID3D11PixelShader> old_ps;
			c->PSGetShader(&old_ps, nullptr, nullptr);
			ComPtr<ID3D11Buffer> old_cb;
			UINT old_first = 0, old_count = 0;
			c1->PSGetConstantBuffers1(13, 1, &old_cb, &old_first, &old_count);
			ComPtr<ID3D11SamplerState> old_sampler;
			c->PSGetSamplers(15, 1, &old_sampler);
			ID3D11Buffer* own = lens_reticle.constants.Get();
			const UINT zero = 0, sixteen = 16;
			original_ps_set_constant_buffers1(c, 13, 1, &own, &zero, &sixteen);
			ID3D11SamplerState* linear = lens_reticle.sampler.Get();
			c->PSSetSamplers(15, 1, &linear);
			// The reticle lens as before; when this quad may be the glass, also into the mask (the composed glass then
			// covers this reticle with its own; an optic whose lens quads are off its line keeps this one).
			if (art)
			{
				c->PSSetShader(lens_reticle.ps.Get(), nullptr, 0);
				original_draw_indexed(c, count, start, base);
				++lens_reticles;
			}
			if (glass)
			{
				// Through the glass the art is magnified with the world (glass_reticle_span).
				constants[1][2] = glass_reticle_span(span, power);
				c->UpdateSubresource(lens_reticle.constants.Get(), 0, nullptr, constants, 0, 0);
				draw_glass_mask(c, count, start, base, glass_quads);
				++glass_quads;
				++glass_masks;
			}
			c->PSSetShader(old_ps.Get(), nullptr, 0);
			ID3D11Buffer* back = old_cb.Get();
			if (old_count)
			{
				original_ps_set_constant_buffers1(c, 13, 1, &back, &old_first, &old_count);
			}
			else
			{
				original_ps_set_constant_buffers1(c, 13, 1, &back, nullptr, nullptr);
			}
			ID3D11SamplerState* back_sampler = old_sampler.Get();
			c->PSSetSamplers(15, 1, &back_sampler);
			return true;
		}

		// Research (pipe "reticle grab <folder>", #39 BF3-style lens): at the next first-eye reticle quad, its pixel
		// shader's textures rendered out as RGBA8 (<folder>\srvN_WxH.rgba), and an info file with the textures'
		// formats, the VS and PS constant buffer bindings with their first 64 floats, and the quad's indices and
		// vertices.
		std::atomic<int> grab_state{}; // 1 armed, 2 done, 3 failed
		// "reticle grab <folder> <count> <start> <dest blend>": instead, the eye render's lens-state draw (stencil reference 2,
		// no depth write, blended) with that index count, start and destination blend (2 ONE, 6 INV_SRC_ALPHA).
		std::atomic<UINT> grab_count{};
		std::atomic<UINT> grab_start{};
		std::atomic<UINT> grab_dest{};
		std::atomic<UINT> grab_nth{}; // the nth such draw of the first eye's render (0 the first)
		UINT grab_matches{};          // render thread: such draws so far in this first-eye render

		bool grab_selected(ID3D11DeviceContext* c, UINT count, UINT start)
		{
			if (grab_count.load(std::memory_order_relaxed) != count || grab_start.load(std::memory_order_relaxed) != start || glass_eye != 0)
			{
				return false;
			}
			ComPtr<ID3D11DepthStencilState> state;
			UINT reference = 0;
			c->OMGetDepthStencilState(&state, &reference);
			ComPtr<ID3D11BlendState> blend;
			float factor[4]{};
			UINT sample_mask = 0;
			c->OMGetBlendState(&blend, factor, &sample_mask);
			if (!state || !blend || reference != reticle_stencil)
			{
				return false;
			}
			D3D11_DEPTH_STENCIL_DESC desc{};
			state->GetDesc(&desc);
			D3D11_BLEND_DESC blending{};
			blend->GetDesc(&blending);
			if (desc.DepthWriteMask != D3D11_DEPTH_WRITE_MASK_ZERO || !blending.RenderTarget[0].BlendEnable
				|| static_cast<UINT>(blending.RenderTarget[0].DestBlend) != grab_dest.load(std::memory_order_relaxed))
			{
				return false;
			}
			return grab_matches++ == grab_nth.load(std::memory_order_relaxed);
		}
		std::string grab_folder;
		std::string grab_result;
		const char grab_shader[] = R"(
Texture2D picture : register(t0);
float4 vs(uint id : SV_VertexID) : SV_Position
{
	float2 p = float2((id << 1) & 2, id & 2);
	return float4(p * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
}
float4 ps(float4 position : SV_Position) : SV_Target { return picture.Load(int3(position.xy, 0)); }
)";

		bool copy_buffer_bytes(ID3D11DeviceContext* c, ID3D11Buffer* buffer, UINT offset, UINT bytes, std::vector<std::uint8_t>& out)
		{
			out.clear();
			if (!buffer || bytes == 0)
			{
				return false;
			}
			D3D11_BUFFER_DESC desc{};
			buffer->GetDesc(&desc);
			if (offset >= desc.ByteWidth)
			{
				return false;
			}
			bytes = std::min(bytes, desc.ByteWidth - offset);
			ComPtr<ID3D11Device> device;
			c->GetDevice(&device);
			D3D11_BUFFER_DESC staging{};
			staging.ByteWidth = (bytes + 15) & ~15u;
			staging.Usage = D3D11_USAGE_STAGING;
			staging.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
			ComPtr<ID3D11Buffer> copy;
			if (FAILED(device->CreateBuffer(&staging, nullptr, &copy)))
			{
				return false;
			}
			const D3D11_BOX box{ offset, 0, 0, offset + bytes, 1, 1 };
			c->CopySubresourceRegion(copy.Get(), 0, 0, 0, 0, buffer, 0, &box);
			D3D11_MAPPED_SUBRESOURCE mapped{};
			if (FAILED(original_map(c, copy.Get(), 0, D3D11_MAP_READ, 0, &mapped)))
			{
				return false;
			}
			out.assign(static_cast<const std::uint8_t*>(mapped.pData), static_cast<const std::uint8_t*>(mapped.pData) + bytes);
			c->Unmap(copy.Get(), 0);
			return true;
		}

		// One shader resource rendered into an RGBA8 target of its size and written raw. The state it changes is
		// put back by the caller's grab.
		bool save_resource_view(ID3D11DeviceContext* c, ID3D11ShaderResourceView* view, const std::string& prefix, std::string& note)
		{
			D3D11_SHADER_RESOURCE_VIEW_DESC desc{};
			view->GetDesc(&desc);
			ComPtr<ID3D11Resource> resource;
			view->GetResource(&resource);
			ComPtr<ID3D11Texture2D> texture;
			char text[256];
			if (desc.ViewDimension != D3D11_SRV_DIMENSION_TEXTURE2D || FAILED(resource.As(&texture)))
			{
				std::snprintf(text, sizeof(text), "view dimension %d format %d (not saved)", static_cast<int>(desc.ViewDimension), static_cast<int>(desc.Format));
				note = text;
				return false;
			}
			D3D11_TEXTURE2D_DESC t{};
			texture->GetDesc(&t);
			const UINT w = std::max(1u, t.Width >> desc.Texture2D.MostDetailedMip), h = std::max(1u, t.Height >> desc.Texture2D.MostDetailedMip);
			std::snprintf(text, sizeof(text), "texture %ux%u mips %u format %d view format %d most detailed mip %u", t.Width, t.Height, t.MipLevels,
				static_cast<int>(t.Format), static_cast<int>(desc.Format), desc.Texture2D.MostDetailedMip);
			note = text;
			if (w > 4096 || h > 4096)
			{
				return false;
			}
			ComPtr<ID3D11Device> device;
			c->GetDevice(&device);
			static ComPtr<ID3D11VertexShader> vs;
			static ComPtr<ID3D11PixelShader> ps;
			if (!ps)
			{
				ComPtr<ID3DBlob> vs_code, ps_code, messages;
				if (FAILED(D3DCompile(grab_shader, sizeof(grab_shader) - 1, "grab", nullptr, nullptr, "vs", "vs_5_0", 0, 0, &vs_code, &messages))
					|| FAILED(D3DCompile(grab_shader, sizeof(grab_shader) - 1, "grab", nullptr, nullptr, "ps", "ps_5_0", 0, 0, &ps_code, &messages))
					|| FAILED(device->CreateVertexShader(vs_code->GetBufferPointer(), vs_code->GetBufferSize(), nullptr, &vs))
					|| FAILED(device->CreatePixelShader(ps_code->GetBufferPointer(), ps_code->GetBufferSize(), nullptr, &ps)))
				{
					return false;
				}
			}
			D3D11_TEXTURE2D_DESC target_desc{};
			target_desc.Width = w;
			target_desc.Height = h;
			target_desc.MipLevels = 1;
			target_desc.ArraySize = 1;
			target_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
			target_desc.SampleDesc.Count = 1;
			target_desc.Usage = D3D11_USAGE_DEFAULT;
			target_desc.BindFlags = D3D11_BIND_RENDER_TARGET;
			ComPtr<ID3D11Texture2D> target;
			ComPtr<ID3D11RenderTargetView> target_view;
			D3D11_TEXTURE2D_DESC staging_desc = target_desc;
			staging_desc.Usage = D3D11_USAGE_STAGING;
			staging_desc.BindFlags = 0;
			staging_desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
			ComPtr<ID3D11Texture2D> staging;
			if (FAILED(device->CreateTexture2D(&target_desc, nullptr, &target)) || FAILED(device->CreateRenderTargetView(target.Get(), nullptr, &target_view))
				|| FAILED(device->CreateTexture2D(&staging_desc, nullptr, &staging)))
			{
				return false;
			}
			ID3D11RenderTargetView* rtv = target_view.Get();
			c->OMSetRenderTargets(1, &rtv, nullptr);
			const D3D11_VIEWPORT viewport{ 0.0f, 0.0f, static_cast<float>(w), static_cast<float>(h), 0.0f, 1.0f };
			c->RSSetViewports(1, &viewport);
			c->RSSetState(nullptr);
			c->OMSetBlendState(nullptr, nullptr, 0xffffffff);
			c->OMSetDepthStencilState(nullptr, 0);
			c->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
			c->IASetInputLayout(nullptr);
			c->VSSetShader(vs.Get(), nullptr, 0);
			c->PSSetShader(ps.Get(), nullptr, 0);
			c->PSSetShaderResources(0, 1, &view);
			original_draw(c, 3, 0);
			c->CopyResource(staging.Get(), target.Get());
			D3D11_MAPPED_SUBRESOURCE mapped{};
			if (FAILED(original_map(c, staging.Get(), 0, D3D11_MAP_READ, 0, &mapped)))
			{
				return false;
			}
			char name[64];
			std::snprintf(name, sizeof(name), "_%ux%u.rgba", w, h);
			std::ofstream file(prefix + name, std::ios::binary);
			for (UINT row = 0; row < h; ++row)
			{
				file.write(static_cast<const char*>(mapped.pData) + static_cast<std::size_t>(row) * mapped.RowPitch, static_cast<std::streamsize>(w) * 4);
			}
			c->Unmap(staging.Get(), 0);
			return static_cast<bool>(file);
		}

		void grab_reticle(ID3D11DeviceContext* c, UINT count, UINT start, INT base)
		{
			ComPtr<ID3D11DeviceContext1> c1;
			if (FAILED(c->QueryInterface(IID_PPV_ARGS(&c1))))
			{
				grab_state = 3;
				return;
			}
			std::string info;
			char line[512];
			// Constant buffers first (nothing is changed yet).
			for (int stage = 0; stage < 2; ++stage)
			{
				ID3D11Buffer* buffers[8]{};
				UINT first[8]{}, number[8]{};
				if (stage == 0)
				{
					c1->VSGetConstantBuffers1(0, 8, buffers, first, number);
				}
				else
				{
					c1->PSGetConstantBuffers1(0, 8, buffers, first, number);
				}
				for (UINT slot = 0; slot < 8; ++slot)
				{
					if (!buffers[slot])
					{
						continue;
					}
					D3D11_BUFFER_DESC bd{};
					buffers[slot]->GetDesc(&bd);
					std::snprintf(line, sizeof(line), "%s cb%u buffer %p bytes %u first %u count %u\n", stage == 0 ? "vs" : "ps", slot, static_cast<void*>(buffers[slot]),
						bd.ByteWidth, first[slot], number[slot]);
					info += line;
					std::vector<std::uint8_t> bytes;
					if (copy_buffer_bytes(c, buffers[slot], first[slot] * 16, 64 * 4, bytes))
					{
						info += "   ";
						for (std::size_t i = 0; i + 4 <= bytes.size(); i += 4)
						{
							float f{};
							std::memcpy(&f, bytes.data() + i, 4);
							std::snprintf(line, sizeof(line), "%s%.5g", i ? " " : "", f);
							info += line;
						}
						info += "\n";
					}
					buffers[slot]->Release();
				}
			}
			// The quad's indices and vertices.
			{
				ComPtr<ID3D11Buffer> index_buffer;
				DXGI_FORMAT format{};
				UINT index_offset = 0;
				c->IAGetIndexBuffer(&index_buffer, &format, &index_offset);
				ID3D11Buffer* vertex_buffer = nullptr;
				UINT stride = 0, vertex_offset = 0;
				c->IAGetVertexBuffers(0, 1, &vertex_buffer, &stride, &vertex_offset);
				const UINT index_size = format == DXGI_FORMAT_R32_UINT ? 4 : 2;
				std::vector<std::uint8_t> indices, vertices;
				if (copy_buffer_bytes(c, index_buffer.Get(), index_offset + start * index_size, count * index_size, indices))
				{
					UINT highest = 0;
					info += "indices";
					for (UINT i = 0; i < count; ++i)
					{
						UINT value = 0;
						std::memcpy(&value, indices.data() + i * index_size, index_size);
						highest = std::max(highest, value);
						std::snprintf(line, sizeof(line), " %u", value);
						info += line;
					}
					std::snprintf(line, sizeof(line), " (base %d, stride %u)\n", base, stride);
					info += line;
					if (vertex_buffer && stride && copy_buffer_bytes(c, vertex_buffer, vertex_offset + static_cast<UINT>(base) * stride, (highest + 1) * stride, vertices))
					{
						for (UINT v = 0; v * stride < vertices.size(); ++v)
						{
							std::snprintf(line, sizeof(line), "vertex %u:", v);
							info += line;
							for (UINT i = 0; i + 4 <= stride; i += 4)
							{
								float f{};
								std::memcpy(&f, vertices.data() + v * stride + i, 4);
								std::snprintf(line, sizeof(line), " %.5g", f);
								info += line;
							}
							info += " | hex";
							for (UINT i = 0; i < stride; ++i)
							{
								std::snprintf(line, sizeof(line), "%s%02x", i % 4 ? "" : " ", vertices[v * stride + i]);
								info += line;
							}
							info += "\n";
						}
					}
				}
				if (vertex_buffer)
				{
					vertex_buffer->Release();
				}
			}
			// The textures, with the state put back afterwards.
			ComPtr<ID3D11RenderTargetView> old_targets[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT];
			ComPtr<ID3D11DepthStencilView> old_depth;
			{
				ID3D11RenderTargetView* targets[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT]{};
				ID3D11DepthStencilView* depth_view = nullptr;
				c->OMGetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, targets, &depth_view);
				for (UINT i = 0; i < D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT; ++i)
				{
					old_targets[i].Attach(targets[i]);
				}
				old_depth.Attach(depth_view);
			}
			D3D11_VIEWPORT old_viewports[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE]{};
			UINT viewport_count = D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;
			c->RSGetViewports(&viewport_count, old_viewports);
			ComPtr<ID3D11RasterizerState> raster;
			c->RSGetState(&raster);
			ComPtr<ID3D11BlendState> blend;
			float factor[4]{};
			UINT sample_mask = 0;
			c->OMGetBlendState(&blend, factor, &sample_mask);
			ComPtr<ID3D11DepthStencilState> depth;
			UINT reference = 0;
			c->OMGetDepthStencilState(&depth, &reference);
			D3D11_PRIMITIVE_TOPOLOGY topology{};
			c->IAGetPrimitiveTopology(&topology);
			ComPtr<ID3D11InputLayout> layout;
			c->IAGetInputLayout(&layout);
			ComPtr<ID3D11VertexShader> vs;
			ComPtr<ID3D11PixelShader> ps;
			c->VSGetShader(&vs, nullptr, nullptr);
			c->PSGetShader(&ps, nullptr, nullptr);
			ID3D11ShaderResourceView* views[8]{};
			c->PSGetShaderResources(0, 8, views);
			for (UINT slot = 0; slot < 8; ++slot)
			{
				if (!views[slot])
				{
					continue;
				}
				std::string note;
				const bool saved = save_resource_view(c, views[slot], grab_folder + "\\srv" + std::to_string(slot), note);
				std::snprintf(line, sizeof(line), "ps t%u %p: %s%s\n", slot, static_cast<void*>(views[slot]), note.c_str(), saved ? " (saved)" : "");
				info += line;
			}
			c->PSSetShaderResources(0, 8, views);
			for (ID3D11ShaderResourceView* v : views)
			{
				if (v)
				{
					v->Release();
				}
			}
			{
				ID3D11RenderTargetView* targets[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT]{};
				for (UINT i = 0; i < D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT; ++i)
				{
					targets[i] = old_targets[i].Get();
				}
				c->OMSetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, targets, old_depth.Get());
			}
			c->RSSetViewports(viewport_count, old_viewports);
			c->RSSetState(raster.Get());
			c->OMSetBlendState(blend.Get(), factor, sample_mask);
			c->OMSetDepthStencilState(depth.Get(), reference);
			c->IASetPrimitiveTopology(topology);
			c->IASetInputLayout(layout.Get());
			c->VSSetShader(vs.Get(), nullptr, 0);
			c->PSSetShader(ps.Get(), nullptr, 0);
			std::ofstream file(grab_folder + "\\reticle.txt");
			file << info;
			grab_result = "reticle grab: written to " + grab_folder;
			grab_state = 2;
		}

		// A draw marker in the probe (stage 'D', slot = index count) ties bindings to draws.
		void STDMETHODCALLTYPE draw_indexed_detour(ID3D11DeviceContext* c, UINT count, UINT start, INT base)
		{
			if (skip_draw(c))
			{
				return;
			}
			if (probing.load(std::memory_order_relaxed) && probe_bindings.size() < 20000)
			{
				probe_bindings.push_back({ 'D', count, nullptr, 0, 0 });
			}
			dump_draw_state(c, count, start);
			if (muzzle_quads::draw(c, count, start, base, original_draw_indexed))
			{
				extra_mesh::after_draw(c, count, start, base, 1);
				return;
			}
			if (grab_state.load(std::memory_order_relaxed) == 1 && in_eye_render && !scope_rendering && !patching && grab_selected(c, count, start))
			{
				grab_reticle(c, count, start, base);
			}
			if (reticle_draw(c, count))
			{
				++reticle_draws;
				if (!patching && grab_count.load(std::memory_order_relaxed) == 0 && grab_state.load(std::memory_order_relaxed) == 1)
				{
					grab_reticle(c, count, start, base);
				}
				if (sight_lens_on.load(std::memory_order_relaxed) && draw_reticle_lens(c, count, start, base))
				{
					extra_mesh::after_draw(c, count, start, base, 1);
					return;
				}
				const bool forced = reticle_forced.load(std::memory_order_relaxed) != 0;
				const float tx = forced ? reticle_forced_tan[0].load(std::memory_order_relaxed) : reticle_tan[0].load(std::memory_order_relaxed);
				const float ty = forced ? reticle_forced_tan[1].load(std::memory_order_relaxed) : reticle_tan[1].load(std::memory_order_relaxed);
				if ((forced || reticle_valid.load(std::memory_order_relaxed)) && projection_sx > 0.0f && projection_sy > 0.0f && std::fabs(tx) < 1.5f && std::fabs(ty) < 1.5f
					&& draw_reticle_anchored(c, count, start, base, tx, ty))
				{
					++reticle_moved;
					extra_mesh::after_draw(c, count, start, base, 1);
					return;
				}
			}
			else if (count <= 64 && in_eye_render && !scope_rendering && glass_lens_draw(c) && draw_reticle_lens(c, count, start, base, false))
			{
				extra_mesh::after_draw(c, count, start, base, 1);
				return;
			}
			original_draw_indexed(c, count, start, base);
			extra_mesh::after_draw(c, count, start, base, 1);
		}


		void STDMETHODCALLTYPE draw_indexed_instanced_detour(ID3D11DeviceContext* c, UINT count, UINT instances, UINT start, INT base, UINT first)
		{
			if (skip_draw(c))
			{
				return;
			}
			if (probing.load(std::memory_order_relaxed) && probe_bindings.size() < 20000)
			{
				probe_bindings.push_back({ 'D', count, nullptr, 0, instances });
			}
			muzzle_quads::note_instanced(c, count, instances, first);
			original_draw_indexed_instanced(c, count, instances, start, base, first);
			extra_mesh::after_draw(c, count, start, base, instances);
		}

		void STDMETHODCALLTYPE vs_set_constant_buffers1_detour(ID3D11DeviceContext* c, UINT s, UINT n, ID3D11Buffer* const* b, const UINT* f, const UINT* k)
		{
			record('V', s, n, b, f, k);
			extra_mesh::on_vs_bindings(s, n, b, f, k);
			set_redirected<&original_vs_set_constant_buffers1, 'V'>(c, s, n, b, f, k);
		}

		void STDMETHODCALLTYPE ps_set_constant_buffers1_detour(ID3D11DeviceContext* c, UINT s, UINT n, ID3D11Buffer* const* b, const UINT* f, const UINT* k)
		{
			record('P', s, n, b, f, k);
			extra_mesh::on_ps_bindings(s, n, b, f, k);
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
				{ draw_slot, &draw_detour, reinterpret_cast<void**>(&original_draw) },
				{ draw_instanced_slot, &draw_instanced_detour, reinterpret_cast<void**>(&original_draw_instanced) },
				{ draw_indexed_instanced_indirect_slot, &draw_indexed_instanced_indirect_detour, reinterpret_cast<void**>(&original_draw_indexed_instanced_indirect) },
				{ draw_instanced_indirect_slot, &draw_instanced_indirect_detour, reinterpret_cast<void**>(&original_draw_instanced_indirect) },
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
			extra_mesh::set_entry_points(original_draw_indexed, original_vs_set_constant_buffers1, original_ps_set_constant_buffers1, &read_arena_block);
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
			extra_mesh::begin_render(2);
			original_per_view_render(renderer, slot, context);
			extra_mesh::end_render();
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

		// Fixed foveated rendering for one eye's render, sized to the back buffer.
		void begin_eye_foveation()
		{
			ID3D11Resource* const buffer = back_buffer.load();
			ComPtr<ID3D11Texture2D> texture;
			if (!eye_context || !buffer || FAILED(buffer->QueryInterface(IID_PPV_ARGS(&texture))))
			{
				return;
			}
			D3D11_TEXTURE2D_DESC desc{};
			texture->GetDesc(&desc);
			begin_foveation(eye_context.Get(), desc.Width, desc.Height);
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
				fresh_used = 0;
				fresh_seen.clear();
				eye1_discarded = false;
				if (!fresh_staging)
				{
					ComPtr<ID3D11Device> fresh_device;
					eye_context->GetDevice(&fresh_device);
					D3D11_BUFFER_DESC desc{};
					desc.ByteWidth = fresh_size;
					desc.Usage = D3D11_USAGE_STAGING;
					desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
					fresh_device->CreateBuffer(&desc, nullptr, &fresh_staging);
				}
				ComPtr<ID3D11Device> device;
				eye_context->GetDevice(&device);
				gpu_begin(device.Get(), eye_context.Get());
				gpu_stamp(0);
				begin_eye_foveation();
			}
			extra_mesh::begin_render(0);
			{
				const std::lock_guard guard(plain_lock);
				plain_draws.clear();
			}
			begin_state_dump();
			reset_eye_discards();
			glass_eye = 0;
			grab_matches = 0;
			reset_glass_arts(0);
			in_eye_render = true;
			muzzle_quads::begin_pass();
			original_per_view_render(renderer, slot, context);
			muzzle_quads::end_pass();
			in_eye_render = false;
			end_state_dump();
			{
				const std::lock_guard guard(plain_lock);
				plain_draws_last = plain_draws;
			}
			extra_mesh::end_render();
			if (patch_constants)
			{
				end_foveation(eye_context.Get());
				gpu_stamp(1);
			}
			collecting = false;
			probing = false;
			pair.rendered(0, frame);
			draw_lens_picture(eye_context.Get(), 0);
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
				{
					const std::lock_guard guard(projection_lock);
					std::memcpy(world_projection_copy, centre_views[0] + 0x320 / 4, sizeof(world_projection_copy));
					world_projection_valid = true;
				}
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
			if (patch_constants)
			{
				begin_eye_foveation();
			}
			extra_mesh::begin_render(1);
			reset_eye_discards();
			glass_eye = 1;
			reset_glass_arts(1);
			in_eye_render = true;
			muzzle_quads::begin_pass();
			original_per_view_render(renderer, slot, context);
			muzzle_quads::end_pass();
			in_eye_render = false;
			extra_mesh::end_render();
			if (patch_constants)
			{
				end_foveation(eye_context.Get());
			}
			patching = false;
			if (frame_foreground_y > 0.0f)
			{
				foreground_y_scale = frame_foreground_y;
				foreground_y_at = GetTickCount64();
				frame_foreground_y = 0.0f;
			}
			probing = false;
			draw_lens_picture(eye_context.Get(), 1);
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

	std::string set_fresh_fix(int on)
	{
		if (on == 0 || on == 1)
		{
			fresh_fix = on == 1;
		}
		return "eyefresh fresh_fix=" + std::to_string(fresh_fix.load() ? 1 : 0) + " copies=" + std::to_string(fresh_copies.load())
			+ " overflows=" + std::to_string(fresh_overflows.load());
	}

	std::string set_discard_fix(int on)
	{
		if (on == 0 || on == 1)
		{
			discard_fix = on == 1;
		}
		return "eyefix discard_fix=" + std::to_string(discard_fix.load() ? 1 : 0) + " stale_after_discard=" + std::to_string(stale_after_discard.load());
	}

	std::string set_eye_hide(const std::string& shaders)
	{
		if (shaders == "list")
		{
			const std::lock_guard guard(plain_lock);
			std::string text = "non-indexed draws (pixel shader, vertices, draws): " + std::to_string(plain_draws_last.size());
			for (const plain_draw& d : plain_draws_last)
			{
				char line[64];
				std::snprintf(line, sizeof(line), "\n%016llX %u %u", static_cast<unsigned long long>(d.shader), d.vertices, d.draws);
				text += line;
			}
			return text;
		}
		if (shaders == "plain")
		{
			hide_plain = true;
			return "eyehide: every non-indexed draw";
		}
		if (shaders == "null")
		{
			hide_null = true;
			return "eyehide: every draw without a pixel shader";
		}
		hide_plain = false;
		hide_null = false;
		int given = 0;
		const char* cursor = shaders.c_str();
		while (shaders != "off" && given < hide_shader_count)
		{
			char* end = nullptr;
			const unsigned long long value = std::strtoull(cursor, &end, 16);
			if (end == cursor || value == 0)
			{
				break;
			}
			hide_shaders[given++] = static_cast<std::uintptr_t>(value);
			cursor = end;
		}
		for (int i = given; i < hide_shader_count; ++i)
		{
			hide_shaders[i] = 0;
		}
		return given > 0 ? "eyehide: " + std::to_string(given) + " pixel shaders" : "eyehide off";
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

	void set_reticle_view(bool valid, float tan_x, float tan_y)
	{
		reticle_tan[0] = tan_x;
		reticle_tan[1] = tan_y;
		reticle_valid = valid;
	}

	void set_lens_axes(bool valid, const float (&along)[3], const float (&up)[3])
	{
		const std::lock_guard guard(lens_axes_lock);
		lens_axes_valid = valid;
		std::memcpy(lens_along, along, sizeof(lens_along));
		std::memcpy(lens_up, up, sizeof(lens_up));
	}

	void set_scope_glass(bool on)
	{
		scope_glass_on = on;
	}

	void set_scope_eye_box(float metres)
	{
		scope_eye_box_m = std::isfinite(metres) ? std::fmax(0.005f, std::fmin(0.05f, metres)) : glass_eye_box;
	}

	bool scope_glass_shown()
	{
		return scope_glass_on.load(std::memory_order_relaxed) && GetTickCount64() - glass_shown_at.load(std::memory_order_relaxed) < 300;
	}

	std::string set_glass_debug(int debug_mode)
	{
		glass_debug = debug_mode;
		return debug_mode == 1 ? "glassdebug: lens quads in colour (0 red, 1 green, 2 blue), sight line magenta"
			: debug_mode == 2 ? "glassdebug: the glass black, its reticle pure green" : "glassdebug: off";
	}

	void set_sight_lens(bool on, float ads_fov_degrees, float brightness)
	{
		sight_lens_on = on;
		sight_lens_fov = ads_fov_degrees;
		sight_lens_brightness = brightness;
	}

	void set_lens_eye(const pose& first_eye)
	{
		const std::lock_guard guard(lens_eye_lock);
		lens_eye = first_eye;
		lens_eye_valid = true;
	}

	void set_scope_in_frame(bool on)
	{
		scope_in_frame_on = on;
	}

	bool scope_drawn_in_frame()
	{
		// #145 (headset 2026-11-24): with the glass on the picture is only ever in the optic's own glass (or the virtual
		// eyepiece), in the frame; the theater's quad showed it a second time as a disc 15 cm ahead of the eye, in front of
		// the optic (an OpenXR layer: no eye shot shows it, only the headset and the SteamVR view).
		if (scope_glass_on.load(std::memory_order_relaxed))
		{
			return true;
		}
		static std::uint64_t seen = 0;
		static ULONGLONG changed_at = 0;
		const std::uint64_t now_draws = lens_draws.load(std::memory_order_relaxed);
		const ULONGLONG now = GetTickCount64();
		if (now_draws != seen)
		{
			seen = now_draws;
			changed_at = now;
		}
		return scope_in_frame_on.load(std::memory_order_relaxed) && now - changed_at < 200;
	}

	std::string lens_report()
	{
		char text[1400];
		const std::lock_guard guard(lens_debug_lock);
		std::snprintf(text, sizeof(text),
			"reticle_span=%.4f level_fov=%.1f | scope_glass=%d glass_masks=%llu glass_composed=%llu glass_no_picture=%llu power=%.2f uv_per_tangent=%.3f line_uv=(%.3f %.3f) quads=%d glass_quad=%d areas=%llu/%llu/%llu on_line=%llu/%llu/%llu field=%.3f virtual=%llu projection_offset=(%.4f %.4f) | sight_lens=%d reticles=%llu art_size=%.4f fov=%.1f | scope_in_frame=%d lens_draws=%llu lens_skipped=%llu shift=%.4f extra=(%.4f %.4f) quad=(%.3f %.3f %.3f)\n"
			"  eye1 at (%.3f %.3f %.3f) lens in view (%.3f %.3f %.3f) ndc_x=%.3f\n  eye2 at (%.3f %.3f %.3f) lens in view (%.3f %.3f %.3f) ndc_x=%.3f",
			lens_span_seen.load(), optic_level_fov_now(), scope_glass_on.load() ? 1 : 0, static_cast<unsigned long long>(glass_masks.load()), static_cast<unsigned long long>(glass_composed.load()),
			static_cast<unsigned long long>(glass_no_picture.load()), glass_power_seen.load(), glass_scale_seen.load(), glass_line_uv[0].load(), glass_line_uv[1].load(), glass_quads_seen.load(), glass_pick_seen.load(),
			static_cast<unsigned long long>(glass_area_seen[0].load()), static_cast<unsigned long long>(glass_area_seen[1].load()), static_cast<unsigned long long>(glass_area_seen[2].load()),
			static_cast<unsigned long long>(glass_on_line[0][0] + glass_on_line[1][0]), static_cast<unsigned long long>(glass_on_line[0][1] + glass_on_line[1][1]),
			static_cast<unsigned long long>(glass_on_line[0][2] + glass_on_line[1][2]), glass_field_seen.load(),
			static_cast<unsigned long long>(glass_fallbacks.load()), world_projection_copy[8], world_projection_copy[9],
			sight_lens_on.load() ? 1 : 0, static_cast<unsigned long long>(lens_reticles.load()), lens_size_seen.load(), sight_lens_fov.load(), scope_in_frame_on.load() ? 1 : 0, static_cast<unsigned long long>(lens_draws.load()), static_cast<unsigned long long>(lens_skipped.load()),
			eye_shift_metres.load(), lens_extra_dx[0].load(), lens_extra_dx[1].load(), lens_debug_quad[0], lens_debug_quad[1], lens_debug_quad[2],
			lens_debug_eye[0][0], lens_debug_eye[0][1], lens_debug_eye[0][2], lens_debug_centre[0][0], lens_debug_centre[0][1], lens_debug_centre[0][2],
			lens_debug_centre[0][3], lens_debug_eye[1][0], lens_debug_eye[1][1], lens_debug_eye[1][2], lens_debug_centre[1][0], lens_debug_centre[1][1],
			lens_debug_centre[1][2], lens_debug_centre[1][3]);
		std::string report = text;
		char art_text[96];
		std::snprintf(art_text, sizeof(art_text), "\n  open_glass=%d magnifier=%d scope_arts=%llu quad_reads=%llu glass_arts=%d",
			optic_id_now() != 0 && optic_id_now() == open_glass_seen.load() ? 1 : 0, optic_is_magnifier_now() ? 1 : 0, static_cast<unsigned long long>(scope_arts_noted.load()),
			static_cast<unsigned long long>(scope_quad_reads.load()), glass_arts_seen.load());
		report += art_text;
		for (int k = 0; k < glass_max_arts && k < glass_arts_seen.load(); ++k)
		{
			std::snprintf(art_text, sizeof(art_text), " [span=%.4f aspect=%.2f anchor=(%.3f %.3f)]", glass_art_seen[k][0].load(), glass_art_seen[k][1].load(),
				glass_art_seen[k][2].load(), glass_art_seen[k][3].load());
			report += art_text;
		}
		{
			const std::lock_guard material_guard(material_seen_lock);
			for (int k = 0; k < 4; ++k)
			{
				report += "\n  material";
				report += material_context[k];
			}
		}
		return report;
	}

	std::string set_lens_fix(float dx1, float dx2)
	{
		lens_extra_dx[0] = dx1;
		lens_extra_dx[1] = dx2;
		return lens_report();
	}

	std::string reticle_command(const std::string& argument)
	{
		float tx = 0.0f, ty = 0.0f;
		if (argument.rfind("dump ", 0) == 0)
		{
			// One first-eye render's stencil-tested draws, written to the file.
			state_dump = 1;
			for (int waited = 0; waited < 40 && state_dump.load() != 3; ++waited)
			{
				Sleep(50);
			}
			const bool done = state_dump.exchange(0) == 3;
			const std::lock_guard guard(state_dump_lock);
			FILE* file{};
			if (!done || fopen_s(&file, argument.substr(5).c_str(), "w") != 0 || !file)
			{
				return done ? "reticle dump: cannot write the file" : "reticle dump: no eye render within 2 s";
			}
			for (const std::string& line : state_dump_lines)
			{
				std::fprintf(file, "%s\n", line.c_str());
			}
			std::fclose(file);
			return "reticle dump: " + std::to_string(state_dump_lines.size()) + " stencil-tested draws of " + std::to_string(state_dump_index) + " indexed draws";
		}
		if (argument.rfind("grab ", 0) == 0)
		{
			grab_folder = argument.substr(5);
			unsigned grab_c = 0, grab_s = 0, grab_d = 6, grab_n = 0;
			if (const std::size_t space = grab_folder.find(' '); space != std::string::npos
				&& sscanf_s(grab_folder.c_str() + space, "%u %u %u %u", &grab_c, &grab_s, &grab_d, &grab_n) >= 2)
			{
				grab_folder.resize(space);
			}
			grab_count = grab_c;
			grab_start = grab_s;
			grab_dest = grab_d;
			grab_nth = grab_n;
			CreateDirectoryA(grab_folder.c_str(), nullptr);
			grab_state = 1;
			for (int waited = 0; waited < 60 && grab_state.load() == 1; ++waited)
			{
				Sleep(50);
			}
			const int state = grab_state.exchange(0);
			return state == 2 ? grab_result : state == 3 ? "reticle grab: failed" : "reticle grab: no reticle quad drawn within 3 s (hold a sight to the eye)";
		}
		if (argument == "off")
		{
			reticle_forced = 0;
		}
		else if (sscanf_s(argument.c_str(), "%f %f", &tx, &ty) == 2)
		{
			reticle_forced_tan[0] = tx;
			reticle_forced_tan[1] = ty;
			reticle_forced = 1;
		}
		char text[256];
		std::snprintf(text, sizeof(text), "reticle valid=%d tan=(%.4f %.4f) forced=%d (%.4f %.4f) draws=%llu moved=%llu", reticle_valid.load() ? 1 : 0, reticle_tan[0].load(),
			reticle_tan[1].load(), reticle_forced.load(), reticle_forced_tan[0].load(), reticle_forced_tan[1].load(), reticle_draws.load(), reticle_moved.load());
		return text;
	}

	std::string eye_pass_report()
	{
		char text[1024];
		std::snprintf(text, sizeof(text), "eye_frames=%llu eye_pairs=%llu eye_rejected=%llu eye_abandoned=%llu eye_ms=%.2f eye_readback_ms=%.2f eye_windows=%u "
			"eye_redirected=%llu eye_blocks=%llu eye_overflows=%llu eye_late=%llu eye_late_patched=%llu eye_vp_shifted=%llu eye_view_blocks=%llu "
			"gpu_ms eye1=%.2f eye2=%.2f scope=%.2f scope_renders=%llu scope_hidden=%llu scope_ms=%.2f (worker %llu us, wait %llu us, render %llu us, compose %llu us) "
			"arena_discards eye1=%llu eye2=%llu stale_after_discard=%llu discard_fix=%d fresh=%llu fresh_overflows=%llu fresh_fix=%d",
			frames.load(), pairs.load(), rejected.load(), abandoned.load(), extra_ms.load(), readback_ms.load(), patch_windows.load(), bindings_redirected.load(),
			blocks_patched.load(), eye_buffer_overflows.load(), late_blocks.load(), late_patched.load(), view_projections_shifted.load(), per_view_blocks_shifted.load(),
			gpu_eye_ms[0].load(), gpu_eye_ms[1].load(), gpu_eye_ms[2].load(), scope_renders.load(), scope_hidden_blocks.load(), scope_ms.load(), scope_prepare_us.load(), scope_wait_us.load(), scope_render_us.load(), scope_compose_us.load(),
			arena_discards_eye1.load(), arena_discards_eye2.load(), stale_after_discard.load(), discard_fix.load() ? 1 : 0,
			fresh_copies.load(), fresh_overflows.load(), fresh_fix.load() ? 1 : 0);
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

	bool world_projection(float (&matrix)[16])
	{
		const std::lock_guard guard(projection_lock);
		std::memcpy(matrix, world_projection_copy, sizeof(matrix));
		return world_projection_valid;
	}
}
