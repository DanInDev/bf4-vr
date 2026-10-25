#include "stereo/eye_pass.hpp"

#include <windows.h>
#include <wrl/client.h>

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <mutex>

#include <MinHook.h>

#include "common/log.hpp"
#include "safety/mp_guard.hpp"
#include "stereo/eye_math.hpp"

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

		// Render thread, after the first eye's tonemap into the back buffer.
		void copy_first_eye()
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
			if (image)
			{
				image->GetDesc(&existing);
			}
			if (!image || existing.Width != desc.Width || existing.Height != desc.Height || existing.Format != desc.Format)
			{
				desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
				desc.MiscFlags = 0;
				desc.CPUAccessFlags = 0;
				desc.Usage = D3D11_USAGE_DEFAULT;
				image.Reset();
				if (FAILED(device->CreateTexture2D(&desc, nullptr, &image)))
				{
					return;
				}
			}
			ComPtr<ID3D11DeviceContext> context;
			device->GetImmediateContext(&context);
			context->CopyResource(image.Get(), source);
		}

		void __fastcall per_view_render_detour(void* renderer, std::uint8_t* slot, void* context)
		{
			const int flags = mode.load(std::memory_order_relaxed);
			if (!(flags & 1) || !slot || safety::mod_disabled() || !safety::campaign_level_loaded())
			{
				original_per_view_render(renderer, slot, context);
				return;
			}
			const std::uint64_t frame = ++frames;
			const std::uint64_t abandoned_before = pair.abandoned();
			pair.begin(frame);
			abandoned += pair.abandoned() - abandoned_before;

			const bool shifted_last = (flags & 2) != 0;
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
			original_per_view_render(renderer, slot, context);
			pair.rendered(0, frame);
			copy_first_eye();

			if (restore)
			{
				std::memcpy(slot, snapshot.get(), slot_size);
			}
			else if (!shifted_last)
			{
				shift_slot(slot, -metres);
			}
			if (shifted_last)
			{
				shift_slot(slot, metres);
			}

			LARGE_INTEGER start{}, end{}, frequency{};
			QueryPerformanceCounter(&start);
			original_per_view_render(renderer, slot, context);
			QueryPerformanceCounter(&end);
			QueryPerformanceFrequency(&frequency);
			extra_ms = static_cast<float>(1000.0 * static_cast<double>(end.QuadPart - start.QuadPart) / static_cast<double>(frequency.QuadPart));

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
			mode = new_mode & 7;
		}
		return "eyepass mode=" + std::to_string(mode.load()) + " shift=" + std::to_string(shift.load()) + " " + eye_pass_report();
	}

	std::string eye_pass_report()
	{
		char text[192];
		std::snprintf(text, sizeof(text), "eye_frames=%llu eye_pairs=%llu eye_rejected=%llu eye_abandoned=%llu eye_ms=%.2f",
			frames.load(), pairs.load(), rejected.load(), abandoned.load(), extra_ms.load());
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

	ID3D11Texture2D* eye_pass_image()
	{
		const std::lock_guard guard(image_lock);
		return image.Get();
	}
}
