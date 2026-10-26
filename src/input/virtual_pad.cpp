#include "input/virtual_pad.hpp"

#define DIRECTINPUT_VERSION 0x0800
#include <windows.h>
#include <objbase.h>
#include <dinput.h>

#include <atomic>
#include <cstdint>
#include <cstring>
#include <mutex>

#include <MinHook.h>

#include "common/config.hpp"
#include "common/log.hpp"
#include "safety/mp_guard.hpp"

namespace bfvr::input
{
	namespace
	{
		// Made up; no real device uses them. Only our wrapper and the IsXInputDevice hook know them.
		constexpr GUID fake_product = { 0x6e1c1f2a, 0x9b4d, 0x4c3e, { 0xa7, 0xf1, 0xb4, 0xf0, 0xe5, 0xd2, 0xc8, 0xa1 } };
		constexpr GUID fake_instance = { 0x0d5b7a93, 0x3e21, 0x4f6c, { 0x8a, 0x4b, 0x2c, 0x9e, 0x71, 0xf0, 0xb6, 0xd4 } };
		constexpr const char* fake_name = "bf-vr virtual pad";
		constexpr int max_logged_devices = 16;

		std::atomic<std::uint64_t> offered{};
		std::atomic<std::uint64_t> xinput_answers{};

		bool is_fake(REFGUID guid)
		{
			return IsEqualGUID(guid, fake_instance) || IsEqualGUID(guid, fake_product);
		}

		bool enabled()
		{
			static const bool value = []
			{
				if (!config::get_bool("virtual_pad", true))
				{
					log::write("virtual pad: disabled (virtual_pad=0)");
					return false;
				}
				// Campaign only: never wrap anything in a process started to join multiplayer.
				if (!safety::command_line_allows_mod())
				{
					return false;
				}
				if (!config::get_bool("gamepad", true) || !config::get_bool("theater", true))
				{
					log::write("virtual pad: off because gamepad=0 or theater=0 (nothing would drive it)");
					return false;
				}
				return true;
			}();
			return value;
		}

#if defined(_M_X64)
		// BF4 179547: Microsoft's WMI-based IsXInputDevice(const GUID* product), called from the
		// EnumDevices callback 0x140946690 (only caller 0x1409466F5). Static analysis, see gamepad.md.
		constexpr std::uint64_t is_xinput_device = 0x14094a090;
		// mov [rsp+8],rbx; push rbp; push rsi; push rdi; push r12; push r13; push r14; push r15
		constexpr std::uint8_t is_xinput_prologue[] = { 0x48, 0x89, 0x5c, 0x24, 0x08, 0x55, 0x56, 0x57, 0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57 };

		// The result is passed through as the whole register, so it does not matter whether the
		// game reads a bool (al) or a BOOL (eax).
		using is_xinput_device_t = std::uintptr_t(__fastcall*)(const GUID* product);
		is_xinput_device_t original_is_xinput_device{};
		std::atomic<int> logged_answers{};

		std::uintptr_t __fastcall is_xinput_device_detour(const GUID* product)
		{
			if (product && IsEqualGUID(*product, fake_product))
			{
				++xinput_answers;
				log::write("virtual pad: IsXInputDevice(bf-vr virtual pad) answered TRUE");
				return 1;
			}
			const std::uintptr_t result = original_is_xinput_device(product);
			if (product && logged_answers++ < max_logged_devices)
			{
				log::write("virtual pad: IsXInputDevice(product %08lX) -> %u", product->Data1, static_cast<unsigned>(result & 0xff));
			}
			return result;
		}
#endif

