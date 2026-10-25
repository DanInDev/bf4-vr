#include "control/frame_trace.hpp"

#include <d3d11.h>
#include <intrin.h>

#include <atomic>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <fstream>
#include <mutex>
#include <vector>

#include <MinHook.h>

#include "common/log.hpp"

namespace bfvr::control
{
	namespace
	{
		// ID3D11DeviceContext vtable slots (d3d11.h order).
		enum slot : int
		{
			draw_indexed = 12, draw = 13, draw_indexed_instanced = 20, draw_instanced = 21,
			om_set_render_targets = 33, om_set_render_targets_uav = 34, draw_indexed_instanced_indirect = 39,
			draw_instanced_indirect = 40, dispatch = 41, dispatch_indirect = 42, rs_set_viewports = 44,
			copy_subresource_region = 46, copy_resource = 47, clear_render_target_view = 50,
			clear_depth_stencil_view = 53, resolve_subresource = 57, execute_command_list = 58,
		};

		struct event
		{
			const char* name{};
			void* caller{};
			DWORD thread{};
			std::string detail;
			void* stack[12]{};
			USHORT depth{};
		};

		std::mutex lock;
		std::vector<event> events;
		std::atomic<bool> recording{};
		std::atomic<int> state{}; // 0 idle, 1 armed (start at next Present), 2 recording (stop at next Present)
		HANDLE done{};
		std::string output_path;
		std::string result;
		std::vector<void*> targets;
		HMODULE own_module{};

		std::string describe(ID3D11Resource* resource)
		{
			if (!resource)
			{
				return "-";
			}
			char text[96];
			D3D11_RESOURCE_DIMENSION dimension{};
			resource->GetType(&dimension);
			if (dimension == D3D11_RESOURCE_DIMENSION_TEXTURE2D)
			{
				D3D11_TEXTURE2D_DESC desc{};
				static_cast<ID3D11Texture2D*>(resource)->GetDesc(&desc);
				std::snprintf(text, sizeof(text), "%p:%ux%u/f%d%s%s", static_cast<void*>(resource), desc.Width, desc.Height, desc.Format,
					desc.ArraySize > 1 ? "/array" : "", desc.SampleDesc.Count > 1 ? "/msaa" : "");
			}
			else
			{
				std::snprintf(text, sizeof(text), "%p:dim%d", static_cast<void*>(resource), dimension);
			}
			return text;
		}

		std::string describe(ID3D11View* view)
		{
			if (!view)
			{
				return "-";
			}
			ID3D11Resource* resource{};
			view->GetResource(&resource);
			std::string text = describe(resource);
			if (resource)
			{
				resource->Release();
			}
			return text;
		}

