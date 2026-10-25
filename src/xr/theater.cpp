#include "xr/theater.hpp"

#include <d3d11.h>
#include <wrl/client.h>

#define XR_USE_PLATFORM_WIN32
#define XR_USE_GRAPHICS_API_D3D11
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <future>
#include <initializer_list>
#include <mutex>
#include <string>
#include <thread>
#include <cstring>
#include <vector>

#include "common/config.hpp"
#include "common/log.hpp"
#include "input/gamepad.hpp"
#include "stereo/eye_pass.hpp"
#include "stereo/hud_capture.hpp"
#include "stereo/tracking.hpp"
#include "xr/controller_actions.hpp"
#include "xr/runtime_selection.hpp"

using Microsoft::WRL::ComPtr;

namespace bfvr::xr
{
	namespace
	{
		// Virtual screen in the LOCAL space (metres): straight ahead at the starting head height.
		constexpr float screen_distance = 2.5f;
		constexpr float screen_width = 3.0f;
		// How often to look for a headset again while none is available.
		constexpr ULONGLONG retry_interval_ms = 2000;
		// Consecutive frame-loop errors tolerated before theater mode gives up.
		constexpr int max_frame_errors = 30;

		enum class phase
		{
			idle,
			waiting_for_headset,
			waiting_for_session,
			running,
			stopped,
			failed,
		};

		const char* phase_name(phase value)
		{
			switch (value)
			{
			case phase::idle: return "idle";
			case phase::waiting_for_headset: return "waiting_for_headset";
			case phase::waiting_for_session: return "waiting_for_session";
			case phase::running: return "running";
			case phase::stopped: return "stopped";
			case phase::failed: return "failed";
			}
			return "unknown";
		}

		// The game writes gamma-encoded bytes, so an sRGB swap chain from the back buffer's
		// format family shows them unchanged; copies within one family need no conversion.
		std::vector<DXGI_FORMAT> format_preferences(DXGI_FORMAT back_buffer)
		{
			switch (back_buffer)
			{
			case DXGI_FORMAT_R8G8B8A8_TYPELESS:
			case DXGI_FORMAT_R8G8B8A8_UNORM:
			case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
				return { DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, DXGI_FORMAT_R8G8B8A8_UNORM };
			case DXGI_FORMAT_B8G8R8A8_TYPELESS:
			case DXGI_FORMAT_B8G8R8A8_UNORM:
			case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
				return { DXGI_FORMAT_B8G8R8A8_UNORM_SRGB, DXGI_FORMAT_B8G8R8A8_UNORM };
			default:
				return { back_buffer };
			}
		}
	}

	struct theater::state
	{
		phase current{ phase::idle };
		ULONGLONG next_attempt{};
		std::string last_runtime_choice;
		std::string last_runtime_name;
		int instance_failures{};
		XrSessionState session_state{ XR_SESSION_STATE_UNKNOWN };

		// Per-second worst-case timings of the work done inside the game's Present.
		ULONGLONG timing_window_start{};
		double window_sync_ms{};
		double window_frame_ms{};
		double reported_sync_ms{};
		double reported_frame_ms{};

		void record_timing(double sync_ms, double frame_ms)
		{
			window_sync_ms = std::max(window_sync_ms, sync_ms);
			window_frame_ms = std::max(window_frame_ms, frame_ms);
			const ULONGLONG now = GetTickCount64();
			if (now - timing_window_start >= 1000)
			{
				reported_sync_ms = window_sync_ms;
				reported_frame_ms = window_frame_ms;
				window_sync_ms = window_frame_ms = 0.0;
				timing_window_start = now;
			}
		}
		int frame_errors{};
		bool warned_multisampled{};
		std::uint64_t presents_seen{};
		std::uint64_t frames_submitted{};

		ComPtr<ID3D11Device> device;
		XrInstance instance{ XR_NULL_HANDLE };
		// xrCreateInstance blocks for seconds while SteamVR is not running, so it never runs on
		// the game's Present thread.
		std::future<XrInstance> instance_attempt;
		XrSystemId system{ XR_NULL_SYSTEM_ID };
		XrSession session{ XR_NULL_HANDLE };
		XrSpace space{ XR_NULL_HANDLE };
		XrSpace view_space{ XR_NULL_HANDLE }; // head-locked layers (HUD)
		bool session_running{};

		// xrWaitFrame runs on its own thread so a throttled headset (taken off, dashboard open)
		// never slows the game; Present only submits when a headset frame is ready.
		std::thread waiter;
		std::mutex waiter_lock;
		std::condition_variable waiter_wake;
		bool waiter_stop{};
		bool wait_requested{};
		bool frame_ready{};
		std::atomic<std::uint64_t> hidden_frames{}; // completed on the waiter (not shown by the headset)
		bool warned_hidden_frame{};
		bool fov_logged{}; // the headset's per-eye FOV, once per process
		bool waiting_for_steamvr_logged{};
		std::atomic<bool> steamvr_restart_pending{}; // set on EXITING (Present thread), read by the instance worker
		std::atomic<bool> runtime_is_steamvr{};      // written by the instance worker
		// Every runtime call from the game's Present and the waiter's hidden-frame begin/end are
		// serialised: concurrent calls froze the game when Steam Link reconnected (2026-10-25).
		std::mutex xr_calls;
		std::atomic<const char*> stage{ "idle" };
		std::atomic<ULONGLONG> stage_since{};

		void enter(const char* name)
		{
			stage = name;
			stage_since = GetTickCount64();
		}
		// Predicted display time of the newest waited frame; the controllers are located for it on
		// the Present thread, under xr_calls like every other call that thread makes.
		std::atomic<XrTime> latest_display_time{};
		XrResult ready_result{};
		XrFrameState ready_frame{ XR_TYPE_FRAME_STATE };

		const bool gamepad_enabled{ config::get_bool("gamepad", true) };
		controller_actions actions;
		bool actions_ready{};
		ULONGLONG last_input_log{};