		// Called from EnumDevices: the game is running its input initialisation, so its code is
		// decrypted by now and a mismatch means another build.
		bool ensure_is_xinput_hook()
		{
#if !defined(_M_X64)
			static std::once_flag logged;
			std::call_once(logged, [] { log::write("virtual pad: IsXInputDevice hook targets BF4 x64 only; fake device not offered"); });
			return false;
#else
			static std::mutex lock;
			static int state{}; // 0 untried, 1 installed, -1 failed
			const std::lock_guard guard(lock);
			if (state != 0)
			{
				return state > 0;
			}
			state = -1;

			auto* const target = reinterpret_cast<void*>(static_cast<std::uintptr_t>(is_xinput_device));
			std::uint8_t bytes[sizeof(is_xinput_prologue)]{};
			if (!ReadProcessMemory(GetCurrentProcess(), target, bytes, sizeof(bytes), nullptr)
				|| std::memcmp(bytes, is_xinput_prologue, sizeof(bytes)) != 0)
			{
				log::write("virtual pad: IsXInputDevice %p does not match build 179547; fake device not offered", target);
				return false;
			}
			MH_STATUS status = MH_Initialize();
			if (status == MH_OK || status == MH_ERROR_ALREADY_INITIALIZED)
			{
				status = MH_CreateHook(target, reinterpret_cast<void*>(&is_xinput_device_detour), reinterpret_cast<void**>(&original_is_xinput_device));
			}
			if (status == MH_OK)
			{
				status = MH_EnableHook(target);
			}
			log::write("virtual pad: IsXInputDevice hook at %p: %s", target, MH_StatusToString(status));
			if (status == MH_OK)
			{
				state = 1;
			}
			return state > 0;
#endif
		}

		DIDEVICEINSTANCEA fake_device()
		{
			DIDEVICEINSTANCEA device{};
			device.dwSize = sizeof(device);
			device.guidInstance = fake_instance;
			device.guidProduct = fake_product;
			device.dwDevType = DI8DEVTYPE_GAMEPAD | (DI8DEVTYPEGAMEPAD_STANDARD << 8) | DIDEVTYPE_HID;
			strcpy_s(device.tszInstanceName, fake_name);
			strcpy_s(device.tszProductName, fake_name);
			device.wUsagePage = 0x01; // HID generic desktop
			device.wUsage = 0x05;     // game pad
			return device;
		}

		struct enum_forward
		{
			LPDIENUMDEVICESCALLBACKA callback{};
			LPVOID context{};
			int devices{};
			bool stopped{};
		};

		BOOL CALLBACK forward_device(LPCDIDEVICEINSTANCEA device, LPVOID context)
		{
			auto* const forward = static_cast<enum_forward*>(context);
			if (device && forward->devices < max_logged_devices)
			{
				log::write("virtual pad: game enumerates '%s' type=0x%08lX product=%08lX", device->tszProductName, device->dwDevType, device->guidProduct.Data1);
			}
			++forward->devices;
			const BOOL result = forward->callback(device, forward->context);
			forward->stopped = result == DIENUM_STOP;
			return result;
		}

		// Forwards everything to the system object; only EnumDevices, CreateDevice and
		// GetDeviceStatus know about the fake device.
		class direct_input8a final : public IDirectInput8A
		{
		public:
			explicit direct_input8a(IDirectInput8A* real) : real_(real) {}

			HRESULT STDMETHODCALLTYPE QueryInterface(REFIID interface_id, LPVOID* object) override
			{
				if (!object)
				{
					return E_POINTER;
				}
				if (IsEqualIID(interface_id, IID_IUnknown) || IsEqualIID(interface_id, IID_IDirectInput8A))
				{
					*object = this;
					AddRef();
					return S_OK;
				}
				return real_->QueryInterface(interface_id, object);
			}

			ULONG STDMETHODCALLTYPE AddRef() override
			{
				return ++references_;
			}

			ULONG STDMETHODCALLTYPE Release() override
			{
				const ULONG left = --references_;
				if (left == 0)
				{
					real_->Release();
					delete this;
				}
				return left;
			}

			HRESULT STDMETHODCALLTYPE CreateDevice(REFGUID guid, LPDIRECTINPUTDEVICE8A* device, LPUNKNOWN outer) override
			{
				if (is_fake(guid))
				{
					if (device)
					{
						*device = nullptr;
					}
					log::write("virtual pad: CreateDevice(bf-vr virtual pad) refused with DIERR_DEVICENOTREG");
					return DIERR_DEVICENOTREG;
				}
				return real_->CreateDevice(guid, device, outer);
			}

