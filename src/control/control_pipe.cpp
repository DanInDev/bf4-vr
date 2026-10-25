#include "control/control_pipe.hpp"

#include <windows.h>
#include <d3d11.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <thread>
#include <vector>

#include "body/arm_pose.hpp"
#include "common/config.hpp"
#include "common/log.hpp"
#include "input/gamepad.hpp"
#include "safety/mp_guard.hpp"
#include "control/watchpoint.hpp"
#include "stereo/camera_hook.hpp"

using Microsoft::WRL::ComPtr;

namespace bfvr::control
{
	namespace
	{
		constexpr wchar_t pipe_name[] = L"\\\\.\\pipe\\bfvr";
		constexpr DWORD shot_timeout_ms = 3000;

		status_provider provider{};

		// Frame pacing, written by the present thread.
		std::atomic<double> frames_per_second{};
		std::atomic<UINT> back_buffer_width{};
		std::atomic<UINT> back_buffer_height{};
		std::atomic<int> back_buffer_format{};
		// Latest "pad" command; an older command's timer must not release a newer hold.
		std::atomic<std::uint64_t> pad_generation{};

		// One screenshot request at a time: the pipe thread asks, the present thread fills it.
		struct shot_request
		{
			bool pending{};
			HANDLE done{};
			std::string error;
			std::uint64_t copies_at_capture{};
			UINT width{};
			UINT height{};
			std::vector<std::uint8_t> bgra;
		};
		std::mutex shot_lock;
		shot_request shot;

		// Copies the back buffer to CPU memory as opaque BGRA. Runs on the present thread.
		std::string capture(IDXGISwapChain* swap_chain, UINT& width, UINT& height, std::vector<std::uint8_t>& bgra)
		{
			ComPtr<ID3D11Texture2D> back_buffer;
			ComPtr<ID3D11Device> device;
			if (FAILED(swap_chain->GetBuffer(0, IID_PPV_ARGS(&back_buffer))) || FAILED(swap_chain->GetDevice(IID_PPV_ARGS(&device))))
			{
				return "back buffer unavailable";
			}

			D3D11_TEXTURE2D_DESC desc{};
			back_buffer->GetDesc(&desc);
			bool swap_red_blue{};
			switch (desc.Format)
			{
			case DXGI_FORMAT_R8G8B8A8_TYPELESS:
			case DXGI_FORMAT_R8G8B8A8_UNORM:
			case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
				swap_red_blue = true;
				break;
			case DXGI_FORMAT_B8G8R8A8_TYPELESS:
			case DXGI_FORMAT_B8G8R8A8_UNORM:
			case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
				break;
			default:
				return "unsupported back buffer format " + std::to_string(desc.Format);
			}
			if (desc.SampleDesc.Count != 1)
			{
				return "multisampled back buffer";
			}

			D3D11_TEXTURE2D_DESC staging_desc = desc;
			staging_desc.Usage = D3D11_USAGE_STAGING;
			staging_desc.BindFlags = 0;
			staging_desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
			staging_desc.MiscFlags = 0;
			ComPtr<ID3D11Texture2D> staging;
			if (FAILED(device->CreateTexture2D(&staging_desc, nullptr, &staging)))
			{
				return "staging texture creation failed";
			}

			ComPtr<ID3D11DeviceContext> context;
			device->GetImmediateContext(&context);
			context->CopyResource(staging.Get(), back_buffer.Get());

			// Map waits for the GPU; acceptable for an occasional debug capture.
			D3D11_MAPPED_SUBRESOURCE mapped{};
			if (FAILED(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped)))
			{
				return "map failed";
			}

			width = desc.Width;
			height = desc.Height;
			bgra.resize(static_cast<std::size_t>(width) * height * 4);
			for (UINT y = 0; y < height; ++y)
			{
				const auto* source = static_cast<const std::uint8_t*>(mapped.pData) + static_cast<std::size_t>(y) * mapped.RowPitch;
				std::uint8_t* target = bgra.data() + static_cast<std::size_t>(y) * width * 4;
				for (UINT x = 0; x < width; ++x)
				{
					target[x * 4 + 0] = source[x * 4 + (swap_red_blue ? 2 : 0)];
					target[x * 4 + 1] = source[x * 4 + 1];
					target[x * 4 + 2] = source[x * 4 + (swap_red_blue ? 0 : 2)];
					target[x * 4 + 3] = 0xFF; // the game's alpha is not meaningful on screen
				}
			}
			context->Unmap(staging.Get(), 0);
			return {};
		}

