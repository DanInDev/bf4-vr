#include "stereo/extra_mesh.hpp"

#include <wrl/client.h>

#include "stereo/palette_copy.hpp"

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
		read_block_fn read_block{};

		struct cb_binding
		{
			ID3D11Buffer* buffer{};
			UINT first{};
			UINT count{};
		};
		constexpr UINT tracked_slots = 4;
		cb_binding vs_bound[tracked_slots]; // render thread: the game's VS bindings, slots 0..3
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
			int palette{ -1 }; // index into probe_palettes
		};
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
			if (vs_bound[0].buffer && vs_bound[0].count == palette_constants && read_block)
			{
				const std::size_t at = probe_palettes.size();
				probe_palettes.resize(at + palette_constants * 4);
				if (read_block(vs_bound[0].buffer, vs_bound[0].first, palette_constants, probe_palettes.data() + at))
				{
					d.palette = static_cast<int>(at / (palette_constants * 4));
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
				skinned += d.palette >= 0 ? 1 : 0;
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
		bool request_active{};
		float request_matrix[12]{};
		ULONGLONG request_at{};
		constexpr ULONGLONG request_stale_ms = 200;
		bool frame_copy{}; // render thread
		float frame_matrix[12]{};
		// Which draws hold the tagged bones, by (index buffer, start, count): a palette is read in full
		// only for these, and once for every other mesh until the table is cleared (every few seconds,
		// so a changed weapon is found again).
		struct known_mesh
		{
			ID3D11Buffer* index_buffer{};
			UINT start{};
			UINT count{};
			bool tagged{};
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

		// The draw just issued, once more with our palette in VS slot 0; the game's binding is restored.
		void draw_with_palette(ID3D11DeviceContext* context, const float* palette, UINT index_count, UINT start_index, INT base_vertex)
		{
			if (!draw_original || !vs_bind_original || !ensure_copy_buffer(context))
			{
				return;
			}
			constexpr UINT bytes = palette_constants * 16;
			if (copy_offset + bytes > copy_buffer_size)
			{
				++copy_overflows;
				return;
			}
			D3D11_MAPPED_SUBRESOURCE mapped{};
			const D3D11_MAP type = copy_discarded ? D3D11_MAP_WRITE_NO_OVERWRITE : D3D11_MAP_WRITE_DISCARD;
			if (FAILED(context->Map(copy_buffer.Get(), 0, type, 0, &mapped)))
			{
				return;
			}
			copy_discarded = true;
			std::memcpy(static_cast<std::uint8_t*>(mapped.pData) + copy_offset, palette, bytes);
			context->Unmap(copy_buffer.Get(), 0);
			ID3D11Buffer* game_buffer{};
			UINT game_first{}, game_count{};
			copy_context->VSGetConstantBuffers1(0, 1, &game_buffer, &game_first, &game_count);
			ID3D11Buffer* const ours = copy_buffer.Get();
			const UINT first = copy_offset / 16;
			const UINT count = palette_constants;
			copy_offset += bytes;
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

	void set_entry_points(draw_indexed_fn draw_indexed, set_constant_buffers1_fn vs_set_constant_buffers1, read_block_fn read)
	{
		draw_original = draw_indexed;
		vs_bind_original = vs_set_constant_buffers1;
		read_block = read;
	}

	void begin_render(int eye)
	{
		render_eye = eye;
		copy_offset = 0;
		copy_discarded = false;
		{
			const std::lock_guard guard(request_lock);
			frame_copy = request_active && GetTickCount64() - request_at < request_stale_ms;
			std::memcpy(frame_matrix, request_matrix, sizeof(frame_matrix));
		}
		if (const ULONGLONG now = GetTickCount64(); now - known_cleared > known_lifetime_ms)
		{
			known_cleared = now;
			unsigned tagged = 0;
			for (const known_mesh& mesh : known_meshes)
			{
				tagged += mesh.tagged ? 1 : 0;
			}
			tagged_meshes = tagged;
			known_meshes.clear();
		}
		if (eye == 0 && probe_requested.exchange(false))
		{
			probe_draws.clear();
			probe_palettes.clear();
			probe_index = 0;
			probing = true;
		}
	}

	void end_render()
	{
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

	void after_draw(ID3D11DeviceContext* context, UINT index_count, UINT start_index, INT base_vertex, UINT instances)
	{
		if (probing && render_eye == 0 && probe_draws.size() < 5000)
		{
			record(context, index_count, start_index, base_vertex, instances);
		}
		if (frame_copy && render_eye >= 0 && instances == 1 && vs_bound[0].buffer && vs_bound[0].count == palette_constants && read_block)
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
			if (known && !known->tagged)
			{
				return;
			}
			alignas(16) static float palette_data[palette_constants * 4];
			if (!read_block(vs_bound[0].buffer, vs_bound[0].first, palette_constants, palette_data))
			{
				return;
			}
			++palettes_read;
			bool any = false;
			for (int e = 0; e < palette::entries && !any; ++e)
			{
				any = palette::tagged(palette_data + e * palette::floats_per_entry, magazine_tag);
			}
			if (!known && known_meshes.size() < 512)
			{
				known_meshes.push_back({ ib.Get(), start_index, index_count, any });
			}
			else if (known)
			{
				known->tagged = any;
			}
			if (any && palette::move_tagged(palette_data, frame_matrix, magazine_tag) > 0)
			{
				draw_with_palette(context, palette_data, index_count, start_index, base_vertex);
				++magazine_copies;
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
		return text;
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