			HRESULT STDMETHODCALLTYPE EnumDevices(DWORD type, LPDIENUMDEVICESCALLBACKA callback, LPVOID context, DWORD flags) override
			{
				// After a multiplayer-guard trip the game only sees its real devices.
				if (type != DI8DEVCLASS_GAMECTRL || !callback || safety::mod_disabled())
				{
					return real_->EnumDevices(type, callback, context, flags);
				}

				const bool hooked = ensure_is_xinput_hook();
				enum_forward forward{ callback, context };
				const HRESULT result = real_->EnumDevices(type, &forward_device, &forward, flags);
				log::write("virtual pad: EnumDevices(game controllers, flags=0x%lX) forwarded: %d device(s), hr=0x%08lX",
					flags, forward.devices, static_cast<unsigned long>(result));
				const bool force_feedback_only = (flags & DIEDFL_FORCEFEEDBACK) != 0;
				if (FAILED(result) || forward.stopped || !hooked || force_feedback_only)
				{
					log::write("virtual pad: fake device not offered (failed=%d stopped=%d hooked=%d force_feedback_only=%d)",
						FAILED(result) ? 1 : 0, forward.stopped ? 1 : 0, hooked ? 1 : 0, force_feedback_only ? 1 : 0);
					return result;
				}

				const DIDEVICEINSTANCEA device = fake_device();
				++offered;
				const BOOL answer = callback(&device, context);
				log::write("virtual pad: offered '%s' to the game, callback returned %d", fake_name, answer);
				return result;
			}

			HRESULT STDMETHODCALLTYPE GetDeviceStatus(REFGUID guid) override
			{
				return is_fake(guid) ? DI_OK : real_->GetDeviceStatus(guid);
			}

			HRESULT STDMETHODCALLTYPE RunControlPanel(HWND owner, DWORD flags) override
			{
				return real_->RunControlPanel(owner, flags);
			}

			HRESULT STDMETHODCALLTYPE Initialize(HINSTANCE instance, DWORD version) override
			{
				return real_->Initialize(instance, version);
			}

			HRESULT STDMETHODCALLTYPE FindDevice(REFGUID guid_class, LPCSTR name, LPGUID guid_instance) override
			{
				return real_->FindDevice(guid_class, name, guid_instance);
			}

			HRESULT STDMETHODCALLTYPE EnumDevicesBySemantics(LPCSTR user, LPDIACTIONFORMATA action_format,
				LPDIENUMDEVICESBYSEMANTICSCBA callback, LPVOID context, DWORD flags) override
			{
				return real_->EnumDevicesBySemantics(user, action_format, callback, context, flags);
			}

			HRESULT STDMETHODCALLTYPE ConfigureDevices(LPDICONFIGUREDEVICESCALLBACK callback, LPDICONFIGUREDEVICESPARAMSA parameters,
				DWORD flags, LPVOID reference_data) override
			{
				return real_->ConfigureDevices(callback, parameters, flags, reference_data);
			}

		private:
			~direct_input8a() = default;

			std::atomic<ULONG> references_{ 1 };
			IDirectInput8A* const real_;
		};
	}

	void wrap_direct_input(REFIID interface_id, LPUNKNOWN outer, const void* caller, void** output)
	{
		if (!output || !*output || outer || !IsEqualIID(interface_id, IID_IDirectInput8A) || !enabled())
		{
			return;
		}

		HMODULE caller_module{};
		GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
			static_cast<LPCWSTR>(caller), &caller_module);
		if (caller_module != GetModuleHandleW(nullptr))
		{
			log::write("virtual pad: DirectInput8Create from %p (module %p) is not the game's; not wrapped", caller, caller_module);
			return;
		}

		*output = static_cast<IDirectInput8A*>(new direct_input8a(static_cast<IDirectInput8A*>(*output)));
		log::write("virtual pad: wrapped the game's IDirectInput8A (caller %p)", caller);
	}

	virtual_pad_stats virtual_pad_counts()
	{
		return { offered.load(), xinput_answers.load() };
	}
}