		void add(const char* name, void* caller, std::string detail = {})
		{
			if (!recording.load(std::memory_order_relaxed))
			{
				return;
			}
			HMODULE module{};
			if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
				static_cast<LPCWSTR>(caller), &module) && module == own_module)
			{
				detail += " [bfvr]";
			}
			event e{ name, caller, GetCurrentThreadId(), std::move(detail) };
			// Draws are folded in the output; bindings and clears carry the call chain.
			if (std::strncmp(name, "Draw", 4) != 0)
			{
				e.depth = RtlCaptureStackBackTrace(2, 12, e.stack, nullptr);
			}
			const std::lock_guard guard(lock);
			if (events.size() < 200000)
			{
				events.push_back(std::move(e));
			}
		}

		using draw_indexed_t = void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, UINT, UINT, INT);
		using draw_t = void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, UINT, UINT);
		using draw_indexed_instanced_t = void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, UINT, UINT, UINT, INT, UINT);
		using draw_instanced_t = void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, UINT, UINT, UINT, UINT);
		using om_set_render_targets_t = void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, UINT, ID3D11RenderTargetView* const*, ID3D11DepthStencilView*);
		using om_set_render_targets_uav_t = void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, UINT, ID3D11RenderTargetView* const*,
			ID3D11DepthStencilView*, UINT, UINT, ID3D11UnorderedAccessView* const*, const UINT*);
		using indirect_t = void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, ID3D11Buffer*, UINT);
		using dispatch_t = void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, UINT, UINT, UINT);
		using rs_set_viewports_t = void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, UINT, const D3D11_VIEWPORT*);
		using copy_subresource_region_t = void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, ID3D11Resource*, UINT, UINT, UINT, UINT,
			ID3D11Resource*, UINT, const D3D11_BOX*);
		using copy_resource_t = void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, ID3D11Resource*, ID3D11Resource*);
		using clear_render_target_view_t = void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, ID3D11RenderTargetView*, const FLOAT[4]);
		using clear_depth_stencil_view_t = void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, ID3D11DepthStencilView*, UINT, FLOAT, UINT8);
		using resolve_subresource_t = void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, ID3D11Resource*, UINT, ID3D11Resource*, UINT, DXGI_FORMAT);
		using execute_command_list_t = void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, ID3D11CommandList*, BOOL);

		draw_indexed_t original_draw_indexed{};
		draw_t original_draw{};
		draw_indexed_instanced_t original_draw_indexed_instanced{};
		draw_instanced_t original_draw_instanced{};
		om_set_render_targets_t original_om_set_render_targets{};
		om_set_render_targets_uav_t original_om_set_render_targets_uav{};
		indirect_t original_draw_indexed_instanced_indirect{};
		indirect_t original_draw_instanced_indirect{};
		dispatch_t original_dispatch{};
		indirect_t original_dispatch_indirect{};
		rs_set_viewports_t original_rs_set_viewports{};
		copy_subresource_region_t original_copy_subresource_region{};
		copy_resource_t original_copy_resource{};
		clear_render_target_view_t original_clear_render_target_view{};
		clear_depth_stencil_view_t original_clear_depth_stencil_view{};
		resolve_subresource_t original_resolve_subresource{};
		execute_command_list_t original_execute_command_list{};

		std::string targets_text(UINT count, ID3D11RenderTargetView* const* views, ID3D11DepthStencilView* depth)
		{
			std::string text = "rt[";
			for (UINT i = 0; i < count && views; ++i)
			{
				text += (i ? " " : "") + describe(views[i]);
			}
			return text + "] ds " + describe(depth);
		}

		void STDMETHODCALLTYPE draw_indexed_detour(ID3D11DeviceContext* c, UINT a, UINT b, INT d)
		{
			add("DrawIndexed", _ReturnAddress());
			original_draw_indexed(c, a, b, d);
		}
		void STDMETHODCALLTYPE draw_detour(ID3D11DeviceContext* c, UINT a, UINT b)
		{
			add("Draw", _ReturnAddress(), std::to_string(a));
			original_draw(c, a, b);
		}
		void STDMETHODCALLTYPE draw_indexed_instanced_detour(ID3D11DeviceContext* c, UINT a, UINT b, UINT d, INT e, UINT f)
		{
			add("DrawIndexedInstanced", _ReturnAddress(), std::to_string(b));
			original_draw_indexed_instanced(c, a, b, d, e, f);
		}
		void STDMETHODCALLTYPE draw_instanced_detour(ID3D11DeviceContext* c, UINT a, UINT b, UINT d, UINT e)
		{
			add("DrawInstanced", _ReturnAddress(), std::to_string(b));
			original_draw_instanced(c, a, b, d, e);
		}
		void STDMETHODCALLTYPE om_set_render_targets_detour(ID3D11DeviceContext* c, UINT n, ID3D11RenderTargetView* const* v, ID3D11DepthStencilView* d)
		{
			if (recording.load(std::memory_order_relaxed))
			{
				add("OMSetRenderTargets", _ReturnAddress(), targets_text(n, v, d));
			}
			original_om_set_render_targets(c, n, v, d);
		}
		void STDMETHODCALLTYPE om_set_render_targets_uav_detour(ID3D11DeviceContext* c, UINT n, ID3D11RenderTargetView* const* v,
			ID3D11DepthStencilView* d, UINT s, UINT u, ID3D11UnorderedAccessView* const* uav, const UINT* counts)
		{
			if (recording.load(std::memory_order_relaxed))
			{
				add("OMSetRenderTargetsAndUAVs", _ReturnAddress(),
					n == D3D11_KEEP_RENDER_TARGETS_AND_DEPTH_STENCIL ? "keep rt, uav " + std::to_string(u) : targets_text(n, v, d) + " uav " + std::to_string(u));
			}
			original_om_set_render_targets_uav(c, n, v, d, s, u, uav, counts);
		}
		void STDMETHODCALLTYPE draw_indexed_instanced_indirect_detour(ID3D11DeviceContext* c, ID3D11Buffer* b, UINT o)
		{
			add("DrawIndexedInstancedIndirect", _ReturnAddress());
			original_draw_indexed_instanced_indirect(c, b, o);
		}
		void STDMETHODCALLTYPE draw_instanced_indirect_detour(ID3D11DeviceContext* c, ID3D11Buffer* b, UINT o)
		{
			add("DrawInstancedIndirect", _ReturnAddress());
			original_draw_instanced_indirect(c, b, o);
		}
		void STDMETHODCALLTYPE dispatch_detour(ID3D11DeviceContext* c, UINT x, UINT y, UINT z)
		{
			add("Dispatch", _ReturnAddress(), std::to_string(x) + "x" + std::to_string(y) + "x" + std::to_string(z));
			original_dispatch(c, x, y, z);
		}
		void STDMETHODCALLTYPE dispatch_indirect_detour(ID3D11DeviceContext* c, ID3D11Buffer* b, UINT o)
		{
			add("DispatchIndirect", _ReturnAddress());
			original_dispatch_indirect(c, b, o);
		}
		void STDMETHODCALLTYPE rs_set_viewports_detour(ID3D11DeviceContext* c, UINT n, const D3D11_VIEWPORT* v)
		{
			if (recording.load(std::memory_order_relaxed) && n && v)
			{
				char text[96];
				std::snprintf(text, sizeof(text), "%u: %.0f,%.0f %.0fx%.0f", n, v->TopLeftX, v->TopLeftY, v->Width, v->Height);
				add("RSSetViewports", _ReturnAddress(), text);
			}
			original_rs_set_viewports(c, n, v);
		}
		void STDMETHODCALLTYPE copy_subresource_region_detour(ID3D11DeviceContext* c, ID3D11Resource* dst, UINT ds, UINT x, UINT y, UINT z,
			ID3D11Resource* src, UINT ss, const D3D11_BOX* box)
		{
			if (recording.load(std::memory_order_relaxed))
			{
				add("CopySubresourceRegion", _ReturnAddress(), describe(src) + " -> " + describe(dst));
			}
			original_copy_subresource_region(c, dst, ds, x, y, z, src, ss, box);
		}
		void STDMETHODCALLTYPE copy_resource_detour(ID3D11DeviceContext* c, ID3D11Resource* dst, ID3D11Resource* src)
		{
			if (recording.load(std::memory_order_relaxed))
			{
				add("CopyResource", _ReturnAddress(), describe(src) + " -> " + describe(dst));
			}
			original_copy_resource(c, dst, src);
		}
		void STDMETHODCALLTYPE clear_render_target_view_detour(ID3D11DeviceContext* c, ID3D11RenderTargetView* v, const FLOAT color[4])
		{
			if (recording.load(std::memory_order_relaxed))
			{
				char text[64];
				std::snprintf(text, sizeof(text), " (%.2f %.2f %.2f %.2f)", color[0], color[1], color[2], color[3]);
				add("ClearRenderTargetView", _ReturnAddress(), describe(v) + text);
			}
			original_clear_render_target_view(c, v, color);
		}
		void STDMETHODCALLTYPE clear_depth_stencil_view_detour(ID3D11DeviceContext* c, ID3D11DepthStencilView* v, UINT f, FLOAT d, UINT8 s)
		{
			if (recording.load(std::memory_order_relaxed))
			{
				char text[48];
				std::snprintf(text, sizeof(text), " flags=%u depth=%.2f stencil=%u", f, d, s);
				add("ClearDepthStencilView", _ReturnAddress(), describe(v) + text);
			}
			original_clear_depth_stencil_view(c, v, f, d, s);
		}
		void STDMETHODCALLTYPE resolve_subresource_detour(ID3D11DeviceContext* c, ID3D11Resource* dst, UINT ds, ID3D11Resource* src, UINT ss, DXGI_FORMAT f)
		{
			if (recording.load(std::memory_order_relaxed))
			{
				add("ResolveSubresource", _ReturnAddress(), describe(src) + " -> " + describe(dst));
			}
			original_resolve_subresource(c, dst, ds, src, ss, f);
		}
		void STDMETHODCALLTYPE execute_command_list_detour(ID3D11DeviceContext* c, ID3D11CommandList* l, BOOL restore)
		{
			add("ExecuteCommandList", _ReturnAddress());
			original_execute_command_list(c, l, restore);
		}

		bool create_hooks(ID3D11DeviceContext* context)
		{
			void** const vtable = *reinterpret_cast<void***>(context);
			const struct { int slot; void* detour; void** original; } table[] = {
				{ draw_indexed, &draw_indexed_detour, reinterpret_cast<void**>(&original_draw_indexed) },
				{ draw, &draw_detour, reinterpret_cast<void**>(&original_draw) },
				{ draw_indexed_instanced, &draw_indexed_instanced_detour, reinterpret_cast<void**>(&original_draw_indexed_instanced) },
				{ draw_instanced, &draw_instanced_detour, reinterpret_cast<void**>(&original_draw_instanced) },
				{ om_set_render_targets, &om_set_render_targets_detour, reinterpret_cast<void**>(&original_om_set_render_targets) },
				{ om_set_render_targets_uav, &om_set_render_targets_uav_detour, reinterpret_cast<void**>(&original_om_set_render_targets_uav) },
				{ draw_indexed_instanced_indirect, &draw_indexed_instanced_indirect_detour, reinterpret_cast<void**>(&original_draw_indexed_instanced_indirect) },
				{ draw_instanced_indirect, &draw_instanced_indirect_detour, reinterpret_cast<void**>(&original_draw_instanced_indirect) },
				{ dispatch, &dispatch_detour, reinterpret_cast<void**>(&original_dispatch) },
				{ dispatch_indirect, &dispatch_indirect_detour, reinterpret_cast<void**>(&original_dispatch_indirect) },
				{ rs_set_viewports, &rs_set_viewports_detour, reinterpret_cast<void**>(&original_rs_set_viewports) },
				{ copy_subresource_region, &copy_subresource_region_detour, reinterpret_cast<void**>(&original_copy_subresource_region) },
				{ copy_resource, &copy_resource_detour, reinterpret_cast<void**>(&original_copy_resource) },
				{ clear_render_target_view, &clear_render_target_view_detour, reinterpret_cast<void**>(&original_clear_render_target_view) },
				{ clear_depth_stencil_view, &clear_depth_stencil_view_detour, reinterpret_cast<void**>(&original_clear_depth_stencil_view) },
				{ resolve_subresource, &resolve_subresource_detour, reinterpret_cast<void**>(&original_resolve_subresource) },
				{ execute_command_list, &execute_command_list_detour, reinterpret_cast<void**>(&original_execute_command_list) },
			};
			for (const auto& entry : table)
			{
				const MH_STATUS status = MH_CreateHook(vtable[entry.slot], entry.detour, entry.original);
				if (status != MH_OK && status != MH_ERROR_ALREADY_CREATED)
				{
					log::write("frametrace: hooking slot %d failed: %s", entry.slot, MH_StatusToString(status));
					return false;
				}
				targets.push_back(vtable[entry.slot]);
			}
			return true;
		}

		void set_enabled(bool enabled)
		{
			for (void* target : targets)
			{
				enabled ? MH_EnableHook(target) : MH_DisableHook(target);
			}
		}

		void write_output()
		{
			std::ofstream file(output_path);
			if (!file)
			{
				result = "error: cannot write " + output_path;
				return;
			}
			const auto base = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
			file << "# one frame, Present to Present; caller = return address in the game (image base 0x"
				<< std::hex << base << std::dec << "); consecutive identical draws are folded\n";
			std::size_t draws = 0;
			for (std::size_t i = 0; i < events.size(); ++i)
			{
				const event& e = events[i];
				const bool is_draw = std::strncmp(e.name, "Draw", 4) == 0;
				if (is_draw)
				{
					std::size_t j = i;
					while (j + 1 < events.size() && events[j + 1].name == e.name && events[j + 1].caller == e.caller)
					{
						++j;
					}
					draws += j - i + 1;
					char line[160];
					std::snprintf(line, sizeof(line), "%5zu %-28s x%-5zu %p t%lu %s\n", i, e.name, j - i + 1, e.caller, e.thread, e.detail.c_str());
					file << line;
					i = j;
					continue;
				}
				char head[96];
				std::snprintf(head, sizeof(head), "%5zu %-28s       %p t%lu ", i, e.name, e.caller, e.thread);
				file << head << e.detail;
				if (e.depth)
				{
					file << "  <";
					for (USHORT k = 0; k < e.depth; ++k)
					{
						char frame[24];
						std::snprintf(frame, sizeof(frame), " %p", e.stack[k]);
						file << frame;
					}
				}
				file << "\n";
			}
			result = "ok " + output_path + " events=" + std::to_string(events.size()) + " draws=" + std::to_string(draws);
		}
	}

	std::string trace_frame(const std::string& path)
	{
		if (path.empty())
		{
			return "error: usage: frametrace <path.txt>";
		}
		if (state.load() != 0)
		{
			return "error: a trace is already running";
		}
		output_path = path;
		result.clear();
		done = CreateEventW(nullptr, TRUE, FALSE, nullptr);
		state = 1;
		const DWORD waited = WaitForSingleObject(done, 5000);
		CloseHandle(done);
		done = nullptr;
		if (waited != WAIT_OBJECT_0)
		{
			recording = false;
			set_enabled(false);
			state = 0;
			return "error: no complete frame within 5 s";
		}
		return result;
	}

	void frame_trace_on_present(IDXGISwapChain* swap_chain)
	{
		const int current = state.load();
		if (current == 1)
		{
			if (targets.empty())
			{
				GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
					reinterpret_cast<LPCWSTR>(&trace_frame), &own_module);
				ID3D11Device* device{};
				ID3D11DeviceContext* context{};
				if (SUCCEEDED(swap_chain->GetDevice(__uuidof(ID3D11Device), reinterpret_cast<void**>(&device))))
				{
					device->GetImmediateContext(&context);
					device->Release();
				}
				const bool created = context && create_hooks(context);
				if (context)
				{
					context->Release();
				}
				if (!created)
				{
					result = "error: could not hook the device context";
					state = 0;
					SetEvent(done);
					return;
				}
			}
			events.clear();
			set_enabled(true);
			recording = true;
			state = 2;
		}
		else if (current == 2)
		{
			recording = false;
			set_enabled(false);
			{
				const std::lock_guard guard(lock);
				write_output();
				events.clear();
				events.shrink_to_fit();
			}
			state = 0;
			SetEvent(done);
		}
	}
}
