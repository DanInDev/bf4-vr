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
#include "common/settings_store.hpp"
#include "hooks/game_window.hpp"
#include "hooks/swap_chain_size.hpp"
#include "input/gamepad.hpp"
#include "input/vr_controls.hpp"
#include "safety/mp_guard.hpp"
#include "control/frame_trace.hpp"
#include "control/thread_stacks.hpp"
#include "control/watchpoint.hpp"
#include "stereo/camera_hook.hpp"
#include "stereo/eye_pass.hpp"
#include "stereo/hud_capture.hpp"
#include "stereo/tracking.hpp"
#include "stereo/world_pass.hpp"
#include "weapon/physical_grenade.hpp"
#include "weapon/physical_reload.hpp"
#include "weapon/weapon_swap.hpp"

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
			bool hud{}; // the HUD layer texture (alpha kept) instead of the back buffer
			bool eye{}; // an eye pass image instead of the back buffer
			int eye_index{};
		};
		std::mutex shot_lock;
		shot_request shot;

		// Copies the back buffer (or the HUD layer texture) to CPU memory as BGRA; the back buffer's
		// alpha is not meaningful on screen and is made opaque. Runs on the present thread.
		std::string capture(IDXGISwapChain* swap_chain, bool hud, bool eye, int eye_index, UINT& width, UINT& height, std::vector<std::uint8_t>& bgra)
		{
			ComPtr<ID3D11Texture2D> back_buffer;
			ComPtr<ID3D11Device> device;
			if (hud)
			{
				back_buffer = stereo::latest_hud().texture;
				if (!back_buffer)
				{
					return "no HUD texture (hud_layer = 0, or no frame captured yet)";
				}
			}
			if (eye)
			{
				back_buffer = stereo::eye_pass_image(eye_index);
				if (!back_buffer)
				{
					return "no eye image (eyepass off, or no frame rendered yet)";
				}
			}
			if ((!hud && !eye && FAILED(swap_chain->GetBuffer(0, IID_PPV_ARGS(&back_buffer)))) || FAILED(swap_chain->GetDevice(IID_PPV_ARGS(&device))))
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
					target[x * 4 + 3] = hud ? source[x * 4 + 3] : 0xFF;
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

		std::string take_screenshot(const std::string& path, bool hud = false, bool eye = false, int eye_index = 0)
		{
			if (path.empty())
			{
				return "error: usage: shot <path.png>";
			}

			{
				const std::lock_guard guard(shot_lock);
				shot = {};
				shot.hud = hud;
				shot.eye = eye;
				shot.eye_index = eye_index;
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
				"aspect", "bone", "eye", "eyealt", "fov", "view", "hand", "armaxes", "frametrace", "worldpass", "viewcount", "threads", "eyepass", "eyeshot", "eyeprobe", "fakehead", "stereorender", "eyepoison", "eyeskip", "eyeprobe2", "eyeshot2", "fakehand", "key" };
			for (const char* research : research_commands)
			{
				if (command == research && !config::research_enabled())
				{
					return "error: '" + command + "' needs research = 1 in bfvr.ini";
				}
			}
			if (command == "shot") return take_screenshot(argument);
			if (command == "hudshot") return take_screenshot(argument, true);
			if (command == "eyeshot") return take_screenshot(argument, false, true);
			if (command == "eyeshot2") return take_screenshot(argument, false, true, 1);
			if (command == "eyeskip")
			{
				unsigned from = 0, to = 0;
				sscanf_s(argument.c_str(), "%u %u", &from, &to);
				return stereo::set_eye_skip(from, to);
			}
			if (command == "eyepoison") return stereo::set_eye_poison(argument);
			if (command == "eyeprobe") return stereo::probe_eye_pass(argument);
			if (command == "eyeprobe2") return stereo::probe_eye_pass(argument, true);
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
				unsigned buttons = 0;
				float lt = 0.0f, rt = 0.0f;
				if (sscanf_s(argument.c_str(), "%f %f %f %f %u %x %f %f", &lx, &ly, &rx, &ry, &duration_ms, &buttons, &lt, &rt) < 4 || duration_ms > 10000)
				{
					return "error: usage: pad <lx> <ly> <rx> <ry> [ms <= 10000] [hex XINPUT_GAMEPAD buttons] [lt rt]  (axes -1..1, triggers 0..1)";
				}
				const auto axis = [](float value) { return static_cast<std::int16_t>(std::clamp(value, -1.0f, 1.0f) * 32767.0f); };
				input::gamepad_state pad{};
				pad.connected = true;
				pad.left_x = axis(lx);
				pad.left_y = axis(ly);
				pad.right_x = axis(rx);
				pad.right_y = axis(ry);
				pad.buttons = static_cast<std::uint16_t>(buttons);
				pad.left_trigger = static_cast<std::uint8_t>(std::clamp(lt, 0.0f, 1.0f) * 255.0f);
				pad.right_trigger = static_cast<std::uint8_t>(std::clamp(rt, 0.0f, 1.0f) * 255.0f);
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
			if (command == "rendersize") return hooks::render_size_report();
			if (command == "frametrace") return trace_frame(argument);
			if (command == "threads") return thread_stacks();
			if (command == "worldpass")
			{
				int passes = 1, target = 0;
				float shift = 0.0f;
				sscanf_s(argument.c_str(), "%d %f %d", &passes, &shift, &target);
				return stereo::set_world_pass(passes, shift, target);
			}
			if (command == "eyepass")
			{
				int mode = -1;
				float shift = 0.064f;
				sscanf_s(argument.c_str(), "%d %f", &mode, &shift);
				return stereo::set_eye_pass(mode, shift); // no argument = report only
			}
			if (command == "viewcount")
			{
				int count = 0, viewport = 0;
				float shift = 0.0f;
				sscanf_s(argument.c_str(), "%d %f %d", &count, &shift, &viewport);
				return stereo::set_view_count(count, shift, viewport); // count 0 = report only
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
			if (command == "arms") return body::report();
			if (command == "armik")
			{
				if (argument == "0" || argument == "1")
				{
					body::set_arm_ik(argument == "1");
				}
				return std::string("arm_ik=") + (body::arm_ik_enabled() ? "1" : "0");
			}
			if (command == "hand")
			{
				char side = 0;
				float position[3]{}, yaw = 0.0f, pitch = 0.0f;
				const int fields = sscanf_s(argument.c_str(), "%c %f %f %f %f %f", &side, 1u, &position[0], &position[1], &position[2], &yaw, &pitch);
				const int hand = side == 'l' ? 0 : side == 'r' ? 1 : -1;
				const bool off = argument.size() > 2 && argument.substr(2) == "off";
				if (hand < 0 || (!off && fields < 4))
				{
					return "error: usage: hand <l|r> <x> <y> <z> [yaw deg] [pitch deg]  (camera axes: x right, y up, -z forward) | hand <l|r> off";
				}
				body::set_hand_target(hand, !off, position, yaw, pitch);
				return off ? "hand target off" : "hand target set";
			}
			if (command == "armaxes")
			{
				float rotation[4]{};
				if (sscanf_s(argument.c_str(), "%f %f %f %f", &rotation[0], &rotation[1], &rotation[2], &rotation[3]) != 4)
				{
					return "error: usage: armaxes <qx> <qy> <qz> <qw>  (OpenXR camera axes -> CameraJoint axes)";
				}
				body::set_camera_axes(rotation);
				return "camera axes set";
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
			if (command == "pointer")
			{
				// Laser-pointer test without a headset: cursor at texture coordinates, button state.
				float u = 0.0f, v = 0.0f;
				int pressed = 0;
				if (sscanf_s(argument.c_str(), "%f %f %d", &u, &v, &pressed) < 2)
				{
					return "error: usage: pointer <u> <v> [0|1]  (0..1, 0 0 = top left)";
				}
				return hooks::point_game_cursor(u, v, pressed != 0) ? "ok" : "error: no game window or outside 0..1";
			}
			if (command == "key")
			{
				// Research: tap a key through the game's Raw Input keyboard (virtual-key code in hex).
				unsigned key = 0, hold_ms = 100;
				if (sscanf_s(argument.c_str(), "%x %u", &key, &hold_ms) < 1 || key == 0 || key > 0xff || hold_ms > 5000)
				{
					return "error: usage: key <hex virtual-key code> [ms <= 5000]";
				}
				if (!hooks::press_game_key(static_cast<unsigned short>(key), true))
				{
					return "error: no game window";
				}
				std::thread([key, hold_ms] {
					Sleep(hold_ms);
					hooks::press_game_key(static_cast<unsigned short>(key), false);
				}).detach();
				return "key " + argument + " sent; synthetic keys read so far " + std::to_string(hooks::synthetic_keys_read());
			}
			if (command == "setting") return settings::pipe_command(argument);
			if (command == "rightstick") return input::set_right_stick_mode(argument);
			if (command == "reload") return weapon::reload_pipe_command(argument);
			if (command == "grenade") return weapon::grenade_pipe_command(argument);
			if (command == "holster") return weapon::holster_pipe_command(argument);
			if (command == "stereorender") return stereo::set_stereo_render(argument);
			if (command == "fakehand")
			{
				// A fixed right controller (aim and grip) turned yaw/pitch degrees from straight ahead, 30 cm
				// in front of the head, for controller-aim tests without a headset; "fakehand off" clears it.
				stereo::hand_sample hands;
				float yaw = 0.0f, pitch = 0.0f;
				if (argument != "off" && sscanf_s(argument.c_str(), "%f %f", &yaw, &pitch) >= 1)
				{
					const float h = yaw * 3.14159265f / 360.0f, v = pitch * 3.14159265f / 360.0f;
					// yaw about +y, then pitch about +x
					const float q[4] = { std::cos(h) * std::sin(v), std::sin(h) * std::cos(v), -std::sin(h) * std::sin(v), std::cos(h) * std::cos(v) };
					hands.valid[1] = true;
					for (int i = 0; i < 4; ++i)
					{
						hands.aim[1].orientation[i] = q[i];
						hands.grip[1].orientation[i] = q[i];
					}
					hands.aim[1].position[0] = hands.grip[1].position[0] = 0.15f;
					hands.aim[1].position[1] = hands.grip[1].position[1] = -0.25f;
					hands.aim[1].position[2] = hands.grip[1].position[2] = -0.3f;
				}
				stereo::publish_hand_sample(hands);
				return hands.valid[1] ? "fakehand yaw=" + std::to_string(yaw) + " pitch=" + std::to_string(pitch) : "fakehand off";
			}
			if (command == "fakehead")
			{
				// A fixed head sample (eyes +-ipd/2, looking ahead, Quest-like FOVs) so stereo paths run
				// without a headset; "fakehead off" clears it. The XR thread overwrites it when tracking.
				stereo::head_sample head;
				float ipd = 0.0f;
				head.valid = argument != "off" && sscanf_s(argument.c_str(), "%f", &ipd) == 1 && ipd > 0.0f && ipd < 0.1f;
				head.eyes[0].position[0] = -0.5f * ipd;
				head.eyes[1].position[0] = 0.5f * ipd;
				head.fovs[0] = { -0.942f, 0.698f, 0.768f, -0.890f };
				head.fovs[1] = { -0.698f, 0.942f, 0.768f, -0.890f };
				stereo::publish_head_sample(head);
				return head.valid ? "fakehead ipd=" + std::to_string(ipd) : "fakehead off";
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
			if (command == "help") return "commands: status | shot <path.png> | hudshot <path.png> | dump <path.exe> | peek <hex address> <length> | peekmany <length> <hex address>... | poke <hex address> <hex bytes> | pad <lx> <ly> <rx> <ry> [ms] | tshift <slot> <hex offset> <m> | viewcrop [h v] | rendersize | frametrace <path.txt> | worldpass <1|2> [shift m] [0 module|1 renderer] | eyepass <flags 1 twice|2 shifted last|4 restore> [shift m] | eyeshot <path.png> | eyeprobe <prefix> | fakehead <ipd>|off | stereorender aer|pair | viewcount <1|2> [shift m] [viewport] | threads | gunscale [g] | arms | armik [0|1] | hand <l|r> <x y z> [yaw pitch] / off | armaxes <q> | bone <i> <dx> <dy> <dz> | aspect <w/h> | fakeyaw <deg> | pin <slot> <hex offset> <float> | watch <hex address> [length] [ms] | help";
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
		frame_trace_on_present(swap_chain);
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
			shot.error = capture(swap_chain, shot.hud, shot.eye, shot.eye_index, shot.width, shot.height, shot.bgra);
			shot.pending = false;
			SetEvent(shot.done);
		}
	}
}
