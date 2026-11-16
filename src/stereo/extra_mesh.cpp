#include "stereo/extra_mesh.hpp"

#include <wrl/client.h>

#include "stereo/palette_copy.hpp"

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <cstdio>
#include <vector>

namespace bfvr::stereo::extra_mesh
{
	using Microsoft::WRL::ComPtr;

	namespace
	{
		draw_indexed_fn draw_original{};
		set_constant_buffers1_fn vs_bind_original{};
		set_constant_buffers1_fn ps_bind_original{};
		read_block_fn read_block{};

		struct cb_binding
		{
			ID3D11Buffer* buffer{};
			UINT first{};
			UINT count{};
		};
		constexpr UINT tracked_slots = 4;
		cb_binding vs_bound[tracked_slots]; // render thread: the game's VS bindings, slots 0..3
		constexpr UINT tracked_ps_slots = 6;
		cb_binding ps_bound[tracked_ps_slots]; // render thread: the game's PS bindings, slots 0..5
		int render_eye = -1;                // render thread

		constexpr UINT palette_constants = 192; // 3072 bytes: 64 bone matrices of 3x4

		// Research probe (render thread fills, pipe thread waits).
		struct probe_draw
		{
			UINT index{};
			UINT count{};
			UINT start{};
			INT base{};
			UINT instances{};
			ID3D11Buffer* index_buffer{};
			DXGI_FORMAT index_format{};
			UINT index_offset{};
			ID3D11Buffer* vertex_buffer{};
			UINT stride{};
			UINT vertex_offset{};
			ID3D11VertexShader* vs{};
			ID3D11PixelShader* ps{};
			ID3D11Resource* target{};
			UINT target_width{};
			DXGI_FORMAT target_format{};
			cb_binding cb[tracked_slots];
			cb_binding ps_cb[tracked_ps_slots];
			UINT targets{};
			void* depth{};
			void* layout{};
			int topology{};
			UINT vs_views{};
			UINT ps_views{};
			UINT ps_samplers{};
			void* blend{};
			void* depth_state{};
			UINT stencil_ref{};
			void* raster{};
			int palette{ -1 }; // index into probe_palettes
			int blocks{ -1 };  // skinned draws: index into probe_blocks (VS 1..3, PS 0..5, 64 constants each)
		};
		constexpr UINT probe_block_constants = 64;
		constexpr UINT probe_block_slots = (tracked_slots - 1) + tracked_ps_slots;
		std::vector<float> probe_blocks;
		std::atomic<bool> probe_requested{};
		bool probing{}; // render thread
		UINT probe_index{};
		std::vector<probe_draw> probe_draws;
		std::vector<float> probe_palettes;
		std::string probe_path;
		std::string probe_result;
		HANDLE probe_done{};

		void record(ID3D11DeviceContext* context, UINT count, UINT start, INT base, UINT instances)
		{
			probe_draw d;
			d.index = probe_index++;
			d.count = count;
			d.start = start;
			d.base = base;
			d.instances = instances;
			{
				ComPtr<ID3D11Buffer> ib;
				context->IAGetIndexBuffer(&ib, &d.index_format, &d.index_offset);
				d.index_buffer = ib.Get();
				ComPtr<ID3D11Buffer> vb;
				context->IAGetVertexBuffers(0, 1, &vb, &d.stride, &d.vertex_offset);
				d.vertex_buffer = vb.Get();
				ComPtr<ID3D11VertexShader> vs;
				context->VSGetShader(&vs, nullptr, nullptr);
				d.vs = vs.Get();
				ComPtr<ID3D11PixelShader> ps;
				context->PSGetShader(&ps, nullptr, nullptr);
				d.ps = ps.Get();
				ComPtr<ID3D11RenderTargetView> rtv;
				context->OMGetRenderTargets(1, &rtv, nullptr);
				if (rtv)
				{
					ComPtr<ID3D11Resource> resource;
					rtv->GetResource(&resource);
					d.target = resource.Get();
					ComPtr<ID3D11Texture2D> texture;
					if (SUCCEEDED(resource.As(&texture)))
					{
						D3D11_TEXTURE2D_DESC desc{};
						texture->GetDesc(&desc);
						d.target_width = desc.Width;
						d.target_format = desc.Format;
					}
				}
			}
			for (UINT slot = 0; slot < tracked_slots; ++slot)
			{
				d.cb[slot] = vs_bound[slot];
			}
			for (UINT slot = 0; slot < tracked_ps_slots; ++slot)
			{
				d.ps_cb[slot] = ps_bound[slot];
			}
			{
				ID3D11RenderTargetView* rtvs[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT]{};
				ComPtr<ID3D11DepthStencilView> dsv;
				context->OMGetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, rtvs, &dsv);
				for (ID3D11RenderTargetView* view : rtvs)
				{
					d.targets += view ? 1 : 0;
					if (view)
					{
						view->Release();
					}
				}
				d.depth = dsv.Get();
				ComPtr<ID3D11InputLayout> layout;
				context->IAGetInputLayout(&layout);
				d.layout = layout.Get();
				D3D11_PRIMITIVE_TOPOLOGY topology{};
				context->IAGetPrimitiveTopology(&topology);
				d.topology = static_cast<int>(topology);
				ID3D11ShaderResourceView* views[16]{};
				context->VSGetShaderResources(0, 16, views);
				for (ID3D11ShaderResourceView*& view : views)
				{
					d.vs_views |= view ? 1u << (&view - views) : 0u;
					if (view)
					{
						view->Release();
					}
				}
				context->PSGetShaderResources(0, 16, views);
				for (ID3D11ShaderResourceView*& view : views)
				{
					d.ps_views |= view ? 1u << (&view - views) : 0u;
					if (view)
					{
						view->Release();
					}
				}
				ID3D11SamplerState* samplers[16]{};
				context->PSGetSamplers(0, 16, samplers);
				for (ID3D11SamplerState*& sampler : samplers)
				{
					d.ps_samplers |= sampler ? 1u << (&sampler - samplers) : 0u;
					if (sampler)
					{
						sampler->Release();
					}
				}
				ComPtr<ID3D11BlendState> blend;
				FLOAT factor[4]{};
				UINT mask{};
				context->OMGetBlendState(&blend, factor, &mask);
				d.blend = blend.Get();
				ComPtr<ID3D11DepthStencilState> depth_state;
				context->OMGetDepthStencilState(&depth_state, &d.stencil_ref);
				d.depth_state = depth_state.Get();
				ComPtr<ID3D11RasterizerState> raster;
				context->RSGetState(&raster);
				d.raster = raster.Get();
			}
			if (vs_bound[0].buffer && vs_bound[0].count == palette_constants && read_block)
			{
				const std::size_t at = probe_palettes.size();
				probe_palettes.resize(at + palette_constants * 4);
				if (read_block(vs_bound[0].buffer, vs_bound[0].first, palette_constants, probe_palettes.data() + at))
				{
					d.palette = static_cast<int>(at / (palette_constants * 4));
					// The other blocks of skinned draws (zeros where nothing is bound or the read fails).
					const std::size_t blocks_at = probe_blocks.size();
					probe_blocks.resize(blocks_at + probe_block_slots * probe_block_constants * 4, 0.0f);
					float* out = probe_blocks.data() + blocks_at;
					for (UINT slot = 0; slot < probe_block_slots; ++slot)
					{
						const cb_binding& b = slot < tracked_slots - 1 ? vs_bound[slot + 1] : ps_bound[slot - (tracked_slots - 1)];
						if (b.buffer && b.count != 4096)
						{
							read_block(b.buffer, b.first, b.count < probe_block_constants ? b.count : probe_block_constants, out + slot * probe_block_constants * 4);
						}
					}
					d.blocks = static_cast<int>(blocks_at / (probe_block_slots * probe_block_constants * 4));
				}
				else
				{
					probe_palettes.resize(at);
				}
			}
			probe_draws.push_back(d);
		}