		XrSwapchain swapchain{ XR_NULL_HANDLE };
		std::vector<XrSwapchainImageD3D11KHR> images;
		UINT swapchain_width{};
		UINT swapchain_height{};
		DXGI_FORMAT swapchain_source_format{ DXGI_FORMAT_UNKNOWN };

		// Alternate-eye stereo (stereo = 1): each Present carries one eye, tagged by the camera
		// hook. The latest image of each eye is kept here with the pose and FOV it was rendered
		// with, and both are submitted as one projection layer (swap chain array slices 0/1).
		const bool stereo_mode{ config::get_bool("stereo", false) };
		ComPtr<ID3D11Texture2D> eye_textures[2];
		bool eye_ready[2]{};
		stereo::pose eye_poses[2];
		stereo::fov eye_fovs[2];
		std::uint64_t last_stored_copy{};
		std::uint64_t last_stored_pair{};
		std::uint64_t eye_images_stored{};

		// HUD layer (hud_layer = 1, stereo/hud_capture.hpp): the game's HUD on a head-locked quad in
		// front of the eyes, at a size and distance that keep it inside the visible lens area.
		const bool hud_mode{ stereo_mode && config::get_bool("hud_layer", false) };
		const float hud_distance{ static_cast<float>(std::atof(config::get("hud_distance", "1.5").c_str())) };
		const float hud_width{ static_cast<float>(std::atof(config::get("hud_width", "1.3").c_str())) };
		XrSwapchain hud_swapchain{ XR_NULL_HANDLE };
		std::vector<XrSwapchainImageD3D11KHR> hud_images;
		UINT hud_swapchain_width{};
		UINT hud_swapchain_height{};
		std::uint64_t hud_seen_frame{};
		int hud_idle_frames{};

		~state()
		{
			destroy_all();
		}

		void set_phase(phase next)
		{
			if (next != current)
			{
				log::write("theater: %s -> %s", phase_name(current), phase_name(next));
				current = next;
			}
		}

		bool check(XrResult result, const char* what)
		{
			if (XR_SUCCEEDED(result))
			{
				return true;
			}

			char text[XR_MAX_RESULT_STRING_SIZE]{};
			if (instance == XR_NULL_HANDLE || XR_FAILED(xrResultToString(instance, result, text)))
			{
				std::snprintf(text, sizeof(text), "XrResult %d", static_cast<int>(result));
			}
			log::write("theater: %s failed: %s", what, text);
			return false;
		}

		void retry_later()
		{
			next_attempt = GetTickCount64() + retry_interval_ms;
			set_phase(phase::waiting_for_headset);
		}

		// Runs on a worker thread; only touches state no other thread uses while it runs.
		XrInstance create_instance()
		{
			// Logged only on changes: while waiting for a headset this runs every retry.
			const std::string runtime = select_runtime();
			if (runtime != last_runtime_choice)
			{
				log::write("theater: OpenXR runtime: %s", runtime.c_str());
				last_runtime_choice = runtime;
			}
			runtime_is_steamvr = runtime.rfind("steamvr", 0) == 0;
			// Never be the one that starts SteamVR: wait until the user's headset connection has.
			// After SteamVR exited, wait for its server to disappear before waiting for a new one.
			if (steamvr_restart_pending && !steamvr_running())
			{
				steamvr_restart_pending = false;
			}
			if (runtime_is_steamvr && (steamvr_restart_pending || !steamvr_running()))
			{
				if (!waiting_for_steamvr_logged)
				{
					log::write("theater: waiting for SteamVR to be started (connect the headset, e.g. Steam Link)");
					waiting_for_steamvr_logged = true;
				}
				return XR_NULL_HANDLE;
			}
			waiting_for_steamvr_logged = false;
			const char* const extensions[] = { XR_KHR_D3D11_ENABLE_EXTENSION_NAME };

			XrInstanceCreateInfo info{ XR_TYPE_INSTANCE_CREATE_INFO };
			strcpy_s(info.applicationInfo.applicationName, "bf-vr");
			info.applicationInfo.applicationVersion = 1;
			strcpy_s(info.applicationInfo.engineName, "Frostbite");
			info.applicationInfo.apiVersion = XR_API_VERSION_1_0;
			info.enabledExtensionCount = 1;
			info.enabledExtensionNames = extensions;
			XrInstance created{ XR_NULL_HANDLE };
			const XrResult result = xrCreateInstance(&info, &created);
			if (XR_FAILED(result))
			{
				// A runtime that is still starting (SteamVR) fails here for a few seconds; log the first few.
				if (++instance_failures <= 3)
				{
					log::write("theater: xrCreateInstance failed: XrResult %d", static_cast<int>(result));
				}
				return XR_NULL_HANDLE;
			}
			instance_failures = 0;

			XrInstanceProperties properties{ XR_TYPE_INSTANCE_PROPERTIES };
			if (XR_SUCCEEDED(xrGetInstanceProperties(created, &properties)) && last_runtime_name != properties.runtimeName)
			{
				log::write("theater: runtime %s %u.%u.%u", properties.runtimeName,
					XR_VERSION_MAJOR(properties.runtimeVersion), XR_VERSION_MINOR(properties.runtimeVersion),
					XR_VERSION_PATCH(properties.runtimeVersion));
				last_runtime_name = properties.runtimeName;
			}
			return created;
		}

		// Returns false while no headset is available (retried later) or on a hard error.
		bool find_system()
		{
			XrSystemGetInfo info{ XR_TYPE_SYSTEM_GET_INFO };
			info.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
			const XrResult result = xrGetSystem(instance, &info, &system);
			if (result == XR_ERROR_FORM_FACTOR_UNAVAILABLE)
			{
				// Drop the instance so the next attempt chooses the runtime again (e.g. SteamVR started later).
				destroy_all();
				retry_later();
				return false;
			}
			if (!check(result, "xrGetSystem"))
			{
				set_phase(phase::failed);
				return false;
			}

			XrSystemProperties properties{ XR_TYPE_SYSTEM_PROPERTIES };
			if (XR_SUCCEEDED(xrGetSystemProperties(instance, system, &properties)))
			{
				log::write("theater: headset %s", properties.systemName);
			}
			return true;
		}

