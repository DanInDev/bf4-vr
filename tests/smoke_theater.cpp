// Smoke test for stage 1 without the game: a D3D11 window whose swap chain goes
// through the real Present hook and theater mode. With the headset connected, the
// colour-cycling window appears on a virtual screen in front of you.
//
// Usage: bfvr_smoke_theater.exe [seconds]   (default 20; closing the window also ends it)
// Exit code 0 when the hook ran; the log (bfvr.log beside the exe) has the XR details.

#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <wrl/client.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>

#include <Xinput.h>

#include "common/log.hpp"
#include "hooks/present_hook.hpp"
#include "hooks/xinput_hook.hpp"
#include "xr/theater.hpp"

using Microsoft::WRL::ComPtr;

namespace
{
	bfvr::xr::theater* theater{};
	unsigned long long hook_calls{};
	bool window_closed{};

	void on_present(IDXGISwapChain* swap_chain)
	{
		++hook_calls;
		theater->on_present(swap_chain);
	}

	LRESULT CALLBACK window_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam)
	{
		if (message == WM_CLOSE)
		{
			window_closed = true;
			return 0;
		}
		return DefWindowProcW(window, message, wparam, lparam);
	}

	HWND create_window()
	{
		WNDCLASSEXW window_class{};
		window_class.cbSize = sizeof(window_class);
		window_class.lpfnWndProc = window_proc;
		window_class.hInstance = GetModuleHandleW(nullptr);
		window_class.hCursor = LoadCursorW(nullptr, IDC_ARROW);
		window_class.lpszClassName = L"bfvr_smoke_theater";
		RegisterClassExW(&window_class);

		RECT rect{ 0, 0, 1280, 720 };
		AdjustWindowRect(&rect, WS_OVERLAPPEDWINDOW, FALSE);
		return CreateWindowExW(0, window_class.lpszClassName, L"bf-vr theater smoke test", WS_OVERLAPPEDWINDOW | WS_VISIBLE,
			CW_USEDEFAULT, CW_USEDEFAULT, rect.right - rect.left, rect.bottom - rect.top,
			nullptr, nullptr, window_class.hInstance, nullptr);
	}
}

int main(int argc, char** argv)
{
	const double seconds = argc > 1 ? std::atof(argv[1]) : 20.0;
	bfvr::log::write("smoke: start, %.0f s", seconds);

	const HWND window = create_window();
	if (!window)
	{
		std::printf("window creation failed\n");
		return 2;
	}

	DXGI_SWAP_CHAIN_DESC description{};
	description.BufferCount = 2;
	description.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	description.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	description.OutputWindow = window;
	description.SampleDesc.Count = 1;
	description.Windowed = TRUE;
	description.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;

	ComPtr<IDXGISwapChain> swap_chain;
	ComPtr<ID3D11Device> device;
	ComPtr<ID3D11DeviceContext> context;
	HRESULT result = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, nullptr, 0,
		D3D11_SDK_VERSION, &description, &swap_chain, &device, nullptr, &context);
	if (FAILED(result))
	{
		std::printf("D3D11 device creation failed: 0x%08lX\n", result);
		return 2;
	}

	ComPtr<ID3D11Texture2D> back_buffer;
	ComPtr<ID3D11RenderTargetView> target;
	swap_chain->GetBuffer(0, IID_PPV_ARGS(&back_buffer));
	device->CreateRenderTargetView(back_buffer.Get(), nullptr, &target);

	theater = new bfvr::xr::theater();
	if (!bfvr::hooks::install_present_hook(&on_present))
	{
		std::printf("Present hook installation failed (see bfvr.log)\n");
		return 1;
	}

	// Read the pad the way BF4 does: XInputGetState from xinput9_1_0.dll, through our hook.
	bfvr::hooks::install_xinput_hooks();
	using get_state_t = DWORD(WINAPI*)(DWORD, XINPUT_STATE*);
	const auto xinput_get_state = reinterpret_cast<get_state_t>(GetProcAddress(GetModuleHandleW(L"xinput9_1_0.dll"), "XInputGetState"));

	const ULONGLONG start = GetTickCount64();
	ULONGLONG last_report = start;
	while (!window_closed && (GetTickCount64() - start) < static_cast<ULONGLONG>(seconds * 1000.0))
	{
		MSG message;
		while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE))
		{
			TranslateMessage(&message);
			DispatchMessageW(&message);
		}

		const float t = static_cast<float>(GetTickCount64() - start) / 1000.0f;
		const float colour[4] = { 0.5f + 0.5f * std::sin(t), 0.5f + 0.5f * std::sin(t + 2.1f), 0.5f + 0.5f * std::sin(t + 4.2f), 1.0f };
		ID3D11RenderTargetView* targets[] = { target.Get() };
		context->OMSetRenderTargets(1, targets, nullptr);
		context->ClearRenderTargetView(target.Get(), colour);
		swap_chain->Present(1, 0);

		if (GetTickCount64() - last_report >= 2000)
		{
			last_report = GetTickCount64();
			const auto status = theater->status();
			XINPUT_STATE pad{};
			const DWORD pad_result = xinput_get_state ? xinput_get_state(0, &pad) : ERROR_DEVICE_NOT_CONNECTED;
			std::printf("t=%4.1fs hook_calls=%llu theater=%s submitted=%llu pad=%s buttons=%04X lt=%u rt=%u l=(%d,%d) r=(%d,%d)\n",
				t, hook_calls, status.phase, status.frames_submitted, pad_result == ERROR_SUCCESS ? "connected" : "none",
				pad.Gamepad.wButtons, pad.Gamepad.bLeftTrigger, pad.Gamepad.bRightTrigger,
				pad.Gamepad.sThumbLX, pad.Gamepad.sThumbLY, pad.Gamepad.sThumbRX, pad.Gamepad.sThumbRY);
		}
	}

	const auto status = theater->status();
	std::printf("done: hook_calls=%llu theater=%s presents=%llu submitted=%llu\n",
		hook_calls, status.phase, status.presents_seen, status.frames_submitted);
	bfvr::log::write("smoke: done, hook_calls=%llu theater=%s submitted=%llu", hook_calls, status.phase, status.frames_submitted);

	delete theater;
	return hook_calls > 0 ? 0 : 1;
}