		std::string write_probe()
		{
			FILE* file{};
			if (fopen_s(&file, probe_path.c_str(), "w") != 0 || !file)
			{
				return "error: cannot write " + probe_path;
			}
			std::fprintf(file, "# draw count start base instances | ib fmt off | vb stride off | vs ps | target width fmt | vs cb0..3 (buffer first count) | palette\n");
			int skinned = 0;
			for (const probe_draw& d : probe_draws)
			{
				std::fprintf(file, "D %u %u %u %d %u | %p %d %u | %p %u %u | %p %p | %p %u %d |", d.index, d.count, d.start, d.base, d.instances,
					static_cast<void*>(d.index_buffer), static_cast<int>(d.index_format), d.index_offset, static_cast<void*>(d.vertex_buffer), d.stride,
					d.vertex_offset, static_cast<void*>(d.vs), static_cast<void*>(d.ps), static_cast<void*>(d.target), d.target_width, static_cast<int>(d.target_format));
				for (const cb_binding& b : d.cb)
				{
					std::fprintf(file, " %p %u %u", static_cast<void*>(b.buffer), b.first, b.count);
				}
				std::fprintf(file, " | %d\n", d.palette);
				std::fprintf(file, "S %u | ps cb", d.index);
				for (const cb_binding& b : d.ps_cb)
				{
					std::fprintf(file, " %p %u %u", static_cast<void*>(b.buffer), b.first, b.count);
				}
				std::fprintf(file, " | rtv %u dsv %p layout %p topo %d | srv vs %x ps %x samplers %x | blend %p depth %p ref %u raster %p | blocks %d\n", d.targets, d.depth,
					d.layout, d.topology, d.vs_views, d.ps_views, d.ps_samplers, d.blend, d.depth_state, d.stencil_ref, d.raster, d.blocks);
				skinned += d.palette >= 0 ? 1 : 0;
			}
			for (std::size_t k = 0; (k + 1) * probe_block_slots * probe_block_constants * 4 <= probe_blocks.size(); ++k)
			{
				for (UINT slot = 0; slot < probe_block_slots; ++slot)
				{
					const float* b = probe_blocks.data() + (k * probe_block_slots + slot) * probe_block_constants * 4;
					std::fprintf(file, "B %zu %s%u", k, slot < tracked_slots - 1 ? "vs" : "ps", slot < tracked_slots - 1 ? slot + 1 : slot - (tracked_slots - 1));
					for (UINT i = 0; i < probe_block_constants * 4; ++i)
					{
						std::fprintf(file, " %.5g", b[i]);
					}
					std::fprintf(file, "\n");
				}
			}
			for (std::size_t p = 0; p * palette_constants * 4 < probe_palettes.size(); ++p)
			{
				std::fprintf(file, "P %zu\n", p);
				const float* m = probe_palettes.data() + p * palette_constants * 4;
				for (UINT bone = 0; bone < 64; ++bone)
				{
					const float* r = m + bone * 12;
					std::fprintf(file, "  %2u %9.5f %9.5f %9.5f %9.5f | %9.5f %9.5f %9.5f %9.5f | %9.5f %9.5f %9.5f %9.5f\n", bone, r[0], r[1], r[2], r[3], r[4], r[5], r[6],
						r[7], r[8], r[9], r[10], r[11]);
				}
			}
			std::fclose(file);
			return "meshprobe: " + std::to_string(probe_draws.size()) + " draws, " + std::to_string(skinned) + " skinned, written to " + probe_path;
		}
	}

	namespace
	{
		// Our palettes: a dynamic constant buffer, discarded on the first copy of each per-view render.
		// Below the eye pass's arena size (64 KB), so its Map hook never takes it for a game arena.
		constexpr UINT copy_buffer_size = 60u << 10;
		ComPtr<ID3D11Buffer> copy_buffer;
		ComPtr<ID3D11DeviceContext1> copy_context;
		UINT copy_offset{};       // bytes
		bool copy_discarded{};
		std::atomic<std::uint64_t> copies_drawn{};
		std::atomic<std::uint64_t> copy_overflows{};

		// Research copy (pipe meshcopy).
		std::atomic<std::uintptr_t> research_index_buffer{};
		std::atomic<float> research_shift[3]{};
		std::atomic<int> research_entries{ 2 };

		// The magazine copy: requested by the pose thread, taken once per render by the render thread.
		std::mutex request_lock;
		// #41: each capture's first bone (its first draw's lowest palette entry, relative to Wep_Root): the item's own
		// frame, for placing it where its mesh is rather than where its Wep_Root is.
		float anchor_rows[item_count][12]{};
		bool has_anchor[item_count]{};
		bool request_active{};
		float request_matrix[12]{};
		ULONGLONG request_at{};
		constexpr ULONGLONG request_stale_ms = 200;
		bool frame_copy{}; // render thread
		float frame_matrix[12]{};
		// Which draws hold the tagged bones, by (index buffer, start, count): a palette is read in full
		// only for these, and once for every other mesh until the table is cleared (every few seconds,
		// so a changed weapon is found again).
		constexpr unsigned magazine_bit = 1;
		constexpr unsigned grenade_bit = 2;
		struct known_mesh
		{
			ID3D11Buffer* index_buffer{};
			UINT start{};
			UINT count{};
			unsigned tags{}; // magazine_bit, grenade_bit
		};
		std::vector<known_mesh> known_meshes;
		ULONGLONG known_cleared{};
		constexpr ULONGLONG known_lifetime_ms = 4000;
		std::atomic<std::uint64_t> magazine_copies{};
		std::atomic<std::uint64_t> palettes_read{};
		std::atomic<unsigned> tagged_meshes{};

		bool ensure_copy_buffer(ID3D11DeviceContext* context)
		{
			if (copy_buffer && copy_context)
			{
				return true;
			}
			ComPtr<ID3D11Device> device;
			context->GetDevice(&device);
			D3D11_BUFFER_DESC desc{};
			desc.ByteWidth = copy_buffer_size;
			desc.Usage = D3D11_USAGE_DYNAMIC;
			desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
			desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
			if (!device || FAILED(device->CreateBuffer(&desc, nullptr, &copy_buffer)) || FAILED(context->QueryInterface(IID_PPV_ARGS(&copy_context))))
			{
				copy_buffer.Reset();
				copy_context.Reset();
				return false;
			}
			return true;
		}

		// Copies bytes into our constant buffer for this render; first = its first constant. Blocks start on
		// 256-byte boundaries (constant offsets are multiples of 16 constants).
		bool stage_constants(ID3D11DeviceContext* context, const void* data, UINT bytes, UINT& first)
		{
			if (!ensure_copy_buffer(context))
			{
				return false;
			}
			const UINT size = (bytes + 255u) & ~255u;
			if (copy_offset + size > copy_buffer_size)
			{
				++copy_overflows;
				return false;
			}
			D3D11_MAPPED_SUBRESOURCE mapped{};
			const D3D11_MAP type = copy_discarded ? D3D11_MAP_WRITE_NO_OVERWRITE : D3D11_MAP_WRITE_DISCARD;
			if (FAILED(context->Map(copy_buffer.Get(), 0, type, 0, &mapped)))
			{
				return false;
			}
			copy_discarded = true;
			std::memcpy(static_cast<std::uint8_t*>(mapped.pData) + copy_offset, data, bytes);
			context->Unmap(copy_buffer.Get(), 0);
			first = copy_offset / 16;
			copy_offset += size;
			return true;
		}

		// The draw just issued, once more with our palette in VS slot 0; the game's binding is restored.
		void draw_with_palette(ID3D11DeviceContext* context, const float* palette, UINT index_count, UINT start_index, INT base_vertex)
		{
			UINT first{};
			if (!draw_original || !vs_bind_original || !stage_constants(context, palette, palette_constants * 16, first))
			{
				return;
			}
			ID3D11Buffer* game_buffer{};
			UINT game_first{}, game_count{};
			copy_context->VSGetConstantBuffers1(0, 1, &game_buffer, &game_first, &game_count);
			ID3D11Buffer* const ours = copy_buffer.Get();
			const UINT count = palette_constants;
			vs_bind_original(context, 0, 1, &ours, &first, &count);
			draw_original(context, index_count, start_index, base_vertex);
			vs_bind_original(context, 0, 1, &game_buffer, &game_first, &game_count);
			if (game_buffer)
			{
				game_buffer->Release();
			}
			++copies_drawn;
		}
	}

	namespace
	{
		// Which pass a draw belongs to: the bound render targets and the blend state (the pre-pass and the
		// G-buffer write the same four targets with different blend states).
		struct pass_key
		{
			UINT targets{};
			DXGI_FORMAT format{};
			void* blend{};
			bool operator==(const pass_key&) const = default;
		};

		pass_key current_pass(ID3D11DeviceContext* context)
		{
			pass_key key;
			ID3D11RenderTargetView* views[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT]{};
			context->OMGetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, views, nullptr);
			for (ID3D11RenderTargetView* view : views)
			{
				if (!view)
				{
					continue;
				}
				if (key.targets++ == 0)
				{
					D3D11_RENDER_TARGET_VIEW_DESC desc{};
					view->GetDesc(&desc);
					key.format = desc.Format;
				}
				view->Release();
			}
			ComPtr<ID3D11BlendState> blend;
			FLOAT factor[4]{};
			UINT mask{};
			context->OMGetBlendState(&blend, factor, &mask);
			key.blend = blend.Get();
			return key;
		}

		// A bone under the hidden Wep_Root: scale grenade_tag, and (being at most a few metres below it, scaled
		// down) within 2 cm of the capture point. The scale alone also matches far-away instance matrices.
		bool under_hidden_root(const float* entry, float scale)
		{
			if (!palette::tagged(entry, scale))
			{
				return false;
			}
			const float dx = entry[3] - grenade_capture_position[0];
			const float dy = entry[7] - grenade_capture_position[1];
			const float dz = entry[11] - grenade_capture_position[2];
			return dx * dx + dy * dy + dz * dz < 0.02f * 0.02f;
		}

		bool grenade_entry(const float* entry)
		{
			return under_hidden_root(entry, grenade_tag);
		}

		// The gun's magazine under the hidden root (capture_magazine_scale): the gun is still drawn.
		bool gun_entry(const float* entry)
		{
			return under_hidden_root(entry, grenade_tag * capture_magazine_scale);
		}

		bool same_binding(const cb_binding& a, const cb_binding& b)
		{
			return a.buffer == b.buffer && a.first == b.first && a.count == b.count;
		}

		// Draws one captured item may hold: a whole primary gun (#58) is its body, attachments and magazine
		// in several passes. At most 64 (the replay keeps one bit per draw).
		constexpr std::size_t max_item_draws = 48;
		constexpr UINT grenade_vertex_slots = 4;
		constexpr UINT grenade_views = 16;
		constexpr UINT max_grenade_block = 256; // constants

		// One draw of the game's grenade, captured whole. State it does not own (per-view and per-object
		// constants, depth, blend, samplers, the rest of the views) comes from the magazine draw it follows.
		struct grenade_draw
		{
			pass_key pass;
			UINT index_count{};
			UINT start_index{};
			INT base_vertex{};
			ComPtr<ID3D11Buffer> index_buffer;
			DXGI_FORMAT index_format{};
			UINT index_offset{};
			ComPtr<ID3D11Buffer> vertex_buffers[grenade_vertex_slots];
			UINT strides[grenade_vertex_slots]{};
			UINT offsets[grenade_vertex_slots]{};
			ComPtr<ID3D11InputLayout> layout;
			D3D11_PRIMITIVE_TOPOLOGY topology{};
			ComPtr<ID3D11VertexShader> vs;
			ComPtr<ID3D11PixelShader> ps;
			ComPtr<ID3D11RasterizerState> raster;
			ComPtr<ID3D11ShaderResourceView> vs_views[grenade_views];
			ComPtr<ID3D11ShaderResourceView> ps_views[grenade_views];
			UINT vs_owned{}; // view slots that are the grenade's own
			UINT ps_owned{};
			cb_binding ps_cb[tracked_ps_slots];
			cb_binding vs_cb[tracked_slots];
			std::vector<float> ps_blocks[tracked_ps_slots]; // contents at the capture
			UINT ps_cb_owned{};
			std::vector<float> palette; // the grenade's entries relative to Wep_Root, all others zero
			std::uint64_t entries{};
		};

		// Another skinned draw of the same pass during the capture: what the grenade shares with it is
		// per-view state, left to the magazine draw at replay.
		struct capture_neighbour
		{
			pass_key pass;
			cb_binding ps_cb[tracked_ps_slots];
			ID3D11ShaderResourceView* vs_views[grenade_views]{};
			ID3D11ShaderResourceView* ps_views[grenade_views]{};
		};

		// Render thread. Per captured item (the grenade, #36; the knife, #41): its draws, and this render's copy.
		std::vector<grenade_draw> grenade_capturing;
		std::vector<grenade_draw> captured_items[item_count];
		std::vector<capture_neighbour> neighbours;
		bool capturing_render{};
		int capturing_item{}; // the item this capturing render records
		ULONGLONG last_capture{};
		std::uint64_t replayed[item_count]{}; // bits of captured_items[i] drawn in this render (max_item_draws)
		bool frame_item[item_count]{};
		bool frame_armed{};
		float frame_item_matrix[item_count][12]{};
		constexpr ULONGLONG capture_interval_ms = 250;

		// Pose thread requests (request_lock) and pipe-visible counters.
		std::atomic<ULONGLONG> capture_wanted_at{};
		std::atomic<int> capture_wanted_item{};
		bool item_request[item_count]{};
		bool grenade_request_armed{};
		float item_request_matrix[item_count][12]{};
		ULONGLONG item_request_at[item_count]{};
		std::atomic<bool> has_capture[item_count]{};
		std::atomic<unsigned> captured_draws[item_count]{};
		std::atomic<std::uint64_t> grenade_entries_seen{};
		std::atomic<std::uint64_t> captures[item_count]{};
		std::atomic<std::uint64_t> capture_attempts[item_count]{};
		std::atomic<std::uint64_t> rifle_draws_dropped{}; // #41: draws of the captured primary left out of another item's capture
		std::atomic<bool> forget_requested[item_count]{};
		std::atomic<std::uint64_t> item_copies[item_count]{};
		std::atomic<ULONGLONG> item_drawn_at[item_count]{};
		// The M67's palette: entry 0 the pin and ring, entry 1 body and spoon (monitor 2026-11-09).
		std::atomic<std::uint64_t> pin_entries{ 1 };
		std::atomic<std::uint64_t> captures_rejected{};
		bool capture_saw_gun{}; // render thread: a draw of this capturing render carried the gun's mark
		std::vector<known_mesh> magazine_meshes; // render thread: the gun magazine draws seen (compared by value only)
		std::mutex summary_lock;
		std::string capture_summary[item_count]; // each item's last capture's draws, for the pipe

		// Render thread: an index buffer of the captured primary (#58), i.e. the rifle's own meshes.
		bool primary_mesh(ID3D11Buffer* index_buffer)
		{
			for (const grenade_draw& rifle : captured_items[static_cast<int>(captured::primary)])
			{
				if (rifle.index_buffer.Get() == index_buffer)
				{
					return true;
				}
			}
			return false;
		}

		void get_views(ID3D11DeviceContext* context, bool pixel, ComPtr<ID3D11ShaderResourceView> (&out)[grenade_views])
		{
			ID3D11ShaderResourceView* views[grenade_views]{};
			if (pixel)
			{
				context->PSGetShaderResources(0, grenade_views, views);
			}
			else
			{
				context->VSGetShaderResources(0, grenade_views, views);
			}
			for (UINT i = 0; i < grenade_views; ++i)
			{
				out[i].Attach(views[i]);
			}
		}

		void note_neighbour(ID3D11DeviceContext* context)
		{
			const pass_key pass = current_pass(context);
			for (const capture_neighbour& n : neighbours)
			{
				if (n.pass == pass)
				{
					return;
				}
			}
			if (neighbours.size() >= 16)
			{
				return;
			}
			capture_neighbour n;
			n.pass = pass;
			std::copy(ps_bound, ps_bound + tracked_ps_slots, n.ps_cb);
			ComPtr<ID3D11ShaderResourceView> views[grenade_views];
			get_views(context, false, views);
			for (UINT i = 0; i < grenade_views; ++i)
			{
				n.vs_views[i] = views[i].Get(); // compared by value only
			}
			get_views(context, true, views);
			for (UINT i = 0; i < grenade_views; ++i)
			{
				n.ps_views[i] = views[i].Get();
			}
			neighbours.push_back(n);
		}

		void capture_grenade_draw(ID3D11DeviceContext* context, const float* palette_data, UINT count, UINT start, INT base)
		{
			grenade_draw d;
			d.pass = current_pass(context);
			if (d.pass.targets == 0)
			{
				return; // shadow maps: the copies follow magazine draws, which have none
			}
			for (const grenade_draw& other : grenade_capturing)
			{
				if (other.pass == d.pass && other.start_index == start && other.index_count == count)
				{
					return;
				}
			}
			if (grenade_capturing.size() >= max_item_draws)
			{
				return;
			}
			d.index_count = count;
			d.start_index = start;
			d.base_vertex = base;
			context->IAGetIndexBuffer(&d.index_buffer, &d.index_format, &d.index_offset);
			ID3D11Buffer* buffers[grenade_vertex_slots]{};
			context->IAGetVertexBuffers(0, grenade_vertex_slots, buffers, d.strides, d.offsets);
			for (UINT i = 0; i < grenade_vertex_slots; ++i)
			{
				d.vertex_buffers[i].Attach(buffers[i]);
			}
			context->IAGetInputLayout(&d.layout);
			context->IAGetPrimitiveTopology(&d.topology);
			context->VSGetShader(&d.vs, nullptr, nullptr);
			context->PSGetShader(&d.ps, nullptr, nullptr);
			context->RSGetState(&d.raster);
			get_views(context, false, d.vs_views);
			get_views(context, true, d.ps_views);
			std::copy(vs_bound + 1, vs_bound + tracked_slots, d.vs_cb + 1); // slot 0 is the palette
			for (UINT slot = 0; slot < tracked_ps_slots; ++slot)
			{
				d.ps_cb[slot] = ps_bound[slot];
				const cb_binding& b = ps_bound[slot];
				if (b.buffer && b.count != 4096 && b.count <= max_grenade_block)
				{
					d.ps_blocks[slot].resize(b.count * 4);
					if (!read_block(b.buffer, b.first, b.count, d.ps_blocks[slot].data()))
					{
						d.ps_blocks[slot].clear();
					}
				}
			}
			const float root[3] = { grenade_capture_position[0], grenade_capture_position[1], grenade_capture_position[2] };
			d.palette.assign(palette_data, palette_data + palette_constants * 4);
			for (int e = 0; e < palette::entries; ++e)
			{
				float* entry = d.palette.data() + e * palette::floats_per_entry;
				if (grenade_entry(entry))
				{
					palette::relative_to_root(entry, grenade_tag, root);
					d.entries |= 1ull << e;
				}
				else
				{
					std::fill(entry, entry + palette::floats_per_entry, 0.0f);
				}
			}
			if (d.entries && d.index_buffer && d.vertex_buffers[0] && d.layout && d.vs && d.ps)
			{
				grenade_capturing.push_back(std::move(d));
			}
		}

		// End of a capturing render: what each draw owns, then the capture replaces the last one.
		void finish_capture()
		{
			for (grenade_draw& d : grenade_capturing)
			{
				const capture_neighbour* n = nullptr;
				for (const capture_neighbour& other : neighbours)
				{
					n = other.pass == d.pass ? &other : n;
				}
				for (UINT slot = 0; slot < tracked_ps_slots; ++slot)
				{
					// A block the vertex shader has too is the object's (its world transform), not the material's.
					bool object = false;
					for (const cb_binding& vs : d.vs_cb)
					{
						object = object || (vs.buffer && same_binding(vs, d.ps_cb[slot]));
					}
					if (!d.ps_blocks[slot].empty() && !object && (!n || !same_binding(n->ps_cb[slot], d.ps_cb[slot])))
					{
						d.ps_cb_owned |= 1u << slot;
					}
				}
				for (UINT i = 0; i < grenade_views; ++i)
				{
					if (d.vs_views[i] && (!n || n->vs_views[i] != d.vs_views[i].Get()))
					{
						d.vs_owned |= 1u << i;
					}
					if (d.ps_views[i] && (!n || n->ps_views[i] != d.ps_views[i].Get()))
					{
						d.ps_owned |= 1u << i;
					}
				}
			}
			// The gun still drawn under the hidden root (the switch to the grenade not finished): not a grenade.
			// Its magazine is marked (capture_magazine_scale); the list of magazine meshes seen before covers a gun
			// whose magazine draw came after the mark was lifted.
			// The grenade is drawn in several passes (pre-pass, G-buffer, the R16G16 one) from one index buffer;
			// a tagged draw whose mesh shows up in one pass only is something else (headset 2026-11-09: gear
			// in the G-buffer only).
			std::vector<bool> keep(grenade_capturing.size(), false);
			for (std::size_t i = 0; i < grenade_capturing.size(); ++i)
			{
				for (std::size_t j = 0; j < grenade_capturing.size(); ++j)
				{
					keep[i] = keep[i] || (grenade_capturing[i].index_buffer.Get() == grenade_capturing[j].index_buffer.Get() && !(grenade_capturing[i].pass == grenade_capturing[j].pass));
				}
			}
			// Only what would be kept counts: with the knife out the gun's body is still drawn under the hidden
			// root in the G-buffer alone (monitor 2026-11-11), which the one-pass rule leaves out anyway.
			// The knife: draws seen with the gun before (the hands share its skeleton under the hidden root) are
			// left out rather than taken for the gun still drawn (monitor 2026-11-11: every knife capture was
			// rejected that way).
			const bool knife = capturing_item == static_cast<int>(captured::knife);
			// #58: the primary is the gun: its own draws (magazine included) are what is wanted.
			const bool primary = capturing_item == static_cast<int>(captured::primary);
			// #41: for another item, a draw from an index buffer of the captured primary is the rifle still drawn
			// during the switch (headset 2026-11-11: the knife's capture kept the rifle's 22-23k-index meshes).
			if (!primary)
			{
				for (std::size_t i = 0; i < grenade_capturing.size(); ++i)
				{
					for (const grenade_draw& rifle : captured_items[static_cast<int>(captured::primary)])
					{
						if (keep[i] && grenade_capturing[i].index_buffer.Get() == rifle.index_buffer.Get())
						{
							keep[i] = false;
							++rifle_draws_dropped;
						}
					}
				}
			}
			bool gun = capture_saw_gun && !knife && !primary;
			std::vector<bool> seen_with_gun(grenade_capturing.size(), false);
			for (std::size_t i = 0; i < grenade_capturing.size(); ++i)
			{
				for (const known_mesh& magazine : magazine_meshes)
				{
					const grenade_draw& d = grenade_capturing[i];
					const bool seen = d.index_buffer.Get() == magazine.index_buffer && d.start_index == magazine.start && d.index_count == magazine.count;
					seen_with_gun[i] = seen_with_gun[i] || seen;
					gun = gun || (keep[i] && seen && !knife && !primary);
				}
			}
			if (knife)
			{
				// #41 (monitor 2026-11-16): the level can start with the knife out, so its own pre-pass draw was listed
				// among the gun's and dropped, and the G-buffer draw alone drew nothing. A draw from the index buffer of
				// a kept draw not seen with the gun is the knife's.
				std::vector<const void*> buffers;
				for (const grenade_draw& d : grenade_capturing)
				{
					buffers.push_back(d.index_buffer.Get());
				}
				keep = palette::keep_knife_draws(keep, seen_with_gun, buffers);
			}
			if (!grenade_capturing.empty())
			{
				std::string summary = gun ? "\n  rejected (the gun among them):" : "";
				for (std::size_t i = 0; i < grenade_capturing.size(); ++i)
				{
					const grenade_draw& d = grenade_capturing[i];
					// #41: the first entry's translation from the root (metres): where the bone sits from Wep_Root.
					float offset[3]{};
					for (int e = 0; e < palette::entries; ++e)
					{
						if (d.entries & (1ull << e))
						{
							const float* entry = d.palette.data() + e * palette::floats_per_entry;
							offset[0] = entry[3];
							offset[1] = entry[7];
							offset[2] = entry[11];
							break;
						}
					}
					char line[300];
					std::snprintf(line, sizeof(line), "\n  draw ib=%p count=%u start=%u targets=%u format=%d blend=%p entries=%llx ps_cb_owned=%x ps_views_owned=%x vs_views_owned=%x from_root=(%.3f %.3f %.3f)%s",
						static_cast<void*>(d.index_buffer.Get()), d.index_count, d.start_index, d.pass.targets, static_cast<int>(d.pass.format), d.pass.blend,
						static_cast<unsigned long long>(d.entries), d.ps_cb_owned, d.ps_owned, d.vs_owned, offset[0], offset[1], offset[2],
						keep[i] ? "" : " (one pass only: left out)");
					summary += line;
				}
				const std::lock_guard guard(summary_lock);
				capture_summary[capturing_item] = summary;
			}
			{
				std::vector<grenade_draw> kept;
				for (std::size_t i = 0; i < grenade_capturing.size(); ++i)
				{
					if (keep[i])
					{
						kept.push_back(std::move(grenade_capturing[i]));
					}
				}
				grenade_capturing = std::move(kept);
			}
			capture_saw_gun = false;
			last_capture = GetTickCount64(); // also after a rejected or empty one: not every render
			++capture_attempts[capturing_item];
			if (gun)
			{
				++captures_rejected;
				grenade_capturing.clear();
			}
			if (!grenade_capturing.empty())
			{
				std::uint64_t seen = 0;
				for (const grenade_draw& d : grenade_capturing)
				{
					seen |= d.entries;
				}
				const int item = capturing_item;
				captured_items[item] = std::move(grenade_capturing);
				captured_draws[item] = static_cast<unsigned>(captured_items[item].size());
				if (item == static_cast<int>(captured::grenade))
				{
					grenade_entries_seen = seen;
				}
				{
					const grenade_draw& first = captured_items[item].front();
					for (int e = 0; e < palette::entries; ++e)
					{
						if (first.entries & (1ull << e))
						{
							const std::lock_guard guard(request_lock);
							std::memcpy(anchor_rows[item], first.palette.data() + e * palette::floats_per_entry, sizeof(anchor_rows[item]));
							has_anchor[item] = true;
							break;
						}
					}
				}
				has_capture[item] = true;
				++captures[item];
			}
			grenade_capturing.clear();
			neighbours.clear();
		}

		// A captured item's draw, right after a magazine draw of the same pass, with its root where the pose
		// thread asked (the hand, the pouch, the holster).
		void draw_grenade(ID3D11DeviceContext* context, const grenade_draw& d, int item)
		{
			if (!draw_original || !vs_bind_original || !ps_bind_original)
			{
				return;
			}
			alignas(16) float placed[palette_constants * 4]{};
			// The grenade's pin and its ring once pulled, unless they would be all of the draw.
			const std::uint64_t pins = pin_entries.load(std::memory_order_relaxed);
			const bool armed = frame_armed && item == static_cast<int>(captured::grenade);
			const std::uint64_t hidden = armed && (d.entries & ~pins) ? pins : 0;
			for (int e = 0; e < palette::entries; ++e)
			{
				const std::uint64_t bit = 1ull << e;
				if ((d.entries & bit) && !(hidden & bit))
				{
					palette::multiply(frame_item_matrix[item], d.palette.data() + e * palette::floats_per_entry, placed + e * palette::floats_per_entry);
				}
			}
			UINT palette_first{};
			if (!stage_constants(context, placed, sizeof(placed), palette_first))
			{
				return;
			}
			UINT block_first[tracked_ps_slots]{};
			for (UINT slot = 0; slot < tracked_ps_slots; ++slot)
			{
				if ((d.ps_cb_owned & (1u << slot))
					&& !stage_constants(context, d.ps_blocks[slot].data(), static_cast<UINT>(d.ps_blocks[slot].size() * sizeof(float)), block_first[slot]))
				{
					return;
				}
			}
			// The magazine draw's state.
			ComPtr<ID3D11Buffer> index_buffer;
			DXGI_FORMAT index_format{};
			UINT index_offset{};
			context->IAGetIndexBuffer(&index_buffer, &index_format, &index_offset);
			ID3D11Buffer* vertex_buffers[grenade_vertex_slots]{};
			UINT strides[grenade_vertex_slots]{}, offsets[grenade_vertex_slots]{};
			context->IAGetVertexBuffers(0, grenade_vertex_slots, vertex_buffers, strides, offsets);
			ComPtr<ID3D11InputLayout> layout;
			context->IAGetInputLayout(&layout);
			D3D11_PRIMITIVE_TOPOLOGY topology{};
			context->IAGetPrimitiveTopology(&topology);
			ComPtr<ID3D11VertexShader> vs;
			context->VSGetShader(&vs, nullptr, nullptr);
			ComPtr<ID3D11PixelShader> ps;
			context->PSGetShader(&ps, nullptr, nullptr);
			ComPtr<ID3D11RasterizerState> raster;
			context->RSGetState(&raster);
			ComPtr<ID3D11ShaderResourceView> vs_views[grenade_views], ps_views[grenade_views];
			get_views(context, false, vs_views);
			get_views(context, true, ps_views);
			ID3D11Buffer* ps_buffers[tracked_ps_slots]{};
			UINT ps_first[tracked_ps_slots]{}, ps_count[tracked_ps_slots]{};
			copy_context->PSGetConstantBuffers1(0, tracked_ps_slots, ps_buffers, ps_first, ps_count);
			ID3D11Buffer* vs_palette{};
			UINT vs_palette_first{}, vs_palette_count{};
			copy_context->VSGetConstantBuffers1(0, 1, &vs_palette, &vs_palette_first, &vs_palette_count);

			// The grenade's.
			context->IASetIndexBuffer(d.index_buffer.Get(), d.index_format, d.index_offset);
			ID3D11Buffer* ours[grenade_vertex_slots]{};
			for (UINT i = 0; i < grenade_vertex_slots; ++i)
			{
				ours[i] = d.vertex_buffers[i].Get();
			}
			context->IASetVertexBuffers(0, grenade_vertex_slots, ours, d.strides, d.offsets);
			context->IASetInputLayout(d.layout.Get());
			context->IASetPrimitiveTopology(d.topology);
			context->VSSetShader(d.vs.Get(), nullptr, 0);
			context->PSSetShader(d.ps.Get(), nullptr, 0);
			context->RSSetState(d.raster.Get());
			for (UINT i = 0; i < grenade_views; ++i)
			{
				if (d.vs_owned & (1u << i))
				{
					ID3D11ShaderResourceView* const view = d.vs_views[i].Get();
					context->VSSetShaderResources(i, 1, &view);
				}
				if (d.ps_owned & (1u << i))
				{
					ID3D11ShaderResourceView* const view = d.ps_views[i].Get();
					context->PSSetShaderResources(i, 1, &view);
				}
			}
			ID3D11Buffer* const copy = copy_buffer.Get();
			for (UINT slot = 0; slot < tracked_ps_slots; ++slot)
			{
				if (d.ps_cb_owned & (1u << slot))
				{
					const UINT count = d.ps_cb[slot].count;
					ps_bind_original(context, slot, 1, &copy, &block_first[slot], &count);
				}
			}
			const UINT palette_count = palette_constants;
			vs_bind_original(context, 0, 1, &copy, &palette_first, &palette_count);
			draw_original(context, d.index_count, d.start_index, d.base_vertex);

			// Back to the magazine draw's state.
			vs_bind_original(context, 0, 1, &vs_palette, &vs_palette_first, &vs_palette_count);
			for (UINT slot = 0; slot < tracked_ps_slots; ++slot)
			{
				if (d.ps_cb_owned & (1u << slot))
				{
					ps_bind_original(context, slot, 1, &ps_buffers[slot], &ps_first[slot], &ps_count[slot]);
				}
			}
			for (UINT i = 0; i < grenade_views; ++i)
			{
				if (d.vs_owned & (1u << i))
				{
					ID3D11ShaderResourceView* const view = vs_views[i].Get();
					context->VSSetShaderResources(i, 1, &view);
				}
				if (d.ps_owned & (1u << i))
				{
					ID3D11ShaderResourceView* const view = ps_views[i].Get();
					context->PSSetShaderResources(i, 1, &view);
				}
			}
			context->RSSetState(raster.Get());
			context->PSSetShader(ps.Get(), nullptr, 0);
			context->VSSetShader(vs.Get(), nullptr, 0);
			context->IASetPrimitiveTopology(topology);
			context->IASetInputLayout(layout.Get());
			context->IASetVertexBuffers(0, grenade_vertex_slots, vertex_buffers, strides, offsets);
			context->IASetIndexBuffer(index_buffer.Get(), index_format, index_offset);
			for (ID3D11Buffer* buffer : vertex_buffers)
			{
				if (buffer)
				{
					buffer->Release();
				}
			}
			for (ID3D11Buffer* buffer : ps_buffers)
			{
				if (buffer)
				{
					buffer->Release();
				}
			}
			if (vs_palette)
			{
				vs_palette->Release();
			}
			++item_copies[item];
			item_drawn_at[item] = GetTickCount64();
		}

		// #41 research: the passes the magazine is drawn in (render thread) and a text snapshot for the pipe, written every
		// 120 renders: each captured item's draws with their pass (targets, format, blend state).
		std::vector<pass_key> magazine_passes_seen;
		std::mutex pass_report_lock;
		std::string pass_report;
		unsigned pass_report_tick{};

		void note_magazine_pass(ID3D11DeviceContext* context)
		{
			const pass_key pass = current_pass(context);
			if (magazine_passes_seen.size() < 8 && std::find(magazine_passes_seen.begin(), magazine_passes_seen.end(), pass) == magazine_passes_seen.end())
			{
				magazine_passes_seen.push_back(pass);
			}
		}

		void write_pass_report()
		{
			if (++pass_report_tick % 120 != 0)
			{
				return;
			}
			std::string text;
			char line[96];
			for (int item = 0; item < item_count; ++item)
			{
				text += std::string("\n  ") + item_name(static_cast<captured>(item)) + " draws:";
				for (const grenade_draw& draw : captured_items[item])
				{
					// The scale of the first used palette entry (its first row's length): near 0 is a draw captured hidden.
					float scale = -1.0f;
					for (int e = 0; e < palette::entries && scale < 0.0f; ++e)
					{
						if (draw.entries & (1ull << e))
						{
							const float* p = draw.palette.data() + e * palette::floats_per_entry;
							scale = sqrtf(p[0] * p[0] + p[1] * p[1] + p[2] * p[2]);
						}
					}
					std::snprintf(line, sizeof(line), " [t%u f%d b%p s%.3f]", draw.pass.targets, static_cast<int>(draw.pass.format), draw.pass.blend, scale);
					text += line;
				}
			}
			text += "\n  magazine passes:";
			for (const pass_key& pass : magazine_passes_seen)
			{
				std::snprintf(line, sizeof(line), " [t%u f%d b%p]", pass.targets, static_cast<int>(pass.format), pass.blend);
				text += line;
			}
			const std::lock_guard guard(pass_report_lock);
			pass_report = text;
		}

		// After a magazine draw: each wanted item's captured draws of this pass, once per render.
		void replay_items(ID3D11DeviceContext* context)
		{
			const pass_key pass = current_pass(context);
			for (int item = 0; item < item_count; ++item)
			{
				if (!frame_item[item])
				{
					continue;
				}
				const std::vector<grenade_draw>& draws = captured_items[item];
				for (std::size_t i = 0; i < draws.size() && i < max_item_draws; ++i)
				{
					if (!(replayed[item] & (1ull << i)) && draws[i].pass == pass)
					{
						replayed[item] |= 1ull << i;
						draw_grenade(context, draws[i], item);
					}
				}
			}
		}
	}

	void set_entry_points(draw_indexed_fn draw_indexed, set_constant_buffers1_fn vs_set_constant_buffers1, set_constant_buffers1_fn ps_set_constant_buffers1,
		read_block_fn read)
	{
		draw_original = draw_indexed;
		vs_bind_original = vs_set_constant_buffers1;
		ps_bind_original = ps_set_constant_buffers1;
		read_block = read;
	}

	void begin_render(int eye)
	{
		write_pass_report();
		render_eye = eye;
		copy_offset = 0;
		copy_discarded = false;
		const ULONGLONG now = GetTickCount64();
		{
			const std::lock_guard guard(request_lock);
			frame_copy = request_active && now - request_at < request_stale_ms;
			std::memcpy(frame_matrix, request_matrix, sizeof(frame_matrix));
			for (int item = 0; item < item_count; ++item)
			{
				frame_item[item] = item_request[item] && now - item_request_at[item] < request_stale_ms;
				std::memcpy(frame_item_matrix[item], item_request_matrix[item], sizeof(frame_item_matrix[item]));
				replayed[item] = 0;
			}
			frame_armed = grenade_request_armed;
		}
		for (int item = 0; item < item_count; ++item)
		{
			if (forget_requested[item].exchange(false))
			{
				captured_items[item].clear();
				captured_draws[item] = 0;
				has_capture[item] = false;
				const std::lock_guard guard(request_lock);
				has_anchor[item] = false;
			}
		}
		capturing_render = eye == 0 && now - capture_wanted_at.load(std::memory_order_relaxed) < request_stale_ms && now - last_capture > capture_interval_ms;
		capturing_item = capture_wanted_item.load(std::memory_order_relaxed);
		if (now - known_cleared > known_lifetime_ms)
		{
			known_cleared = now;
			unsigned tagged = 0;
			for (const known_mesh& mesh : known_meshes)
			{
				tagged += mesh.tags ? 1 : 0;
			}
			tagged_meshes = tagged;
			known_meshes.clear();
		}
		if (eye == 0 && probe_requested.exchange(false))
		{
			probe_draws.clear();
			probe_palettes.clear();
			probe_blocks.clear();
			probe_index = 0;
			probing = true;
		}
	}

	void end_render()
	{
		if (capturing_render)
		{
			capturing_render = false;
			finish_capture();
		}
		if (probing && render_eye == 0)
		{
			probing = false;
			probe_result = write_probe();
			if (probe_done)
			{
				SetEvent(probe_done);
			}
		}
		render_eye = -1;
	}

	void on_vs_bindings(UINT start, UINT count, ID3D11Buffer* const* buffers, const UINT* first, const UINT* number)
	{
		for (UINT i = 0; i < count; ++i)
		{
			const UINT slot = start + i;
			if (slot >= tracked_slots)
			{
				continue;
			}
			vs_bound[slot].buffer = buffers ? buffers[i] : nullptr;
			vs_bound[slot].first = first ? first[i] : 0;
			vs_bound[slot].count = number ? number[i] : 4096;
		}
	}

	void on_ps_bindings(UINT start, UINT count, ID3D11Buffer* const* buffers, const UINT* first, const UINT* number)
	{
		for (UINT i = 0; i < count; ++i)
		{
			const UINT slot = start + i;
			if (slot >= tracked_ps_slots)
			{
				continue;
			}
			ps_bound[slot].buffer = buffers ? buffers[i] : nullptr;
			ps_bound[slot].first = first ? first[i] : 0;
			ps_bound[slot].count = number ? number[i] : 4096;
		}
	}

	void after_draw(ID3D11DeviceContext* context, UINT index_count, UINT start_index, INT base_vertex, UINT instances)
	{
		if (probing && render_eye == 0 && probe_draws.size() < 5000)
		{
			record(context, index_count, start_index, base_vertex, instances);
		}
		const bool palette_draw = render_eye >= 0 && instances == 1 && vs_bound[0].buffer && vs_bound[0].count == palette_constants && read_block;
		bool grenade_replay = false;
		for (int item = 0; item < item_count; ++item)
		{
			grenade_replay = grenade_replay || (frame_item[item] && !captured_items[item].empty());
		}
		if (palette_draw && (frame_copy || grenade_replay || capturing_render))
		{
			ComPtr<ID3D11Buffer> ib;
			DXGI_FORMAT format{};
			UINT offset{};
			context->IAGetIndexBuffer(&ib, &format, &offset);
			known_mesh* known = nullptr;
			for (known_mesh& mesh : known_meshes)
			{
				if (mesh.index_buffer == ib.Get() && mesh.start == start_index && mesh.count == index_count)
				{
					known = &mesh;
					break;
				}
			}
			if (known && !known->tags && !capturing_render)
			{
				return;
			}
			alignas(16) static float palette_data[palette_constants * 4];
			if (!read_block(vs_bound[0].buffer, vs_bound[0].first, palette_constants, palette_data))
			{
				return;
			}
			++palettes_read;
			unsigned tags = 0;
			bool gun_mark = false;
			for (int e = 0; e < palette::entries; ++e)
			{
				const float* entry = palette_data + e * palette::floats_per_entry;
				tags |= palette::tagged(entry, magazine_tag) ? magazine_bit : grenade_entry(entry) ? grenade_bit : 0u;
				gun_mark = gun_mark || (capturing_render && gun_entry(entry));
			}
			// #41: the gun's mark on a mesh of the captured primary is the rifle still drawn during the switch; its
			// draws are left out of the capture (finish_capture), so it does not spoil the whole capture.
			capture_saw_gun = capture_saw_gun || (gun_mark && !primary_mesh(ib.Get()));
			if (!known && known_meshes.size() < 512)
			{
				known_meshes.push_back({ ib.Get(), start_index, index_count, tags });
			}
			else if (known)
			{
				known->tags = tags;
			}
			if (capturing_render)
			{
				if (tags & grenade_bit)
				{
					capture_grenade_draw(context, palette_data, index_count, start_index, base_vertex);
				}
				else
				{
					note_neighbour(context);
				}
			}
			if (tags & magazine_bit)
			{
				const bool listed = std::any_of(magazine_meshes.begin(), magazine_meshes.end(), [&](const known_mesh& m) {
					return m.index_buffer == ib.Get() && m.start == start_index && m.count == index_count;
				});
				if (!listed)
				{
					if (magazine_meshes.size() >= 16)
					{
						magazine_meshes.erase(magazine_meshes.begin());
					}
					magazine_meshes.push_back({ ib.Get(), start_index, index_count, magazine_bit });
				}
				note_magazine_pass(context);
				if (grenade_replay)
				{
					replay_items(context);
				}
				if (frame_copy && palette::move_tagged(palette_data, frame_matrix, magazine_tag) > 0)
				{
					draw_with_palette(context, palette_data, index_count, start_index, base_vertex);
					++magazine_copies;
				}
			}
			return;
		}
		const std::uintptr_t research = research_index_buffer.load(std::memory_order_relaxed);
		if (research && render_eye >= 0 && instances == 1 && vs_bound[0].buffer && vs_bound[0].count == palette_constants && read_block)
		{
			ComPtr<ID3D11Buffer> ib;
			DXGI_FORMAT format{};
			UINT offset{};
			context->IAGetIndexBuffer(&ib, &format, &offset);
			alignas(16) static float palette[palette_constants * 4];
			if (reinterpret_cast<std::uintptr_t>(ib.Get()) == research && read_block(vs_bound[0].buffer, vs_bound[0].first, palette_constants, palette))
			{
				const int entries = research_entries.load(std::memory_order_relaxed);
				for (int bone = 0; bone < entries && bone < 64; ++bone)
				{
					for (int row = 0; row < 3; ++row)
					{
						palette[bone * 12 + row * 4 + 3] += research_shift[row].load(std::memory_order_relaxed);
					}
				}
				draw_with_palette(context, palette, index_count, start_index, base_vertex);
			}
		}
	}

	void set_magazine_copy(bool active, const float* matrix)
	{
		const std::lock_guard guard(request_lock);
		request_active = active && matrix;
		if (request_active)
		{
			std::memcpy(request_matrix, matrix, sizeof(request_matrix));
			request_at = GetTickCount64();
		}
	}

	void set_item_capture(captured item, bool wanted)
	{
		if (wanted)
		{
			capture_wanted_item = static_cast<int>(item);
			capture_wanted_at = GetTickCount64();
		}
	}

	void set_item_copy(captured item, bool active, const float* matrix)
	{
		const int i = static_cast<int>(item);
		const std::lock_guard guard(request_lock);
		item_request[i] = active && matrix;
		if (item_request[i])
		{
			std::memcpy(item_request_matrix[i], matrix, sizeof(item_request_matrix[i]));
			item_request_at[i] = GetTickCount64();
		}
	}

	bool item_anchor(captured item, float (&rows)[12])
	{
		const int i = static_cast<int>(item);
		const std::lock_guard guard(request_lock);
		if (!has_anchor[i])
		{
			return false;
		}
		std::memcpy(rows, anchor_rows[i], sizeof(rows));
		return true;
	}

	bool item_drawn(captured item)
	{
		return GetTickCount64() - item_drawn_at[static_cast<int>(item)].load(std::memory_order_relaxed) < 150;
	}

	bool item_captured(captured item)
	{
		return has_capture[static_cast<int>(item)].load(std::memory_order_relaxed);
	}

	std::uint64_t item_captures(captured item)
	{
		return captures[static_cast<int>(item)].load(std::memory_order_relaxed);
	}

	const char* item_name(captured item)
	{
		switch (item)
		{
		case captured::grenade: return "grenade";
		case captured::knife: return "knife";
		case captured::primary: return "primary";
		}
		return "?";
	}

	void forget_item(captured item)
	{
		const int i = static_cast<int>(item);
		capture_attempts[i] = 0;
		forget_requested[i] = true;
	}

	std::uint64_t item_capture_attempts(captured item)
	{
		return capture_attempts[static_cast<int>(item)].load(std::memory_order_relaxed);
	}

	void set_grenade_capture(bool wanted)
	{
		set_item_capture(captured::grenade, wanted);
	}

	void set_grenade_copy(bool active, const float* matrix, bool armed)
	{
		{
			const std::lock_guard guard(request_lock);
			grenade_request_armed = armed;
		}
		set_item_copy(captured::grenade, active, matrix);
	}

	bool grenade_drawn()
	{
		return item_drawn(captured::grenade);
	}

	bool grenade_captured()
	{
		return item_captured(captured::grenade);
	}

	std::uint64_t grenade_captures()
	{
		return item_captures(captured::grenade);
	}

	std::string set_grenade_pin_entries(const std::string& mask)
	{
		unsigned long long value{};
		if (sscanf_s(mask.c_str(), "%llx", &value) == 1)
		{
			pin_entries = value;
		}
		char text[64];
		std::snprintf(text, sizeof(text), "extramesh pin entries %llx", static_cast<unsigned long long>(pin_entries.load()));
		return text;
	}

	std::string grenade_report()
	{
		std::string out;
		for (int item = 0; item < item_count; ++item)
		{
			bool active{}, armed{};
			float t[3]{};
			ULONGLONG age{};
			{
				const std::lock_guard guard(request_lock);
				active = item_request[item];
				armed = item == static_cast<int>(captured::grenade) && grenade_request_armed;
				t[0] = item_request_matrix[item][3];
				t[1] = item_request_matrix[item][7];
				t[2] = item_request_matrix[item][11];
				age = GetTickCount64() - item_request_at[item];
			}
			char text[320];
			std::snprintf(text, sizeof(text), "%s%s_copy=%d armed=%d at=(%.3f %.3f %.3f) age_ms=%llu captured=%d captures=%llu attempts=%llu draws=%u copies=%llu",
				item ? "\n" : "", item_name(static_cast<captured>(item)), active ? 1 : 0, armed ? 1 : 0, t[0], t[1], t[2],
				static_cast<unsigned long long>(age), has_capture[item].load() ? 1 : 0, static_cast<unsigned long long>(captures[item].load()),
				static_cast<unsigned long long>(capture_attempts[item].load()), captured_draws[item].load(), static_cast<unsigned long long>(item_copies[item].load()));
			out += text;
		}
		{
			const std::lock_guard guard(pass_report_lock);
			out += pass_report;
		}
		char text[200];
		std::snprintf(text, sizeof(text), "\nrejected=%llu rifle_draws_dropped=%llu grenade_entries=%llx pin=%llx capture_wanted=%s %llu ms ago",
			static_cast<unsigned long long>(captures_rejected.load()), static_cast<unsigned long long>(rifle_draws_dropped.load()),
			static_cast<unsigned long long>(grenade_entries_seen.load()),
			static_cast<unsigned long long>(pin_entries.load()), item_name(static_cast<captured>(capture_wanted_item.load())),
			static_cast<unsigned long long>(GetTickCount64() - capture_wanted_at.load()));
		out += text;
		const std::lock_guard guard(summary_lock);
		for (int item = 0; item < item_count; ++item)
		{
			if (!capture_summary[item].empty())
			{
				out += "\n  last ";
				out += item_name(static_cast<captured>(item));
				out += " capture:" + capture_summary[item];
			}
		}
		return out;
	}

	std::string forget_pipe_command(const std::string& name)
	{
		for (int item = 0; item < item_count; ++item)
		{
			if (name == item_name(static_cast<captured>(item)))
			{
				forget_item(static_cast<captured>(item));
				return std::string("extramesh forgot ") + name + ": captured again the next time it is out";
			}
		}
		return "extramesh forget grenade|knife|primary";
	}

	std::string report()
	{
		bool active{};
		float t[3]{};
		ULONGLONG age{};
		{
			const std::lock_guard guard(request_lock);
			active = request_active;
			t[0] = request_matrix[3];
			t[1] = request_matrix[7];
			t[2] = request_matrix[11];
			age = GetTickCount64() - request_at;
		}
		char text[256];
		std::snprintf(text, sizeof(text), "extra_mesh magazine_copy=%d move=(%.3f %.3f %.3f) age_ms=%llu copies=%llu palettes_read=%llu tagged_meshes=%u overflows=%llu",
			active ? 1 : 0, t[0], t[1], t[2], static_cast<unsigned long long>(age), static_cast<unsigned long long>(magazine_copies.load()),
			static_cast<unsigned long long>(palettes_read.load()), tagged_meshes.load(), static_cast<unsigned long long>(copy_overflows.load()));
		return std::string(text) + "\n" + grenade_report();
	}

	std::string set_research_copy(const std::string& spec)
	{
		unsigned long long buffer{};
		float d[3]{};
		int entries = 2;
		if (spec == "off" || sscanf_s(spec.c_str(), "%llx %f %f %f %d", &buffer, &d[0], &d[1], &d[2], &entries) < 4)
		{
			research_index_buffer = 0;
			return "meshcopy off (copies drawn " + std::to_string(copies_drawn.load()) + ", overflows " + std::to_string(copy_overflows.load()) + ")";
		}
		for (int i = 0; i < 3; ++i)
		{
			research_shift[i] = d[i];
		}
		research_entries = entries;
		research_index_buffer = static_cast<std::uintptr_t>(buffer);
		char text[160];
		std::snprintf(text, sizeof(text), "meshcopy ib %llx shift (%.3f %.3f %.3f) entries %d; copies drawn so far %llu", buffer, d[0], d[1], d[2], entries,
			static_cast<unsigned long long>(copies_drawn.load()));
		return text;
	}

	std::string probe(const std::string& path)
	{
		if (path.empty())
		{
			return "error: usage: meshprobe <file.txt>";
		}
		probe_path = path;
		probe_result.clear();
		probe_done = CreateEventW(nullptr, TRUE, FALSE, nullptr);
		probe_requested = true;
		const DWORD waited = WaitForSingleObject(probe_done, 5000);
		probe_requested = false;
		CloseHandle(probe_done);
		probe_done = nullptr;
		return waited == WAIT_OBJECT_0 ? probe_result : "error: no eye-pass frame within 5 s (pair rendering off?)";
	}
}