		bool create_session()
		{
			PFN_xrGetD3D11GraphicsRequirementsKHR get_requirements{};
			if (!check(xrGetInstanceProcAddr(instance, "xrGetD3D11GraphicsRequirementsKHR",
					reinterpret_cast<PFN_xrVoidFunction*>(&get_requirements)), "xrGetInstanceProcAddr"))
			{
				return false;
			}

			XrGraphicsRequirementsD3D11KHR requirements{ XR_TYPE_GRAPHICS_REQUIREMENTS_D3D11_KHR };
			if (!check(get_requirements(instance, system, &requirements), "xrGetD3D11GraphicsRequirementsKHR"))
			{
				return false;
			}

			// The runtime can only share textures with a device on the GPU that drives the headset.
			ComPtr<IDXGIDevice> dxgi_device;
			ComPtr<IDXGIAdapter> adapter;
			DXGI_ADAPTER_DESC adapter_desc{};
			if (FAILED(device.As(&dxgi_device)) || FAILED(dxgi_device->GetAdapter(&adapter)) || FAILED(adapter->GetDesc(&adapter_desc)))
			{
				log::write("theater: could not query the game's adapter");
				return false;
			}
			if (std::memcmp(&adapter_desc.AdapterLuid, &requirements.adapterLuid, sizeof(LUID)) != 0)
			{
				log::write("theater: the game renders on '%ls', but the headset runtime requires a different adapter", adapter_desc.Description);
				return false;
			}
			if (device->GetFeatureLevel() < requirements.minFeatureLevel)
			{
				log::write("theater: device feature level 0x%x is below the runtime minimum 0x%x",
					device->GetFeatureLevel(), requirements.minFeatureLevel);
				return false;
			}

			XrGraphicsBindingD3D11KHR binding{ XR_TYPE_GRAPHICS_BINDING_D3D11_KHR };
			binding.device = device.Get();
			XrSessionCreateInfo session_info{ XR_TYPE_SESSION_CREATE_INFO };
			session_info.next = &binding;
			session_info.systemId = system;
			if (!check(xrCreateSession(instance, &session_info, &session), "xrCreateSession"))
			{
				return false;
			}

			XrReferenceSpaceCreateInfo space_info{ XR_TYPE_REFERENCE_SPACE_CREATE_INFO };
			space_info.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_LOCAL;
			space_info.poseInReferenceSpace.orientation.w = 1.0f;
			if (!check(xrCreateReferenceSpace(session, &space_info, &space), "xrCreateReferenceSpace"))
			{
				return false;
			}
			space_info.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_VIEW;
			if (!check(xrCreateReferenceSpace(session, &space_info, &view_space), "xrCreateReferenceSpace(VIEW)"))
			{
				return false;
			}

			log::write("theater: session created on '%ls'", adapter_desc.Description);
			// Controller input is optional: without it the screen still works.
			actions_ready = gamepad_enabled && actions.create(instance, session);
			set_phase(phase::waiting_for_session);
			return true;
		}

		void handle_session_state(XrSessionState new_state)
		{
			log::write("theater: session state %d", static_cast<int>(new_state));
			session_state = new_state;
			switch (new_state)
			{
			case XR_SESSION_STATE_READY:
			{
				XrSessionBeginInfo begin{ XR_TYPE_SESSION_BEGIN_INFO };
				begin.primaryViewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
				if (check(xrBeginSession(session, &begin), "xrBeginSession"))
				{
					session_running = true;
					start_waiter();
					set_phase(phase::running);
				}
				break;
			}
			case XR_SESSION_STATE_STOPPING:
				stop_waiter();
				check(xrEndSession(session), "xrEndSession");
				session_running = false;
				set_phase(phase::waiting_for_session);
				break;
			case XR_SESSION_STATE_EXITING:
				// The user or runtime ended VR (e.g. quitting SteamVR). With SteamVR, reconnect only
				// after its server has gone and been started again by the user; never while it shuts
				// down. Other runtimes stay out until the game restarts.
				destroy_all();
				if (runtime_is_steamvr)
				{
					steamvr_restart_pending = true;
					retry_later();
				}
				else
				{
					set_phase(phase::stopped);
				}
				break;
			case XR_SESSION_STATE_LOSS_PENDING:
				// Connection lost (headset unplugged, link dropped): rebuild later, choosing the runtime again.
				destroy_all();
				retry_later();
				break;
			default:
				break;
			}
		}

		void log_interaction_profile()
		{
			for (const char* hand : { "/user/hand/left", "/user/hand/right" })
			{
				XrPath hand_path{};
				XrInteractionProfileState state{ XR_TYPE_INTERACTION_PROFILE_STATE };
				char name[XR_MAX_PATH_LENGTH]{};
				std::uint32_t length{};
				if (XR_SUCCEEDED(xrStringToPath(instance, hand, &hand_path))
					&& XR_SUCCEEDED(xrGetCurrentInteractionProfile(session, hand_path, &state)))
				{
					if (state.interactionProfile == XR_NULL_PATH
						|| XR_FAILED(xrPathToString(instance, state.interactionProfile, sizeof(name), &length, name)))
					{
						strcpy_s(name, "(none)");
					}
					log::write("controllers: %s uses %s", hand, name);
				}
			}
		}

		void poll_events()
		{
			for (;;)
			{
				XrEventDataBuffer event{ XR_TYPE_EVENT_DATA_BUFFER };
				const XrResult result = xrPollEvent(instance, &event);
				if (result == XR_EVENT_UNAVAILABLE || !check(result, "xrPollEvent"))
				{
					return;
				}

				if (event.type == XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED && session != XR_NULL_HANDLE)
				{
					handle_session_state(reinterpret_cast<const XrEventDataSessionStateChanged&>(event).state);
					if (instance == XR_NULL_HANDLE)
					{
						return;
					}
				}
				else if (event.type == XR_TYPE_EVENT_DATA_INTERACTION_PROFILE_CHANGED && session != XR_NULL_HANDLE)
				{
					log_interaction_profile();
				}
				else if (event.type == XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING)
				{
					log::write("theater: runtime is going away; rebuilding later");
					destroy_all();
					retry_later();
					return;
				}
			}
		}