		std::string write_png(const std::wstring& path, UINT width, UINT height, std::vector<std::uint8_t>& bgra)
		{
			ComPtr<IWICImagingFactory> factory;
			ComPtr<IWICStream> stream;
			ComPtr<IWICBitmapEncoder> encoder;
			ComPtr<IWICBitmapFrameEncode> frame;
			WICPixelFormatGUID format = GUID_WICPixelFormat32bppBGRA;
			const UINT stride = width * 4;
			const HRESULT result = [&]
			{
				HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory));
				if (SUCCEEDED(hr)) hr = factory->CreateStream(&stream);
				if (SUCCEEDED(hr)) hr = stream->InitializeFromFilename(path.c_str(), GENERIC_WRITE);
				if (SUCCEEDED(hr)) hr = factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder);
				if (SUCCEEDED(hr)) hr = encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache);
				if (SUCCEEDED(hr)) hr = encoder->CreateNewFrame(&frame, nullptr);
				if (SUCCEEDED(hr)) hr = frame->Initialize(nullptr);
				if (SUCCEEDED(hr)) hr = frame->SetSize(width, height);
				if (SUCCEEDED(hr)) hr = frame->SetPixelFormat(&format);
				if (SUCCEEDED(hr)) hr = frame->WritePixels(height, stride, static_cast<UINT>(bgra.size()), bgra.data());
				if (SUCCEEDED(hr)) hr = frame->Commit();
				if (SUCCEEDED(hr)) hr = encoder->Commit();
				return hr;
			}();

			if (FAILED(result))
			{
				char text[64];
				std::snprintf(text, sizeof(text), "PNG encoding failed: hr=0x%08lX", result);
				return text;
			}
			return {};
		}

		std::wstring widen(const std::string& text)
		{
			const int length = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, nullptr, 0);
			std::wstring wide(length > 0 ? length - 1 : 0, L'\0');
			if (length > 1)
			{
				MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, wide.data(), length);
			}
			return wide;
		}

		std::string take_screenshot(const std::string& path)
		{
			if (path.empty())
			{
				return "error: usage: shot <path.png>";
			}

			{
				const std::lock_guard guard(shot_lock);
				shot = {};
				shot.pending = true;
				shot.done = CreateEventW(nullptr, TRUE, FALSE, nullptr);
			}
			const DWORD waited = WaitForSingleObject(shot.done, shot_timeout_ms);

			const std::lock_guard guard(shot_lock);
			CloseHandle(shot.done);
			shot.done = nullptr;
			if (waited != WAIT_OBJECT_0)
			{
				shot.pending = false;
				return "error: no frame presented within 3 s";
			}
			if (!shot.error.empty())
			{
				return "error: " + shot.error;
			}

			const std::string error = write_png(widen(path), shot.width, shot.height, shot.bgra);
			shot.bgra = {};
			return error.empty() ? "ok " + path + " copies_at_capture=" + std::to_string(shot.copies_at_capture) : "error: " + error;
		}

		// Writes the game's in-memory (decrypted) executable image as a PE file for local static
		// analysis. Section offsets are rewritten to the memory layout so Ghidra maps it 1:1.
		// Never share the output (see docs/SAFETY.md).
		std::string dump_image(const std::string& path)
		{
			if (path.empty())
			{
				return "error: usage: dump <path.exe>";
			}

			auto* const base = reinterpret_cast<std::uint8_t*>(GetModuleHandleW(nullptr));
			const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
			const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
			const SIZE_T size = nt->OptionalHeader.SizeOfImage;

			// Page-wise reads: unreadable pages (guard/no-access) become zeros instead of a crash.
			std::vector<std::uint8_t> image(size, 0);
			std::size_t unreadable_pages = 0;
			constexpr SIZE_T page = 4096;
			for (SIZE_T offset = 0; offset < size; offset += page)
			{
				SIZE_T read = 0;
				if (!ReadProcessMemory(GetCurrentProcess(), base + offset, image.data() + offset, std::min(page, size - offset), &read))
				{
					++unreadable_pages;
				}
			}

			auto* headers = reinterpret_cast<IMAGE_NT_HEADERS*>(image.data() + dos->e_lfanew);
			headers->OptionalHeader.FileAlignment = headers->OptionalHeader.SectionAlignment;
			IMAGE_SECTION_HEADER* section = IMAGE_FIRST_SECTION(headers);
			for (WORD i = 0; i < headers->FileHeader.NumberOfSections; ++i, ++section)
			{
				section->PointerToRawData = section->VirtualAddress;
				section->SizeOfRawData = std::min<DWORD>(section->Misc.VirtualSize, static_cast<DWORD>(size - section->VirtualAddress));
			}

			const HANDLE file = CreateFileW(widen(path).c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
			if (file == INVALID_HANDLE_VALUE)
			{
				return "error: cannot create " + path;
			}
			DWORD written = 0;
			const BOOL ok = WriteFile(file, image.data(), static_cast<DWORD>(image.size()), &written, nullptr);
			CloseHandle(file);

			char text[160];
			std::snprintf(text, sizeof(text), " base=%p size=%zu unreadable_pages=%zu", static_cast<void*>(base),
				static_cast<std::size_t>(size), unreadable_pages);
			log::write("control: image dumped to %s%s", path.c_str(), text);
			return ok ? "ok " + path + text : "error: write failed";
		}

		// Reads game memory for structure research: "peek <hex address> <length>" -> hex bytes.
		// ReadProcessMemory on our own process fails cleanly on unmapped addresses instead of crashing.
		std::string peek(const std::string& argument)
		{
			unsigned long long address = 0;
			unsigned length = 0;
			if (sscanf_s(argument.c_str(), "%llx %u", &address, &length) != 2 || length == 0 || length > 65536)
			{
				return "error: usage: peek <hex address> <length 1..65536>";
			}

			std::vector<std::uint8_t> bytes(length);
			SIZE_T read = 0;
			if (!ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<LPCVOID>(address), bytes.data(), length, &read) || read != length)
			{
				return "error: unreadable";
			}

			std::string hex;
			hex.reserve(length * 2);
			constexpr char digits[] = "0123456789abcdef";
			for (const std::uint8_t byte : bytes)
			{
				hex += digits[byte >> 4];
				hex += digits[byte & 0xF];
			}
			return hex;
		}

		// Many reads in one round trip: "peekmany <length> <hex address>..." -> one line per address,
		// hex bytes or "-" when unreadable. Each pipe connection costs ~20 ms, so scans batch here.
		std::string peek_many(const std::string& argument)
		{
			unsigned length = 0;
			int consumed = 0;
			if (sscanf_s(argument.c_str(), "%u%n", &length, &consumed) != 1 || length == 0 || length > 4096)
			{
				return "error: usage: peekmany <length 1..4096> <hex address>...";
			}
			std::string reply;
			std::vector<std::uint8_t> bytes(length);
			constexpr char digits[] = "0123456789abcdef";
			const char* cursor = argument.c_str() + consumed;
			unsigned long long address = 0;
			int advance = 0;
			while (sscanf_s(cursor, " %llx%n", &address, &advance) == 1)
			{
				cursor += advance;
				SIZE_T read = 0;
				if (ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<LPCVOID>(address), bytes.data(), length, &read) && read == length)
				{
					for (const std::uint8_t byte : bytes)
					{
						reply += digits[byte >> 4];
						reply += digits[byte & 0xF];
					}
				}
				else
				{
					reply += '-';
				}
				reply += '\n';
			}
			return reply;
		}

		// Writes game data for structure research: "poke <hex address> <hex bytes>" (at most 64 bytes).
		// Only pages that are already writable, so code and read-only data are never patched.
		std::string poke(const std::string& argument)
		{
			unsigned long long address = 0;
			char hex[129]{};
			if (sscanf_s(argument.c_str(), "%llx %128s", &address, hex, static_cast<unsigned>(sizeof(hex))) != 2
				|| std::strlen(hex) % 2 != 0)
			{
				return "error: usage: poke <hex address> <hex bytes, at most 64>";
			}
			std::vector<std::uint8_t> bytes;
			for (std::size_t i = 0; hex[i] != '\0'; i += 2)
			{
				unsigned value = 0;
				if (sscanf_s(hex + i, "%2x", &value) != 1)
				{
					return "error: bad hex bytes";
				}
				bytes.push_back(static_cast<std::uint8_t>(value));
			}

			MEMORY_BASIC_INFORMATION info{};
			const auto start = reinterpret_cast<std::uint8_t*>(address);
			if (!VirtualQuery(start, &info, sizeof(info)) || info.State != MEM_COMMIT
				|| !(info.Protect & (PAGE_READWRITE | PAGE_WRITECOPY)) || (info.Protect & PAGE_GUARD)
				|| start + bytes.size() > static_cast<std::uint8_t*>(info.BaseAddress) + info.RegionSize)
			{
				return "error: not a writable data range";
			}
			std::memcpy(start, bytes.data(), bytes.size());
			return "ok " + std::to_string(bytes.size()) + " bytes";
		}

		std::string status()
		{
			char text[256];
			std::snprintf(text, sizeof(text), "fps=%.1f back_buffer=%ux%u format=%d\n", frames_per_second.load(),
				back_buffer_width.load(), back_buffer_height.load(), back_buffer_format.load());
			return text + (provider ? provider() : std::string());
		}

		std::string handle(const std::string& line)
		{
			const auto space = line.find(' ');
			const std::string command = line.substr(0, space);
			const std::string argument = space == std::string::npos ? std::string() : line.substr(space + 1);

			if (command == "status") return status();
			if (safety::mod_disabled()) return "error: mod disabled (not a campaign level)";
			// Memory research and test overrides: off in release builds unless research = 1.
			static constexpr const char* research_commands[] = { "dump", "poke", "watch", "pad", "tshift", "pin", "fakeyaw",
				"aspect", "bone", "eye", "eyealt", "fov", "view" };
			for (const char* research : research_commands)
			{
				if (command == research && !config::research_enabled())
				{
					return "error: '" + command + "' needs research = 1 in bfvr.ini";
				}
			}
			if (command == "shot") return take_screenshot(argument);
			if (command == "dump") return dump_image(argument);
			if (command == "peek") return peek(argument);
			if (command == "peekmany") return peek_many(argument);
			if (command == "poke") return poke(argument);
			if (command == "pad")
			{
				// Holds the virtual pad's sticks for a while (automation without a headset session,
				// which would overwrite them every frame). Needs XInput polling, i.e. a physical pad.
				float lx = 0.0f, ly = 0.0f, rx = 0.0f, ry = 0.0f;
				unsigned duration_ms = 500;
				if (sscanf_s(argument.c_str(), "%f %f %f %f %u", &lx, &ly, &rx, &ry, &duration_ms) < 4 || duration_ms > 10000)
				{
					return "error: usage: pad <lx> <ly> <rx> <ry> [ms <= 10000]  (axes -1..1)";
				}
				const auto axis = [](float value) { return static_cast<std::int16_t>(std::clamp(value, -1.0f, 1.0f) * 32767.0f); };
				input::gamepad_state pad{};
				pad.connected = true;
				pad.left_x = axis(lx);
				pad.left_y = axis(ly);
				pad.right_x = axis(rx);
				pad.right_y = axis(ry);
				const std::uint64_t generation = ++pad_generation;
				input::publish(pad);
				std::thread([generation, duration_ms] {
					Sleep(duration_ms);
					if (pad_generation == generation)
					{
						input::publish({});
					}
				}).detach();
				return "pad held for " + std::to_string(duration_ms) + " ms";
			}
			if (command == "view")
			{
				int view = -1;
				float fov = 0.0f, shift = 0.0f;
				if (sscanf_s(argument.c_str(), "%d %f %f", &view, &fov, &shift) < 1 || view < 0 || view > 2)
				{
					return "error: usage: view <0|1|2> <fov rad, 0=off> <shift m, 0=off>";
				}
				stereo::set_view_override(view, fov, shift);
				return "view " + std::to_string(view) + " fov=" + std::to_string(fov) + " shift=" + std::to_string(shift);
			}
			if (command == "pin")
			{
				int slot = -1;
				unsigned offset = 0;
				float value = 0.0f;
				if (sscanf_s(argument.c_str(), "%d %x %f", &slot, &offset, &value) < 2 || !stereo::set_pin(slot, offset, value))
				{
					return "error: usage: pin <slot 0..3> <hex offset in view params, 4-aligned, 0 = off> <float>";
				}
				return "pin " + std::to_string(slot) + " offset=" + std::to_string(offset) + " value=" + std::to_string(value);
			}
			if (command == "viewcrop")
			{
				float horizontal = 0.0f, vertical = 0.0f;
				if (sscanf_s(argument.c_str(), "%f %f", &horizontal, &vertical) == 2)
				{
					stereo::set_view_crop(horizontal, vertical);
				}
				stereo::current_view_crop(horizontal, vertical);
				return "view_crop=" + std::to_string(horizontal) + " " + std::to_string(vertical);
			}
			if (command == "gunscale")
			{
				float scale = 0.0f;
				if (!argument.empty() && sscanf_s(argument.c_str(), "%f", &scale) == 1)
				{
					stereo::set_gun_scale(scale);
				}
				return "gun_scale=" + std::to_string(stereo::current_gun_scale());
			}
			if (command == "bone")
			{
				int bone = -1;
				float dx = 0.0f, dy = 0.0f, dz = 0.0f;
				if (sscanf_s(argument.c_str(), "%d %f %f %f", &bone, &dx, &dy, &dz) < 1)
				{
					return "error: usage: bone <index, -1 = off> <dx> <dy> <dz>";
				}
				body::set_bone_offset(bone, dx, dy, dz);
				const body::arm_pose_stats seen = body::stats();
				return "bone " + std::to_string(bone) + " offset=(" + std::to_string(dx) + "," + std::to_string(dy) + "," + std::to_string(dz)
					+ ") conversions=" + std::to_string(seen.conversions) + " matched=" + std::to_string(seen.matched);
			}
			if (command == "aspect")
			{
				float value = 0.0f;
				sscanf_s(argument.c_str(), "%f", &value);
				stereo::set_research_aspect(value);
				return "research_aspect=" + std::to_string(value);
			}
			if (command == "fakeyaw")
			{
				float degrees = 0.0f;
				sscanf_s(argument.c_str(), "%f", &degrees);
				stereo::set_fake_yaw(degrees);
				return "fake_yaw=" + std::to_string(degrees);
			}
			if (command == "tshift")
			{
				int slot = -1;
				unsigned offset = 0;
				float metres = 0.0f;
				if (sscanf_s(argument.c_str(), "%d %x %f", &slot, &offset, &metres) != 3
					|| !stereo::set_transform_shift(slot, offset, metres))
				{
					return "error: usage: tshift <slot 0..3> <hex offset in view params, 16-aligned, <= 10c0> <metres, 0 = off>";
				}
				return "tshift " + std::to_string(slot) + " offset=" + std::to_string(offset) + " metres=" + std::to_string(metres);
			}
			if (command == "fov")
			{
				float radians = 0.0f;
				sscanf_s(argument.c_str(), "%f", &radians);
				stereo::set_fov_override(radians);
				return "fov_override=" + std::to_string(radians);
			}
			if (command == "eyealt")
			{
				float amplitude = 0.0f;
				sscanf_s(argument.c_str(), "%f", &amplitude);
				stereo::set_alternating_offset(amplitude);
				return "eye_alternating=" + std::to_string(amplitude);
			}
			if (command == "eye")
			{
				float metres = 0.0f;
				if (!argument.empty() && sscanf_s(argument.c_str(), "%f", &metres) == 1)
				{
					stereo::set_eye_offset(metres);
				}
				char text[48];
				std::snprintf(text, sizeof(text), "eye_offset=%.4f", stereo::eye_offset());
				return text;
			}
			if (command == "watch")
			{
				unsigned long long address = 0;
				unsigned length = 8;
				unsigned duration_ms = 1000;
				if (sscanf_s(argument.c_str(), "%llx %u %u", &address, &length, &duration_ms) < 1 || duration_ms > 10000)
				{
					return "error: usage: watch <hex address> [length 1|2|4|8] [ms <= 10000]";
				}
				return watch_writes(static_cast<std::uintptr_t>(address), length, duration_ms);
			}
			if (command == "help") return "commands: status | shot <path.png> | dump <path.exe> | peek <hex address> <length> | peekmany <length> <hex address>... | poke <hex address> <hex bytes> | pad <lx> <ly> <rx> <ry> [ms] | tshift <slot> <hex offset> <m> | viewcrop [h v] | gunscale [g] | bone <i> <dx> <dy> <dz> | aspect <w/h> | fakeyaw <deg> | pin <slot> <hex offset> <float> | watch <hex address> [length] [ms] | help";
			return "error: unknown command '" + command + "' (try help)";
		}

		DWORD WINAPI server_thread(LPVOID)
		{
			// WIC needs COM on the thread that encodes screenshots.
			CoInitializeEx(nullptr, COINIT_MULTITHREADED);
			log::write("control: listening on \\\\.\\pipe\\bfvr");

			for (;;)
			{
				const HANDLE pipe = CreateNamedPipeW(pipe_name, PIPE_ACCESS_DUPLEX,
					PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS, 1, 64 * 1024, 64 * 1024, 0, nullptr);
				if (pipe == INVALID_HANDLE_VALUE)
				{
					log::write("control: CreateNamedPipe failed: %lu", GetLastError());
					return 1;
				}

				if (ConnectNamedPipe(pipe, nullptr) || GetLastError() == ERROR_PIPE_CONNECTED)
				{
					// Large enough for a peekmany batch of a few thousand addresses.
					std::vector<char> buffer(64 * 1024);
					DWORD read{};
					if (ReadFile(pipe, buffer.data(), static_cast<DWORD>(buffer.size()), &read, nullptr))
					{
						std::string line(buffer.data(), read);
						line.erase(line.find_last_not_of("\r\n ") + 1);
						const std::string reply = handle(line) + "\n";
						DWORD written{};
						WriteFile(pipe, reply.data(), static_cast<DWORD>(reply.size()), &written, nullptr);
						FlushFileBuffers(pipe);
					}
				}
				DisconnectNamedPipe(pipe);
				CloseHandle(pipe);
			}
		}
	}

	void start(status_provider status_source)
	{
		provider = status_source;
		const HANDLE thread = CreateThread(nullptr, 0, &server_thread, nullptr, 0, nullptr);
		if (thread)
		{
			CloseHandle(thread);
		}
	}

	void on_present(IDXGISwapChain* swap_chain)
	{
		static LARGE_INTEGER frequency{};
		static LARGE_INTEGER window_start{};
		static unsigned frames{};
		LARGE_INTEGER now{};
		QueryPerformanceCounter(&now);
		if (!frequency.QuadPart)
		{
			QueryPerformanceFrequency(&frequency);
			window_start = now;
		}
		++frames;
		const double elapsed = static_cast<double>(now.QuadPart - window_start.QuadPart) / static_cast<double>(frequency.QuadPart);
		if (elapsed >= 1.0)
		{
			frames_per_second = frames / elapsed;
			frames = 0;
			window_start = now;

			DXGI_SWAP_CHAIN_DESC desc{};
			if (SUCCEEDED(swap_chain->GetDesc(&desc)))
			{
				back_buffer_width = desc.BufferDesc.Width;
				back_buffer_height = desc.BufferDesc.Height;
				back_buffer_format = desc.BufferDesc.Format;
			}
		}

		const std::lock_guard guard(shot_lock);
		if (shot.pending)
		{
			shot.copies_at_capture = stereo::stats().copies;
			shot.error = capture(swap_chain, shot.width, shot.height, shot.bgra);
			shot.pending = false;
			SetEvent(shot.done);
		}
	}
}