		bool ensure_swapchain(const D3D11_TEXTURE2D_DESC& back_buffer)
		{
			if (swapchain != XR_NULL_HANDLE && back_buffer.Width == swapchain_width && back_buffer.Height == swapchain_height
				&& back_buffer.Format == swapchain_source_format)
			{
				return true;
			}
			destroy_swapchain();

			std::uint32_t count{};
			if (!check(xrEnumerateSwapchainFormats(session, 0, &count, nullptr), "xrEnumerateSwapchainFormats"))
			{
				return false;
			}
			std::vector<std::int64_t> formats(count);
			if (!check(xrEnumerateSwapchainFormats(session, count, &count, formats.data()), "xrEnumerateSwapchainFormats"))
			{
				return false;
			}

			DXGI_FORMAT chosen = DXGI_FORMAT_UNKNOWN;
			for (const DXGI_FORMAT candidate : format_preferences(back_buffer.Format))
			{
				if (std::find(formats.begin(), formats.end(), static_cast<std::int64_t>(candidate)) != formats.end())
				{
					chosen = candidate;
					break;
				}
			}
			if (chosen == DXGI_FORMAT_UNKNOWN)
			{
				log::write("theater: runtime offers no swap chain format compatible with back buffer format %d", back_buffer.Format);
				return false;
			}

			XrSwapchainCreateInfo info{ XR_TYPE_SWAPCHAIN_CREATE_INFO };
			info.usageFlags = XR_SWAPCHAIN_USAGE_TRANSFER_DST_BIT | XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT;
			info.format = chosen;
			info.sampleCount = 1;
			info.width = back_buffer.Width;
			info.height = back_buffer.Height;
			info.faceCount = 1;
			info.arraySize = stereo_mode ? 2 : 1; // stereo: slice 0 = left eye, slice 1 = right eye
			info.mipCount = 1;
			if (!check(xrCreateSwapchain(session, &info, &swapchain), "xrCreateSwapchain"))
			{
				return false;
			}

			if (!check(xrEnumerateSwapchainImages(swapchain, 0, &count, nullptr), "xrEnumerateSwapchainImages"))
			{
				return false;
			}
			images.assign(count, XrSwapchainImageD3D11KHR{ XR_TYPE_SWAPCHAIN_IMAGE_D3D11_KHR });
			if (!check(xrEnumerateSwapchainImages(swapchain, count, &count,
					reinterpret_cast<XrSwapchainImageBaseHeader*>(images.data())), "xrEnumerateSwapchainImages"))
			{
				return false;
			}

			if (stereo_mode)
			{
				// Copies of the latest frame per eye, same format family as the back buffer.
				D3D11_TEXTURE2D_DESC eye_desc = back_buffer;
				eye_desc.Usage = D3D11_USAGE_DEFAULT;
				eye_desc.BindFlags = 0;
				eye_desc.CPUAccessFlags = 0;
				eye_desc.MiscFlags = 0;
				eye_desc.ArraySize = 1;
				eye_desc.MipLevels = 1;
				for (auto& texture : eye_textures)
				{
					if (FAILED(device->CreateTexture2D(&eye_desc, nullptr, &texture)))
					{
						log::write("theater: eye texture creation failed");
						return false;
					}
				}
				eye_ready[0] = eye_ready[1] = false;
			}

			swapchain_width = back_buffer.Width;
			swapchain_height = back_buffer.Height;
			swapchain_source_format = back_buffer.Format;
			log::write("theater: swap chain %ux%u x%u, back buffer format %d -> runtime format %d, %u images",
				swapchain_width, swapchain_height, info.arraySize, back_buffer.Format, chosen, count);
			return true;
		}

		bool ensure_hud_swapchain(const D3D11_TEXTURE2D_DESC& hud)
		{
			if (hud_swapchain != XR_NULL_HANDLE && hud.Width == hud_swapchain_width && hud.Height == hud_swapchain_height)
			{
				return true;
			}
			destroy_hud_swapchain();
			std::uint32_t count{};
			if (!check(xrEnumerateSwapchainFormats(session, 0, &count, nullptr), "xrEnumerateSwapchainFormats"))
			{
				return false;
			}
			std::vector<std::int64_t> formats(count);
			if (!check(xrEnumerateSwapchainFormats(session, count, &count, formats.data()), "xrEnumerateSwapchainFormats"))
			{
				return false;
			}
			DXGI_FORMAT chosen = DXGI_FORMAT_UNKNOWN;
			for (const DXGI_FORMAT candidate : format_preferences(hud.Format))
			{
				if (std::find(formats.begin(), formats.end(), static_cast<std::int64_t>(candidate)) != formats.end())
				{
					chosen = candidate;
					break;
				}
			}
			if (chosen == DXGI_FORMAT_UNKNOWN)
			{
				return false;
			}
			XrSwapchainCreateInfo info{ XR_TYPE_SWAPCHAIN_CREATE_INFO };
			info.usageFlags = XR_SWAPCHAIN_USAGE_TRANSFER_DST_BIT | XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT;
			info.format = chosen;
			info.sampleCount = 1;
			info.width = hud.Width;
			info.height = hud.Height;
			info.faceCount = 1;
			info.arraySize = 1;
			info.mipCount = 1;
			if (!check(xrCreateSwapchain(session, &info, &hud_swapchain), "xrCreateSwapchain(HUD)")
				|| !check(xrEnumerateSwapchainImages(hud_swapchain, 0, &count, nullptr), "xrEnumerateSwapchainImages"))
			{
				return false;
			}
			hud_images.assign(count, XrSwapchainImageD3D11KHR{ XR_TYPE_SWAPCHAIN_IMAGE_D3D11_KHR });
			if (!check(xrEnumerateSwapchainImages(hud_swapchain, count, &count,
					reinterpret_cast<XrSwapchainImageBaseHeader*>(hud_images.data())), "xrEnumerateSwapchainImages"))
			{
				return false;
			}
			hud_swapchain_width = hud.Width;
			hud_swapchain_height = hud.Height;
			log::write("theater: HUD swap chain %ux%u, format %d, quad %.2f m wide at %.2f m", hud.Width, hud.Height, chosen, hud_width, hud_distance);
			return true;
		}

		// The latest captured HUD into its swap chain; false when there is none to show.
		bool copy_hud_to_swapchain()
		{
			const stereo::hud_frame hud = stereo::latest_hud();
			if (!hud.texture)
			{
				return false;
			}
			// The HUD is not drawn on every frame type (cutscenes, loading); hide a stale one.
			hud_idle_frames = hud.frame == hud_seen_frame ? hud_idle_frames + 1 : 0;
			hud_seen_frame = hud.frame;
			if (hud.frame == 0 || hud_idle_frames > 30)
			{
				return false;
			}
			D3D11_TEXTURE2D_DESC desc{};
			hud.texture->GetDesc(&desc);
			if (!ensure_hud_swapchain(desc))
			{
				return false;
			}
			std::uint32_t index{};
			XrSwapchainImageAcquireInfo acquire{ XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO };
			if (!check(xrAcquireSwapchainImage(hud_swapchain, &acquire, &index), "xrAcquireSwapchainImage(HUD)"))
			{
				return false;
			}
			XrSwapchainImageWaitInfo wait{ XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO };
			wait.timeout = XR_INFINITE_DURATION;
			if (!check(xrWaitSwapchainImage(hud_swapchain, &wait), "xrWaitSwapchainImage(HUD)"))
			{
				return false;
			}
			ComPtr<ID3D11DeviceContext> context;
			device->GetImmediateContext(&context);
			context->CopySubresourceRegion(hud_images[index].texture, 0, 0, 0, 0, hud.texture, 0, nullptr);
			XrSwapchainImageReleaseInfo release{ XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO };
			return check(xrReleaseSwapchainImage(hud_swapchain, &release), "xrReleaseSwapchainImage(HUD)");
		}

		void destroy_hud_swapchain()
		{
			if (hud_swapchain != XR_NULL_HANDLE)
			{
				xrDestroySwapchain(hud_swapchain);
				hud_swapchain = XR_NULL_HANDLE;
			}
			hud_images.clear();
			hud_swapchain_width = hud_swapchain_height = 0;
		}

		// Pair rendering: the eye pass's first image is the left eye, this Present's image the right
		// eye of the same game frame. Only a pair completed since the last one is taken.
		void store_pair(ID3D11Texture2D* back_buffer, const stereo::rendered_view& tag)
		{
			const std::uint64_t completed = stereo::eye_pass_pairs();
			ID3D11Texture2D* const left = stereo::eye_pass_image();
			if (completed == last_stored_pair || !left || !eye_textures[0] || !eye_textures[1])
			{
				return;
			}
			ComPtr<ID3D11DeviceContext> context;
			device->GetImmediateContext(&context);
			context->CopyResource(eye_textures[0].Get(), left);
			context->CopyResource(eye_textures[1].Get(), back_buffer);
			eye_poses[0] = tag.eye_pose;
			eye_poses[1] = tag.second_pose;
			eye_fovs[0] = eye_fovs[1] = tag.rendered_fov;
			eye_ready[0] = eye_ready[1] = true;
			last_stored_pair = completed;
			last_stored_copy = tag.copy_index;
			eye_images_stored += 2;
		}

		// Stereo: keeps this Present's image as the eye the camera hook rendered it for.
		void store_eye(ID3D11Texture2D* back_buffer)
		{
			const stereo::rendered_view tag = stereo::latest_rendered_view();
			if (!tag.stereo)
			{
				// Mono frame (menu, loading, no tracking): fall back to the theater screen.
				eye_ready[0] = eye_ready[1] = false;
				return;
			}
			if (tag.pair)
			{
				store_pair(back_buffer, tag);
				return;
			}
			if (tag.copy_index == last_stored_copy || !eye_textures[tag.eye])
			{
				return;
			}
			ComPtr<ID3D11DeviceContext> context;
			device->GetImmediateContext(&context);
			context->CopyResource(eye_textures[tag.eye].Get(), back_buffer);
			eye_poses[tag.eye] = tag.eye_pose;
			eye_fovs[tag.eye] = tag.rendered_fov;
			eye_ready[tag.eye] = true;
			last_stored_copy = tag.copy_index;
			++eye_images_stored;
		}

		// Stereo: both eye images into the array swap chain, one projection view per eye.
		bool copy_eyes_to_swapchain()
		{
			std::uint32_t index{};
			XrSwapchainImageAcquireInfo acquire{ XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO };
			if (!check(xrAcquireSwapchainImage(swapchain, &acquire, &index), "xrAcquireSwapchainImage"))
			{
				return false;
			}
			XrSwapchainImageWaitInfo wait{ XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO };
			wait.timeout = XR_INFINITE_DURATION;
			if (!check(xrWaitSwapchainImage(swapchain, &wait), "xrWaitSwapchainImage"))
			{
				return false;
			}
			ComPtr<ID3D11DeviceContext> context;
			device->GetImmediateContext(&context);
			for (UINT eye = 0; eye < 2; ++eye)
			{
				context->CopySubresourceRegion(images[index].texture, D3D11CalcSubresource(0, eye, 1), 0, 0, 0, eye_textures[eye].Get(), 0, nullptr);
			}
			XrSwapchainImageReleaseInfo release{ XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO };
			return check(xrReleaseSwapchainImage(swapchain, &release), "xrReleaseSwapchainImage");
		}

		// Stereo: samples both eye poses for the predicted display time (waiter thread).
		void publish_head_sample(XrTime display_time)
		{
			XrViewLocateInfo locate{ XR_TYPE_VIEW_LOCATE_INFO };
			locate.viewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
			locate.displayTime = display_time;
			locate.space = space;
			XrViewState view_state{ XR_TYPE_VIEW_STATE };
			XrView views[2]{ { XR_TYPE_VIEW }, { XR_TYPE_VIEW } };
			std::uint32_t count = 0;
			stereo::head_sample sample;
			if (XR_SUCCEEDED(xrLocateViews(session, &locate, &view_state, 2, &count, views)) && count == 2
				&& (view_state.viewStateFlags & XR_VIEW_STATE_ORIENTATION_VALID_BIT)
				&& (view_state.viewStateFlags & XR_VIEW_STATE_POSITION_VALID_BIT))
			{
				sample.valid = true;
				for (int eye = 0; eye < 2; ++eye)
				{
					const XrPosef& pose = views[eye].pose;
					sample.eyes[eye] = { { pose.position.x, pose.position.y, pose.position.z },
						{ pose.orientation.x, pose.orientation.y, pose.orientation.z, pose.orientation.w } };
					sample.fovs[eye] = { views[eye].fov.angleLeft, views[eye].fov.angleRight, views[eye].fov.angleUp, views[eye].fov.angleDown };
				}
				if (!fov_logged)
				{
					constexpr float degrees = 57.29578f;
					for (int eye = 0; eye < 2; ++eye)
					{
						const XrFovf& f = views[eye].fov;
						log::write("theater: %s eye FOV left %.1f right %.1f up %.1f down %.1f deg", eye == 0 ? "left" : "right",
							f.angleLeft * degrees, f.angleRight * degrees, f.angleUp * degrees, f.angleDown * degrees);
					}
					fov_logged = true;
				}
			}
			stereo::publish_head_sample(sample);
		}

		bool copy_to_swapchain(ID3D11Texture2D* back_buffer)
		{
			std::uint32_t index{};
			XrSwapchainImageAcquireInfo acquire{ XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO };
			if (!check(xrAcquireSwapchainImage(swapchain, &acquire, &index), "xrAcquireSwapchainImage"))
			{
				return false;
			}

			XrSwapchainImageWaitInfo wait{ XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO };
			wait.timeout = XR_INFINITE_DURATION;
			if (!check(xrWaitSwapchainImage(swapchain, &wait), "xrWaitSwapchainImage"))
			{
				return false;
			}

			ComPtr<ID3D11DeviceContext> context;
			device->GetImmediateContext(&context);
			context->CopySubresourceRegion(images[index].texture, 0, 0, 0, 0, back_buffer, 0, nullptr);

			XrSwapchainImageReleaseInfo release{ XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO };
			return check(xrReleaseSwapchainImage(swapchain, &release), "xrReleaseSwapchainImage");
		}

		void frame_error()
		{
			if (++frame_errors >= max_frame_errors)
			{
				log::write("theater: %d consecutive frame errors; disabling theater mode", frame_errors);
				destroy_all();
				set_phase(phase::failed);
			}
		}

		void waiter_loop()
		{
			bool wait_again = false; // the previous frame was completed here; no request needed
			for (;;)
			{
				{
					std::unique_lock guard(waiter_lock);
					if (!wait_again)
					{
						waiter_wake.wait(guard, [this] { return waiter_stop || wait_requested; });
						wait_requested = false;
					}
					if (waiter_stop)
					{
						return;
					}
				}
				wait_again = false;

				XrFrameWaitInfo wait_info{ XR_TYPE_FRAME_WAIT_INFO };
				XrFrameState frame{ XR_TYPE_FRAME_STATE };
				const XrResult result = xrWaitFrame(session, &wait_info, &frame);
				if (stereo_mode && XR_SUCCEEDED(result))
				{
					publish_head_sample(frame.predictedDisplayTime);
				}
				if (XR_SUCCEEDED(result))
				{
					latest_display_time = frame.predictedDisplayTime;
				}

				// A frame the headset will not show (headset off, dashboard) is begun and ended
				// here: SteamVR throttles those calls to ~5 Hz for a hidden app, which held the
				// game's Present for ~200 ms per frame (7 fps) when done there.
				if (XR_SUCCEEDED(result) && !frame.shouldRender)
				{
					std::unique_lock xr(xr_calls, std::defer_lock);
					while (!xr.try_lock())
					{
						if (waiter_stop_requested())
						{
							return;
						}
						Sleep(1);
					}
					XrFrameBeginInfo begin_info{ XR_TYPE_FRAME_BEGIN_INFO };
					XrFrameEndInfo end_info{ XR_TYPE_FRAME_END_INFO };
					end_info.displayTime = frame.predictedDisplayTime;
					end_info.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
					const XrResult begun = xrBeginFrame(session, &begin_info);
					const XrResult ended = XR_SUCCEEDED(begun) ? xrEndFrame(session, &end_info) : begun;
					if (XR_FAILED(ended) && !warned_hidden_frame)
					{
						log::write("theater: completing a hidden frame failed: XrResult %d", static_cast<int>(ended));
						warned_hidden_frame = true;
					}
					++hidden_frames;
					wait_again = true;
					continue;
				}

				const std::lock_guard guard(waiter_lock);
				ready_result = result;
				ready_frame = frame;
				frame_ready = true;
			}
		}

		bool waiter_stop_requested()
		{
			const std::lock_guard guard(waiter_lock);
			return waiter_stop;
		}

		void start_waiter()
		{
			stop_waiter();
			waiter_stop = false;
			frame_ready = false;
			latest_display_time = 0;
			wait_requested = true;
			waiter = std::thread([this] { waiter_loop(); });
		}

		// Must run before the session is ended or destroyed; xrWaitFrame returns at the next headset frame.
		void stop_waiter()
		{
			if (!waiter.joinable())
			{
				return;
			}
			{
				const std::lock_guard guard(waiter_lock);
				waiter_stop = true;
			}
			waiter_wake.notify_one();
			waiter.join();
		}

		void request_next_wait()
		{
			{
				const std::lock_guard guard(waiter_lock);
				wait_requested = true;
			}
			waiter_wake.notify_one();
		}

		void submit_frame(IDXGISwapChain* swap_chain)
		{
			// Submit only when the waiter has a headset frame; otherwise this Present goes out
			// without VR work, so the game keeps its own frame rate.
			XrFrameState frame{ XR_TYPE_FRAME_STATE };
			XrResult wait_result{};
			{
				const std::lock_guard guard(waiter_lock);
				if (!frame_ready)
				{
					return;
				}
				frame_ready = false;
				frame = ready_frame;
				wait_result = ready_result;
			}

			// Every consumed wait leads to exactly one begin/end pair, then the next wait.
			struct next_wait
			{
				state* owner;
				~next_wait() { owner->request_next_wait(); }
			} const schedule_next{ this };

			if (!check(wait_result, "xrWaitFrame"))
			{
				frame_error();
				return;
			}

			// A waited frame must always be begun and ended, even if there is nothing to show.
			ComPtr<ID3D11Texture2D> back_buffer;
			bool can_copy = false;
			if (SUCCEEDED(swap_chain->GetBuffer(0, IID_PPV_ARGS(&back_buffer))))
			{
				D3D11_TEXTURE2D_DESC desc{};
				back_buffer->GetDesc(&desc);
				if (desc.SampleDesc.Count != 1 && !warned_multisampled)
				{
					log::write("theater: multisampled back buffers are not supported yet; showing an empty frame");
					warned_multisampled = true;
				}
				can_copy = desc.SampleDesc.Count == 1 && ensure_swapchain(desc);
			}

			enter("begin frame");
			XrFrameBeginInfo begin_info{ XR_TYPE_FRAME_BEGIN_INFO };
			if (!check(xrBeginFrame(session, &begin_info), "xrBeginFrame"))
			{
				frame_error();
				return;
			}

			XrCompositionLayerQuad quad{ XR_TYPE_COMPOSITION_LAYER_QUAD };
			XrCompositionLayerQuad hud_quad{ XR_TYPE_COMPOSITION_LAYER_QUAD };
			XrCompositionLayerProjection projection{ XR_TYPE_COMPOSITION_LAYER_PROJECTION };
			XrCompositionLayerProjectionView views[2]{ { XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW }, { XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW } };
			const XrCompositionLayerBaseHeader* layers[2]{};
			std::uint32_t layer_count = 0;
			const bool stereo_frame = stereo_mode && can_copy && eye_ready[0] && eye_ready[1];
			enter("copy to swap chain");
			if (frame.shouldRender && stereo_frame && copy_eyes_to_swapchain())
			{
				// Each eye is shown with exactly the pose and FOV it was rendered with; the
				// compositor reprojects to the current head pose.
				for (int eye = 0; eye < 2; ++eye)
				{
					const stereo::pose& pose = eye_poses[eye];
					views[eye].pose.position = { pose.position[0], pose.position[1], pose.position[2] };
					views[eye].pose.orientation = { pose.orientation[0], pose.orientation[1], pose.orientation[2], pose.orientation[3] };
					views[eye].fov = { eye_fovs[eye].left, eye_fovs[eye].right, eye_fovs[eye].up, eye_fovs[eye].down };
					views[eye].subImage.swapchain = swapchain;
					views[eye].subImage.imageRect.extent = { static_cast<std::int32_t>(swapchain_width), static_cast<std::int32_t>(swapchain_height) };
					views[eye].subImage.imageArrayIndex = static_cast<std::uint32_t>(eye);
				}
				projection.space = space;
				projection.viewCount = 2;
				projection.views = views;
				layers[0] = reinterpret_cast<const XrCompositionLayerBaseHeader*>(&projection);
				layer_count = 1;

				if (hud_mode && copy_hud_to_swapchain())
				{
					// The texture holds the HUD over transparent black: premultiplied alpha.
					hud_quad.layerFlags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;
					hud_quad.space = view_space;
					hud_quad.eyeVisibility = XR_EYE_VISIBILITY_BOTH;
					hud_quad.subImage.swapchain = hud_swapchain;
					hud_quad.subImage.imageRect.extent = { static_cast<std::int32_t>(hud_swapchain_width), static_cast<std::int32_t>(hud_swapchain_height) };
					hud_quad.pose.orientation.w = 1.0f;
					hud_quad.pose.position.z = -hud_distance;
					hud_quad.size = { hud_width, hud_width * static_cast<float>(hud_swapchain_height) / static_cast<float>(hud_swapchain_width) };
					layers[layer_count++] = reinterpret_cast<const XrCompositionLayerBaseHeader*>(&hud_quad);
				}
			}
			else if (frame.shouldRender && can_copy && copy_to_swapchain(back_buffer.Get()))
			{
				quad.space = space;
				quad.eyeVisibility = XR_EYE_VISIBILITY_BOTH;
				quad.subImage.swapchain = swapchain;
				quad.subImage.imageRect.extent = { static_cast<std::int32_t>(swapchain_width), static_cast<std::int32_t>(swapchain_height) };
				quad.pose.orientation.w = 1.0f;
				quad.pose.position.z = -screen_distance;
				quad.size = { screen_width, screen_width * static_cast<float>(swapchain_height) / static_cast<float>(swapchain_width) };
				layers[0] = reinterpret_cast<const XrCompositionLayerBaseHeader*>(&quad);
				layer_count = 1;
			}

			enter("end frame");
			XrFrameEndInfo end_info{ XR_TYPE_FRAME_END_INFO };
			end_info.displayTime = frame.predictedDisplayTime;
			end_info.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
			end_info.layerCount = layer_count;
			end_info.layers = layers;
			if (!check(xrEndFrame(session, &end_info), "xrEndFrame"))
			{
				frame_error();
				return;
			}

			frame_errors = 0;
			if (layer_count)
			{
				++frames_submitted;
			}
		}

		void destroy_swapchain()
		{
			if (swapchain != XR_NULL_HANDLE)
			{
				xrDestroySwapchain(swapchain);
				swapchain = XR_NULL_HANDLE;
			}
			images.clear();
			for (int eye = 0; eye < 2; ++eye)
			{
				eye_textures[eye].Reset();
				eye_ready[eye] = false;
			}
			swapchain_width = swapchain_height = 0;
			swapchain_source_format = DXGI_FORMAT_UNKNOWN;
		}

		void destroy_session()
		{
			stop_waiter();
			destroy_swapchain();
			destroy_hud_swapchain();
			if (view_space != XR_NULL_HANDLE)
			{
				xrDestroySpace(view_space);
				view_space = XR_NULL_HANDLE;
			}
			if (space != XR_NULL_HANDLE)
			{
				xrDestroySpace(space);
				space = XR_NULL_HANDLE;
			}
			if (session != XR_NULL_HANDLE)
			{
				xrDestroySession(session);
				session = XR_NULL_HANDLE;
			}
			session_running = false;
		}

		void destroy_all()
		{
			destroy_session();
			actions.destroy();
			actions_ready = false;
			stereo::publish_hand_sample({});
			input::publish({});
			if (instance != XR_NULL_HANDLE)
			{
				xrDestroyInstance(instance);
				instance = XR_NULL_HANDLE;
			}
			system = XR_NULL_SYSTEM_ID;
		}
	};

	theater::theater() : state_(std::make_unique<state>())
	{
	}

	theater::~theater() = default;

	void theater::on_present(IDXGISwapChain* swap_chain)
	{
		state& s = *state_;
		++s.presents_seen;
		if (s.current == phase::failed || s.current == phase::stopped)
		{
			return;
		}

		ComPtr<ID3D11Device> device;
		if (FAILED(swap_chain->GetDevice(IID_PPV_ARGS(&device))))
		{
			return;
		}
		if (s.device && s.device != device)
		{
			// The game recreated its device; the session is bound to the old one.
			log::write("theater: D3D11 device changed; rebuilding the session");
			s.destroy_all();
			s.set_phase(phase::idle);
		}
		s.device = device;

		if (s.instance == XR_NULL_HANDLE)
		{
			if (!s.instance_attempt.valid())
			{
				if (GetTickCount64() >= s.next_attempt)
				{
					s.instance_attempt = std::async(std::launch::async, [&s] { return s.create_instance(); });
				}
				return;
			}
			if (s.instance_attempt.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
			{
				return;
			}
			s.instance = s.instance_attempt.get();
			if (s.instance == XR_NULL_HANDLE)
			{
				s.retry_later();
				return;
			}
		}

		if (s.system == XR_NULL_SYSTEM_ID)
		{
			if (GetTickCount64() < s.next_attempt || !s.find_system())
			{
				return;
			}
		}

		if (s.session == XR_NULL_HANDLE && !s.create_session())
		{
			s.destroy_all();
			s.set_phase(phase::failed);
			return;
		}

		const std::lock_guard xr(s.xr_calls);
		s.enter("poll events");
		s.poll_events();
		if (s.session_running)
		{
			s.enter("sync actions");
			LARGE_INTEGER frequency{}, t0{}, t1{}, t2{};
			QueryPerformanceFrequency(&frequency);
			QueryPerformanceCounter(&t0);
			if (s.actions_ready)
			{
				const input::gamepad_state pad = s.actions.sync(s.session);
				input::publish(pad);
				if (const XrTime time = s.latest_display_time.load(); time != 0)
				{
					stereo::publish_hand_sample(s.actions.locate(s.space, time));
				}
				// Non-neutral input is logged at most once per second, for diagnosing bindings.
				const bool active = pad.buttons || pad.left_trigger > 64 || pad.right_trigger > 64
					|| std::abs(pad.left_x) > 8000 || std::abs(pad.left_y) > 8000 || std::abs(pad.right_x) > 8000 || std::abs(pad.right_y) > 8000;
				if (active && GetTickCount64() - s.last_input_log >= 1000)
				{
					log::write("controllers: input buttons=%04X lt=%u rt=%u left=(%d,%d) right=(%d,%d)", pad.buttons,
						pad.left_trigger, pad.right_trigger, pad.left_x, pad.left_y, pad.right_x, pad.right_y);
					s.last_input_log = GetTickCount64();
				}
			}
			if (s.stereo_mode)
			{
				s.enter("store eye");
				ComPtr<ID3D11Texture2D> back_buffer;
				if (SUCCEEDED(swap_chain->GetBuffer(0, IID_PPV_ARGS(&back_buffer))))
				{
					D3D11_TEXTURE2D_DESC desc{};
					back_buffer->GetDesc(&desc);
					if (desc.SampleDesc.Count == 1 && s.ensure_swapchain(desc))
					{
						s.store_eye(back_buffer.Get());
					}
				}
			}
			QueryPerformanceCounter(&t1);
			s.enter("submit");
			s.submit_frame(swap_chain);
			QueryPerformanceCounter(&t2);
			const double ticks_per_ms = static_cast<double>(frequency.QuadPart) / 1000.0;
			s.record_timing(static_cast<double>(t1.QuadPart - t0.QuadPart) / ticks_per_ms,
				static_cast<double>(t2.QuadPart - t1.QuadPart) / ticks_per_ms);
		}
		else
		{
			input::publish({});
		}
		s.enter("idle");
	}

	theater_status theater::status() const
	{
		const state& s = *state_;
		const char* stage = s.stage.load();
		const double stage_ms = std::strcmp(stage, "idle") == 0 ? 0.0 : static_cast<double>(GetTickCount64() - s.stage_since.load());
		return { phase_name(s.current), s.presents_seen, s.frames_submitted, static_cast<int>(s.session_state),
			s.reported_sync_ms, s.reported_frame_ms, s.stereo_mode, s.eye_images_stored, stage, stage_ms };
	}
}
