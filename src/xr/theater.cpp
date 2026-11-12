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
#include <cmath>
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
#include "common/settings_store.hpp"
#include "hooks/game_window.hpp"
#include "input/gestures.hpp"
#include "input/haptics.hpp"
#include "input/pointer.hpp"
#include "stereo/eye_math.hpp"
#include "stereo/hud_layout.hpp"
#include "stereo/camera_hook.hpp"
#include "stereo/eye_pass.hpp"
#include "stereo/extra_mesh.hpp"
#include "stereo/hud_capture.hpp"
#include "stereo/scope_compose.hpp"
#include "stereo/tracking.hpp"
#include "body/arm_ik.hpp"
#include "body/arm_pose.hpp"
#include "ui/held_grenade.hpp"
#include "ui/settings_panel.hpp"
#include "ui/reticle.hpp"
#include "ui/vignette.hpp"
#include "ui/vr_panel.hpp"
#include "weapon/grab_debug.hpp"
#include "weapon/physical_grenade.hpp"
#include "weapon/two_hand.hpp"
#include "weapon/two_handed.hpp"
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
		// Consecutive frame-loop errors before the connection is rebuilt, and rebuilds without a
		// single good frame before theater mode gives up.
		constexpr int max_frame_errors = 30;
		constexpr int max_rebuilds = 5;
		// How often SteamVR's server process is looked up (on its own thread).
		constexpr ULONGLONG server_check_ms = 2000;
		// How long stopping the frame waiter may take; a runtime that died may never return xrWaitFrame.
		constexpr auto waiter_stop_timeout = std::chrono::seconds(3);

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
		// Input focus (FOCUSED): SteamVR's dashboard takes it while the game stays visible.
		std::atomic<std::uint32_t> focus_losses{};
		std::atomic<ULONGLONG> unfocused_since{}; // 0 while focused or never focused
		ULONGLONG focused_since{};
		std::atomic<bool> input_lost{};

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
		XrSpace stage_space{ XR_NULL_HANDLE }; // the floor, when the runtime has one (#46)
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
		// SteamVR's server process: the one the instance was created against, the one that sent
		// EXITING, and the one running now (server_watch). A new id under a live instance means
		// SteamVR was restarted from the headset while the game ran (MP3, 2026-11-04).
		std::atomic<unsigned long> connected_server{};
		std::atomic<unsigned long> exited_server{};
		std::atomic<unsigned long> server_now{};
		std::atomic<std::uint64_t> server_samples{};  // server_now updates so far
		std::atomic<std::uint64_t> connected_sample{}; // server_samples when the instance was created
		std::atomic<bool> connection_lost{}; // a call returned INSTANCE_LOST or SESSION_LOST
		int rebuilds{};                      // since the last good frame
		bool waiter_done{};                  // the current waiter thread has returned
		std::uint64_t waiter_generation{};   // a detached waiter from an older session sees a newer one
		std::condition_variable waiter_exit;
		// A process snapshot takes milliseconds: never on the game's Present thread.
		std::jthread server_watch{ [this](std::stop_token stop)
			{
				while (!stop.stop_requested())
				{
					server_now = steamvr_server_id();
					++server_samples;
					for (ULONGLONG slept = 0; slept < server_check_ms && !stop.stop_requested(); slept += 100)
					{
						Sleep(100);
					}
				}
			} };
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
		LARGE_INTEGER last_haptics{};

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
			if (result == XR_ERROR_INSTANCE_LOST || result == XR_ERROR_SESSION_LOST)
			{
				connection_lost = true;
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
			// After SteamVR exited, wait until the server that exited is gone or replaced: a restart
			// from the headset can bring up a new server between two retries.
			if (steamvr_restart_pending && steamvr_server_id() != exited_server)
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
			connected_sample = server_samples.load();
			connected_server = runtime_is_steamvr ? steamvr_server_id() : 0;

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
				max_layers = std::max<std::uint32_t>(properties.graphicsProperties.maxLayerCount, 1u);
				log::write("theater: headset %s, up to %u layers", properties.systemName, max_layers);
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
			// Optional: the floor (STAGE) for the body calibration (#46); without it the height comes from the span.
			space_info.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_STAGE;
			if (XR_FAILED(xrCreateReferenceSpace(session, &space_info, &stage_space)))
			{
				stage_space = XR_NULL_HANDLE;
				log::write("theater: no STAGE space (the body calibration estimates the height from the arm span)");
			}

			log::write("theater: session created on '%ls'", adapter_desc.Description);
			// Controller input is optional: without it the screen still works.
			actions_ready = gamepad_enabled && actions.create(instance, session);
			set_phase(phase::waiting_for_session);
			return true;
		}

		// Why the runtime moved the session there, as far as the two states tell.
		static const char* state_reason(XrSessionState from, XrSessionState to)
		{
			switch (to)
			{
			case XR_SESSION_STATE_IDLE: return "not running (yet)";
			case XR_SESSION_STATE_READY: return "runtime ready: beginning the session";
			case XR_SESSION_STATE_SYNCHRONIZED:
				return from == XR_SESSION_STATE_VISIBLE ? "frames no longer shown (headset off or idle, another app in front)" : "frames in sync, not shown yet";
			case XR_SESSION_STATE_VISIBLE:
				return from == XR_SESSION_STATE_FOCUSED
					? "the runtime took the controllers (SteamVR dashboard or overlay; on Quest the left menu button opens it)"
					: "shown without input (a runtime overlay has the controllers)";
			case XR_SESSION_STATE_FOCUSED: return from == XR_SESSION_STATE_VISIBLE ? "input back (dashboard or overlay closed)" : "shown with input";
			case XR_SESSION_STATE_STOPPING: return "runtime asked the session to stop";
			case XR_SESSION_STATE_LOSS_PENDING: return "connection to the runtime is going away";
			case XR_SESSION_STATE_EXITING: return "the user or runtime ended VR";
			default: return "unknown";
			}
		}

		static const char* state_name(XrSessionState value)
		{
			switch (value)
			{
			case XR_SESSION_STATE_IDLE: return "IDLE";
			case XR_SESSION_STATE_READY: return "READY";
			case XR_SESSION_STATE_SYNCHRONIZED: return "SYNCHRONIZED";
			case XR_SESSION_STATE_VISIBLE: return "VISIBLE";
			case XR_SESSION_STATE_FOCUSED: return "FOCUSED";
			case XR_SESSION_STATE_STOPPING: return "STOPPING";
			case XR_SESSION_STATE_LOSS_PENDING: return "LOSS_PENDING";
			case XR_SESSION_STATE_EXITING: return "EXITING";
			default: return "UNKNOWN";
			}
		}

		void set_input_lost(bool lost)
		{
			if (lost != input_lost.load())
			{
				log::write("theater: input focus %s", lost ? "lost: the runtime has the controllers" : "back");
				input_lost = lost;
			}
			ui::set_input_focus_lost(lost);
		}

		void handle_session_state(XrSessionState new_state)
		{
			const XrSessionState old_state = session_state;
			const ULONGLONG now = GetTickCount64();
			char detail[160]{};
			if (old_state == XR_SESSION_STATE_FOCUSED && new_state != XR_SESSION_STATE_FOCUSED)
			{
				// What the hands were doing: a held menu button points at the SteamVR dashboard.
				const input::gamepad_state pad = input::latest();
				std::snprintf(detail, sizeof(detail), "; buttons=%04X, menu button held %.2f s, %.1f s after focus came", pad.buttons,
					ui::menu_hold_seconds(), static_cast<double>(now - focused_since) / 1000.0);
				++focus_losses;
				unfocused_since = now;
			}
			else if (new_state == XR_SESSION_STATE_FOCUSED)
			{
				if (unfocused_since.load())
				{
					std::snprintf(detail, sizeof(detail), "; without input for %.1f s", static_cast<double>(now - unfocused_since.load()) / 1000.0);
				}
				focused_since = now;
				unfocused_since = 0;
			}
			log::write("theater: session %s -> %s: %s%s (focus losses %u)", state_name(old_state), state_name(new_state),
				state_reason(old_state, new_state), detail, focus_losses.load());
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
				exited_server = connected_server ? connected_server.load() : steamvr_server_id();
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
			log::write("theater: HUD swap chain %ux%u, format %d", hud.Width, hud.Height, chosen);
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

		// Images drawn on the CPU (settings panel, comfort vignette): each has its own swap chain,
		// filled only when the image changed; a quad keeps showing the last released image.
		struct cpu_overlay
		{
			const char* name{};
			XrSwapchain swapchain{ XR_NULL_HANDLE };
			std::vector<XrSwapchainImageD3D11KHR> images;
			ComPtr<ID3D11Texture2D> texture;
			std::uint32_t width{};
			std::uint32_t height{};
			bool rgba{};  // the runtime took R8G8B8A8: swap the BGRA pixels
			bool shown{}; // an image was released: the quad may be submitted
		};
		cpu_overlay panel_overlay{ "settings panel" };
		std::uint64_t panel_revision{};
		std::vector<std::uint32_t> panel_pixels;
		cpu_overlay notice_overlay{ "notice" };
		std::uint64_t notice_revision{};
		std::vector<std::uint32_t> notice_pixels;
		cpu_overlay vignette_overlay{ "vignette" };
		cpu_overlay reticle_overlay{ "reticle" };
		static constexpr std::uint32_t reticle_pixels_size = 64;
		cpu_overlay sight_dot_overlay{ "sight dot" };
		bool sight_dot_shown{}; // this frame: the aim reticle stays off (the sight has its dot)
		std::atomic<std::uint64_t> sight_dot_layers{}; // frames with a sight dot submitted (pipe status)
		cpu_overlay ring_overlay[2]{ { "grab zone" }, { "grab zone (hand inside)" } };
		cpu_overlay grenade_overlay[2]{ { "held grenade" }, { "held grenade (armed)" } };
		static constexpr std::uint32_t grenade_pixels_size = 128;
		std::atomic<std::uint64_t> grenade_layers{}; // frames the held grenade was submitted (pipe status)
		std::uint32_t max_layers{ 16 }; // the runtime's limit (OpenXR guarantees 16); more fails the frame
		static constexpr std::uint32_t ring_pixels_size = 128;
		ui::vignette comfort_vignette;
		float vignette_uploaded{ -1.0f };
		std::vector<std::uint32_t> vignette_pixels;
		LARGE_INTEGER vignette_last{};
		static constexpr std::uint32_t vignette_size = 256;

		bool ensure_overlay(cpu_overlay& overlay, std::uint32_t width, std::uint32_t height)
		{
			if (overlay.swapchain != XR_NULL_HANDLE)
			{
				return true;
			}
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
			for (const DXGI_FORMAT candidate : { DXGI_FORMAT_B8G8R8A8_UNORM_SRGB, DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, DXGI_FORMAT_B8G8R8A8_UNORM, DXGI_FORMAT_R8G8B8A8_UNORM })
			{
				if (std::find(formats.begin(), formats.end(), static_cast<std::int64_t>(candidate)) != formats.end())
				{
					chosen = candidate;
					break;
				}
			}
			if (chosen == DXGI_FORMAT_UNKNOWN)
			{
				log::write("theater: no 8-bit swap chain format for the %s", overlay.name);
				return false;
			}
			D3D11_TEXTURE2D_DESC desc{};
			desc.Width = width;
			desc.Height = height;
			desc.MipLevels = 1;
			desc.ArraySize = 1;
			desc.Format = chosen;
			desc.SampleDesc.Count = 1;
			desc.Usage = D3D11_USAGE_DEFAULT;
			desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
			if (FAILED(device->CreateTexture2D(&desc, nullptr, &overlay.texture)))
			{
				log::write("theater: %s texture failed", overlay.name);
				return false;
			}
			XrSwapchainCreateInfo info{ XR_TYPE_SWAPCHAIN_CREATE_INFO };
			info.usageFlags = XR_SWAPCHAIN_USAGE_TRANSFER_DST_BIT | XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT;
			info.format = chosen;
			info.sampleCount = 1;
			info.width = width;
			info.height = height;
			info.faceCount = 1;
			info.arraySize = 1;
			info.mipCount = 1;
			if (!check(xrCreateSwapchain(session, &info, &overlay.swapchain), "xrCreateSwapchain(overlay)")
				|| !check(xrEnumerateSwapchainImages(overlay.swapchain, 0, &count, nullptr), "xrEnumerateSwapchainImages"))
			{
				return false;
			}
			overlay.images.assign(count, XrSwapchainImageD3D11KHR{ XR_TYPE_SWAPCHAIN_IMAGE_D3D11_KHR });
			if (!check(xrEnumerateSwapchainImages(overlay.swapchain, count, &count,
					reinterpret_cast<XrSwapchainImageBaseHeader*>(overlay.images.data())), "xrEnumerateSwapchainImages"))
			{
				return false;
			}
			overlay.width = width;
			overlay.height = height;
			overlay.rgba = chosen == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB || chosen == DXGI_FORMAT_R8G8B8A8_UNORM;
			log::write("theater: %s swap chain %ux%u, format %d", overlay.name, width, height, chosen);
			return true;
		}

		// Top-down BGRA pixels into the overlay's next image (swapped in place for RGBA runtimes).
		void upload_overlay(cpu_overlay& overlay, std::vector<std::uint32_t>& pixels)
		{
			if (overlay.rgba)
			{
				for (std::uint32_t& pixel : pixels)
				{
					pixel = (pixel & 0xFF00FF00u) | ((pixel & 0xFFu) << 16) | ((pixel >> 16) & 0xFFu);
				}
			}
			ComPtr<ID3D11DeviceContext> context;
			device->GetImmediateContext(&context);
			context->UpdateSubresource(overlay.texture.Get(), 0, nullptr, pixels.data(), overlay.width * 4, 0);
			std::uint32_t index{};
			XrSwapchainImageAcquireInfo acquire{ XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO };
			if (!check(xrAcquireSwapchainImage(overlay.swapchain, &acquire, &index), "xrAcquireSwapchainImage(overlay)"))
			{
				return;
			}
			XrSwapchainImageWaitInfo wait{ XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO };
			wait.timeout = XR_INFINITE_DURATION;
			if (check(xrWaitSwapchainImage(overlay.swapchain, &wait), "xrWaitSwapchainImage(overlay)"))
			{
				context->CopyResource(overlay.images[index].texture, overlay.texture.Get());
			}
			XrSwapchainImageReleaseInfo release{ XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO };
			overlay.shown = check(xrReleaseSwapchainImage(overlay.swapchain, &release), "xrReleaseSwapchainImage(overlay)") || overlay.shown;
		}

		void destroy_overlay(cpu_overlay& overlay)
		{
			if (overlay.swapchain != XR_NULL_HANDLE)
			{
				xrDestroySwapchain(overlay.swapchain);
				overlay.swapchain = XR_NULL_HANDLE;
			}
			overlay.images.clear();
			overlay.texture.Reset();
			overlay.shown = false;
		}

		// Adds the panel quad when it is open; layers has room for it.
		void add_panel_layer(XrCompositionLayerQuad& panel_quad, const XrCompositionLayerBaseHeader** layers, std::uint32_t& layer_count)
		{
			const ui::panel_view view = ui::vr_panel_view();
			if (!view.open || !ensure_overlay(panel_overlay, ui::settings_panel::width, ui::settings_panel::height))
			{
				return;
			}
			if (ui::vr_panel_pixels(panel_revision, panel_pixels))
			{
				upload_overlay(panel_overlay, panel_pixels);
			}
			if (!panel_overlay.shown)
			{
				return;
			}
			panel_quad = { XR_TYPE_COMPOSITION_LAYER_QUAD };
			panel_quad.space = space;
			panel_quad.eyeVisibility = XR_EYE_VISIBILITY_BOTH;
			panel_quad.subImage.swapchain = panel_overlay.swapchain;
			panel_quad.subImage.imageRect.extent = { ui::settings_panel::width, ui::settings_panel::height };
			panel_quad.pose.position = { view.pose.position[0], view.pose.position[1], view.pose.position[2] };
			panel_quad.pose.orientation = { view.pose.orientation[0], view.pose.orientation[1], view.pose.orientation[2], view.pose.orientation[3] };
			panel_quad.size = { view.width, view.height };
			panel_quad.layerFlags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT; // premultiplied, see-through backdrop
			layers[layer_count++] = reinterpret_cast<const XrCompositionLayerBaseHeader*>(&panel_quad);
		}

		// The panel's one-line notice (opener hint, input focus lost): head-locked below the view centre.
		void add_notice_layer(XrCompositionLayerQuad& notice_quad, const XrCompositionLayerBaseHeader** layers, std::uint32_t& layer_count)
		{
			constexpr float width = 0.6f; // metres at 1 m
			if (!ui::vr_notice_shown() || view_space == XR_NULL_HANDLE || !ensure_overlay(notice_overlay, ui::notice_width, ui::notice_height))
			{
				return;
			}
			if (ui::vr_notice_pixels(notice_revision, notice_pixels))
			{
				upload_overlay(notice_overlay, notice_pixels);
			}
			if (!notice_overlay.shown)
			{
				return;
			}
			notice_quad = { XR_TYPE_COMPOSITION_LAYER_QUAD };
			notice_quad.space = view_space;
			notice_quad.eyeVisibility = XR_EYE_VISIBILITY_BOTH;
			notice_quad.subImage.swapchain = notice_overlay.swapchain;
			notice_quad.subImage.imageRect.extent = { ui::notice_width, ui::notice_height };
			notice_quad.pose.orientation.w = 1.0f;
			notice_quad.pose.position = { 0.0f, -0.22f, -1.0f };
			notice_quad.size = { width, width * ui::notice_height / ui::notice_width };
			layers[layer_count++] = reinterpret_cast<const XrCompositionLayerBaseHeader*>(&notice_quad);
		}

		// Magnified sights (stereo/scope): the eye pass's lens picture (premultiplied, round) on a quad at
		// the lens. Its own swap chain in the image's format, copied once per new image.
		XrSwapchain scope_swapchain{ XR_NULL_HANDLE };
		std::vector<XrSwapchainImageD3D11KHR> scope_images;
		D3D11_TEXTURE2D_DESC scope_swapchain_desc{};
		std::uint64_t scope_frame_copied{};
		bool scope_shown{};
		static constexpr ULONGLONG scope_stale_ms = 150;

		void destroy_scope_swapchain()
		{
			if (scope_swapchain != XR_NULL_HANDLE)
			{
				xrDestroySwapchain(scope_swapchain);
				scope_swapchain = XR_NULL_HANDLE;
			}
			scope_images.clear();
			scope_swapchain_desc = {};
			scope_frame_copied = 0;
			scope_shown = false;
		}

		bool ensure_scope_swapchain(const D3D11_TEXTURE2D_DESC& desc)
		{
			if (scope_swapchain != XR_NULL_HANDLE && desc.Width == scope_swapchain_desc.Width && desc.Height == scope_swapchain_desc.Height
				&& desc.Format == scope_swapchain_desc.Format)
			{
				return true;
			}
			destroy_scope_swapchain();
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
			for (const DXGI_FORMAT candidate : format_preferences(desc.Format))
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
			info.width = desc.Width;
			info.height = desc.Height;
			info.faceCount = 1;
			info.arraySize = 1;
			info.mipCount = 1;
			if (!check(xrCreateSwapchain(session, &info, &scope_swapchain), "xrCreateSwapchain(scope)")
				|| !check(xrEnumerateSwapchainImages(scope_swapchain, 0, &count, nullptr), "xrEnumerateSwapchainImages"))
			{
				return false;
			}
			scope_images.assign(count, XrSwapchainImageD3D11KHR{ XR_TYPE_SWAPCHAIN_IMAGE_D3D11_KHR });
			if (!check(xrEnumerateSwapchainImages(scope_swapchain, count, &count, reinterpret_cast<XrSwapchainImageBaseHeader*>(scope_images.data())),
					"xrEnumerateSwapchainImages"))
			{
				return false;
			}
			scope_swapchain_desc = desc;
			log::write("theater: scope swap chain %ux%u, format %d", desc.Width, desc.Height, chosen);
			return true;
		}

		// A fresh lens picture to show; the reticle stays off while it is (the lens has its own dot).
		bool scope_visible()
		{
			const stereo::scope_image image = stereo::latest_scope_image();
			return image.texture && image.frame != 0 && GetTickCount64() - image.at < scope_stale_ms;
		}

		void add_scope_layer(XrCompositionLayerQuad& scope_quad, const XrCompositionLayerBaseHeader** layers, std::uint32_t& layer_count)
		{
			const stereo::scope_image image = stereo::latest_scope_image();
			if (!scope_visible() || layer_count >= max_layers)
			{
				return;
			}
			D3D11_TEXTURE2D_DESC desc{};
			image.texture->GetDesc(&desc);
			if (!ensure_scope_swapchain(desc))
			{
				return;
			}
			if (image.frame != scope_frame_copied)
			{
				std::uint32_t index{};
				XrSwapchainImageAcquireInfo acquire{ XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO };
				if (!check(xrAcquireSwapchainImage(scope_swapchain, &acquire, &index), "xrAcquireSwapchainImage(scope)"))
				{
					return;
				}
				XrSwapchainImageWaitInfo wait{ XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO };
				wait.timeout = XR_INFINITE_DURATION;
				if (check(xrWaitSwapchainImage(scope_swapchain, &wait), "xrWaitSwapchainImage(scope)"))
				{
					ComPtr<ID3D11DeviceContext> context;
					device->GetImmediateContext(&context);
					context->CopySubresourceRegion(scope_images[index].texture, 0, 0, 0, 0, image.texture, 0, nullptr);
				}
				XrSwapchainImageReleaseInfo release{ XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO };
				scope_shown = check(xrReleaseSwapchainImage(scope_swapchain, &release), "xrReleaseSwapchainImage(scope)") || scope_shown;
				scope_frame_copied = image.frame;
			}
			if (!scope_shown)
			{
				return;
			}
			scope_quad = { XR_TYPE_COMPOSITION_LAYER_QUAD };
			scope_quad.layerFlags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;
			scope_quad.space = space;
			// Like a real eyepiece: the other eye sees the drawn optic, not a second picture.
			scope_quad.eyeVisibility = image.eye == 0 ? XR_EYE_VISIBILITY_LEFT : XR_EYE_VISIBILITY_RIGHT;
			scope_quad.subImage.swapchain = scope_swapchain;
			scope_quad.subImage.imageRect.extent = { static_cast<std::int32_t>(desc.Width), static_cast<std::int32_t>(desc.Height) };
			scope_quad.pose.position = { image.quad.position[0], image.quad.position[1], image.quad.position[2] };
			scope_quad.pose.orientation = { image.quad.orientation[0], image.quad.orientation[1], image.quad.orientation[2], image.quad.orientation[3] };
			scope_quad.size = { image.quad_size, image.quad_size };
			layers[layer_count++] = reinterpret_cast<const XrCompositionLayerBaseHeader*>(&scope_quad);
		}

		// sight_dot (#39): the reflex sight's red dot, collimated: per eye, far down the sight line from that
		// eye, shown only to an eye that looks through the lens (the game's own dot follows the view's centre).
		void add_sight_dot_layers(XrCompositionLayerQuad (&quads)[2], const XrCompositionLayerBaseHeader** layers, std::uint32_t& layer_count)
		{
			sight_dot_shown = false;
			const settings::registry& store = settings::global();
			const stereo::head_sample head = stereo::latest_head_sample();
			const weapon::sight_dot_line line = weapon::latest_sight_dot_line();
			if (!line.valid || scope_visible() || !store.boolean("sight_dot", true) || !head.valid)
			{
				return;
			}
			constexpr float distance = 50.0f;
			const float angular = store.real("sight_dot_size", 0.3f) * 3.14159265f / 180.0f;
			for (int e = 0; e < 2; ++e)
			{
				const float eye[3]{ head.eyes[e].position[0], head.eyes[e].position[1], head.eyes[e].position[2] };
				const weapon::sight_dot_view view = weapon::sight_dot_for_eye(eye, line.start, line.direction, line.lens_radius, line.max_behind, line.max_ahead, distance);
				if (!view.visible || layer_count >= max_layers || !ensure_overlay(sight_dot_overlay, reticle_pixels_size, reticle_pixels_size))
				{
					continue;
				}
				if (!sight_dot_overlay.shown)
				{
					std::vector<std::uint32_t> pixels;
					ui::sight_dot_texture(static_cast<int>(reticle_pixels_size), pixels);
					upload_overlay(sight_dot_overlay, pixels);
					if (!sight_dot_overlay.shown)
					{
						return;
					}
				}
				const ui::reticle_quad placed = ui::place_reticle(view.point, eye, angular);
				XrCompositionLayerQuad& quad = quads[e];
				quad = { XR_TYPE_COMPOSITION_LAYER_QUAD };
				quad.layerFlags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;
				quad.space = space;
				quad.eyeVisibility = e == 0 ? XR_EYE_VISIBILITY_LEFT : XR_EYE_VISIBILITY_RIGHT;
				quad.subImage.swapchain = sight_dot_overlay.swapchain;
				quad.subImage.imageRect.extent = { static_cast<std::int32_t>(reticle_pixels_size), static_cast<std::int32_t>(reticle_pixels_size) };
				quad.pose.position = { placed.pose.position[0], placed.pose.position[1], placed.pose.position[2] };
				quad.pose.orientation = { placed.pose.orientation[0], placed.pose.orientation[1], placed.pose.orientation[2], placed.pose.orientation[3] };
				quad.size = { placed.size, placed.size };
				layers[layer_count++] = reinterpret_cast<const XrCompositionLayerBaseHeader*>(&quad);
				sight_dot_shown = true;
			}
			if (sight_dot_shown)
			{
				++sight_dot_layers;
			}
		}

		// Aim reticle (#3, #12): with controller aim, a dot where the shots converge, facing the head,
		// a constant angular size; world-locked so it sits at that depth in both eyes.
		void add_reticle_layer(XrCompositionLayerQuad& reticle_quad, const XrCompositionLayerBaseHeader** layers, std::uint32_t& layer_count)
		{
			const settings::registry& store = settings::global();
			const stereo::head_sample head = stereo::latest_head_sample();
			float point[3]{};
			if (scope_visible() || sight_dot_shown || !store.boolean("reticle", true) || !head.valid || !weapon::aim_point(point)
				|| !ensure_overlay(reticle_overlay, reticle_pixels_size, reticle_pixels_size))
			{
				return;
			}
			if (!reticle_overlay.shown)
			{
				std::vector<std::uint32_t> pixels;
				ui::reticle_texture(static_cast<int>(reticle_pixels_size), pixels);
				upload_overlay(reticle_overlay, pixels);
				if (!reticle_overlay.shown)
				{
					return;
				}
			}
			const stereo::pose centre = stereo::eye_math::centre_pose(head);
			const float eye[3]{ centre.position[0], centre.position[1], centre.position[2] };
			const ui::reticle_quad placed = ui::place_reticle(point, eye, store.real("reticle_size", 1.0f) * 3.14159265f / 180.0f);
			reticle_quad = { XR_TYPE_COMPOSITION_LAYER_QUAD };
			reticle_quad.layerFlags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;
			reticle_quad.space = space;
			reticle_quad.eyeVisibility = XR_EYE_VISIBILITY_BOTH;
			reticle_quad.subImage.swapchain = reticle_overlay.swapchain;
			reticle_quad.subImage.imageRect.extent = { static_cast<std::int32_t>(reticle_pixels_size), static_cast<std::int32_t>(reticle_pixels_size) };
			reticle_quad.pose.position = { placed.pose.position[0], placed.pose.position[1], placed.pose.position[2] };
			reticle_quad.pose.orientation = { placed.pose.orientation[0], placed.pose.orientation[1], placed.pose.orientation[2], placed.pose.orientation[3] };
			reticle_quad.size = { placed.size, placed.size };
			layers[layer_count++] = reinterpret_cast<const XrCompositionLayerBaseHeader*>(&reticle_quad);
		}

		// grab_debug: every fresh grab zone as a ring at its centre, as wide as its reach, facing the head.
		void add_grab_zone_layers(XrCompositionLayerQuad (&quads)[weapon::grab_zone_count], const XrCompositionLayerBaseHeader** layers, std::uint32_t& layer_count)
		{
			weapon::grab_zone zones[weapon::grab_zone_count];
			const stereo::head_sample head = stereo::latest_head_sample();
			if (!weapon::grab_debug_enabled() || !head.valid || weapon::grab_zones(zones) == 0)
			{
				return;
			}
			for (int hot = 0; hot < 2; ++hot)
			{
				if (!ensure_overlay(ring_overlay[hot], ring_pixels_size, ring_pixels_size))
				{
					return;
				}
				if (!ring_overlay[hot].shown)
				{
					std::vector<std::uint32_t> pixels;
					ui::ring_texture(static_cast<int>(ring_pixels_size), hot != 0, pixels);
					upload_overlay(ring_overlay[hot], pixels);
					if (!ring_overlay[hot].shown)
					{
						return;
					}
				}
			}
			const stereo::pose centre = stereo::eye_math::centre_pose(head);
			const float eye[3]{ centre.position[0], centre.position[1], centre.position[2] };
			// The held grenade, the scope, the reticle, the panel and the notice come after the rings:
			// keep room for them.
			constexpr std::uint32_t reserved = 5;
			for (int i = 0; i < weapon::grab_zone_count && layer_count + reserved < max_layers; ++i)
			{
				if (!zones[i].valid)
				{
					continue;
				}
				const ui::reticle_quad placed = ui::place_reticle(zones[i].centre, eye, 0.0f);
				const cpu_overlay& overlay = ring_overlay[zones[i].hot ? 1 : 0];
				XrCompositionLayerQuad& quad = quads[i];
				quad = { XR_TYPE_COMPOSITION_LAYER_QUAD };
				quad.layerFlags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;
				quad.space = space;
				quad.eyeVisibility = XR_EYE_VISIBILITY_BOTH;
				quad.subImage.swapchain = overlay.swapchain;
				quad.subImage.imageRect.extent = { static_cast<std::int32_t>(ring_pixels_size), static_cast<std::int32_t>(ring_pixels_size) };
				quad.pose.position = { placed.pose.position[0], placed.pose.position[1], placed.pose.position[2] };
				quad.pose.orientation = { placed.pose.orientation[0], placed.pose.orientation[1], placed.pose.orientation[2], placed.pose.orientation[3] };
				quad.size = { 2.0f * zones[i].radius, 2.0f * zones[i].radius };
				layers[layer_count++] = reinterpret_cast<const XrCompositionLayerBaseHeader*>(&quad);
			}
		}

		// Comfort vignette (#10): head-locked, darkens the edge while the game turns or moves the
		// body (stereo::body_motion). Uploaded only when the strength moved noticeably.
		void add_vignette_layer(XrCompositionLayerQuad& vignette_quad, const XrCompositionLayerBaseHeader** layers, std::uint32_t& layer_count)
		{
			LARGE_INTEGER now{}, frequency{};
			QueryPerformanceCounter(&now);
			QueryPerformanceFrequency(&frequency);
			const float seconds = vignette_last.QuadPart
				? std::min(static_cast<float>(now.QuadPart - vignette_last.QuadPart) / static_cast<float>(frequency.QuadPart), 0.1f) : 0.0f;
			vignette_last = now;

			const settings::registry& store = settings::global();
			ui::vignette_config config = comfort_vignette.config();
			const std::string mode = store.text("vignette", "off");
			config.mode = mode == "turning" ? ui::vignette_mode::turning : mode == "moving" ? ui::vignette_mode::moving
				: mode == "both" ? ui::vignette_mode::both : ui::vignette_mode::off;
			config.strength = store.real("vignette_strength", 0.7f);
			comfort_vignette.configure(config);
			float yaw_rate{}, speed{};
			stereo::body_motion(yaw_rate, speed);
			const float strength = comfort_vignette.update(yaw_rate, speed, seconds);
			if (strength <= 0.01f || !ensure_overlay(vignette_overlay, vignette_size, vignette_size))
			{
				return;
			}
			if (std::fabs(strength - vignette_uploaded) > 0.02f)
			{
				ui::vignette_texture(strength, static_cast<int>(vignette_size), vignette_pixels);
				upload_overlay(vignette_overlay, vignette_pixels);
				vignette_uploaded = strength;
			}
			if (!vignette_overlay.shown)
			{
				return;
			}
			// 3.2 m wide at 1 m: past the lens edge (about 58 degrees each side), the corners darkest.
			vignette_quad = { XR_TYPE_COMPOSITION_LAYER_QUAD };
			vignette_quad.layerFlags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;
			vignette_quad.space = view_space;
			vignette_quad.eyeVisibility = XR_EYE_VISIBILITY_BOTH;
			vignette_quad.subImage.swapchain = vignette_overlay.swapchain;
			vignette_quad.subImage.imageRect.extent = { static_cast<std::int32_t>(vignette_size), static_cast<std::int32_t>(vignette_size) };
			vignette_quad.pose.orientation.w = 1.0f;
			vignette_quad.pose.position.z = -1.0f;
			vignette_quad.size = { 3.2f, 3.2f };
			layers[layer_count++] = reinterpret_cast<const XrCompositionLayerBaseHeader*>(&vignette_quad);
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

		// HUD on the body (hud_mode = body, #26/#27, docs/tasks/hud-gestures.md). The HUD texture is
		// shown in pieces: everything but the minimap and the ammo counter projected like the eye
		// images of the frame (markers on their targets), the minimap on the left forearm while it is
		// glanced at, the ammo counter beside the gun after a steady inspect. Rectangles measured at
		// 2160x2160 (2026-10-26).
		static constexpr stereo::hud_layout::uv_rect minimap_rect{ 0.013f, 0.824f, 0.154f, 0.997f };
		static constexpr stereo::hud_layout::uv_rect ammo_rect{ 0.877f, 0.912f, 0.993f, 0.995f };
		// The texture split into four pieces without overlap, each with a hud_show_<piece> toggle (#28):
		// main = crosshair, markers, objective text; bottom = weapon/stance icons between minimap and ammo.
		static constexpr stereo::hud_layout::uv_rect upper_rect{ 0.0f, 0.0f, 1.0f, 0.82f };
		static constexpr stereo::hud_layout::uv_rect lower_middle_rect{ 0.16f, 0.82f, 0.87f, 1.0f };
		static constexpr stereo::hud_layout::uv_rect minimap_piece{ 0.0f, 0.82f, 0.16f, 1.0f };
		static constexpr stereo::hud_layout::uv_rect ammo_piece{ 0.87f, 0.82f, 1.0f, 1.0f };
		input::gestures::forearm_glance minimap_gesture;
		input::gestures::gun_inspect ammo_gesture;
		LARGE_INTEGER last_gesture_time{};

		void fill_quad(XrCompositionLayerQuad& layer, const stereo::hud_layout::quad& placed, XrSpace in_space = XR_NULL_HANDLE)
		{
			layer = { XR_TYPE_COMPOSITION_LAYER_QUAD };
			layer.layerFlags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;
			layer.space = in_space != XR_NULL_HANDLE ? in_space : space;
			layer.eyeVisibility = XR_EYE_VISIBILITY_BOTH;
			layer.subImage.swapchain = hud_swapchain;
			const auto w = static_cast<float>(hud_swapchain_width);
			const auto h = static_cast<float>(hud_swapchain_height);
			layer.subImage.imageRect.offset = { static_cast<std::int32_t>(placed.rect.u0 * w), static_cast<std::int32_t>(placed.rect.v0 * h) };
			layer.subImage.imageRect.extent = { static_cast<std::int32_t>((placed.rect.u1 - placed.rect.u0) * w),
				static_cast<std::int32_t>((placed.rect.v1 - placed.rect.v0) * h) };
			const stereo::pose& p = placed.placement;
			layer.pose.position = { p.position[0], p.position[1], p.position[2] };
			layer.pose.orientation = { p.orientation[0], p.orientation[1], p.orientation[2], p.orientation[3] };
			layer.size = { placed.width, placed.height };
		}

		// hud_mode = head: the pieces at their places on the head-locked quad (VIEW space).
		int head_hud_layers(XrCompositionLayerQuad (&out)[4])
		{
			settings::registry& store = settings::global();
			const float distance = store.real("hud_distance", 1.5f);
			const float width = store.real("hud_width", 1.3f);
			const float height = width * static_cast<float>(hud_swapchain_height) / static_cast<float>(hud_swapchain_width);
			const struct { const char* toggle; stereo::hud_layout::uv_rect rect; } pieces[] = {
				{ "hud_show_main", upper_rect }, { "hud_show_bottom", lower_middle_rect },
				{ "hud_show_minimap", minimap_piece }, { "hud_show_ammo", ammo_piece },
			};
			int count = 0;
			for (const auto& piece : pieces)
			{
				if (!store.boolean(piece.toggle, true))
				{
					continue;
				}
				stereo::hud_layout::quad placed;
				placed.rect = piece.rect;
				placed.width = (piece.rect.u1 - piece.rect.u0) * width;
				placed.height = (piece.rect.v1 - piece.rect.v0) * height;
				placed.placement.position[0] = (0.5f * (piece.rect.u0 + piece.rect.u1) - 0.5f) * width;
				placed.placement.position[1] = (0.5f - 0.5f * (piece.rect.v0 + piece.rect.v1)) * height;
				placed.placement.position[2] = -distance;
				fill_quad(out[count++], placed, view_space);
			}
			return count;
		}

		int body_hud_layers(XrCompositionLayerQuad (&out)[4])
		{
			if (!eye_ready[0] || !eye_ready[1] || !hud_swapchain_width || !hud_swapchain_height)
			{
				return 0;
			}
			settings::registry& store = settings::global();
			// The frame's rendered view: the eye midpoint with the left eye's orientation.
			stereo::pose view = eye_poses[0];
			for (int axis = 0; axis < 3; ++axis)
			{
				view.position[axis] = 0.5f * (eye_poses[0].position[axis] + eye_poses[1].position[axis]);
			}
			const float distance = store.real("hud_marker_distance", 10.0f);
			int count = 0;
			if (store.boolean("hud_show_main", true))
			{
				fill_quad(out[count++], stereo::hud_layout::projected(view, eye_fovs[0], distance, upper_rect));
			}
			if (store.boolean("hud_show_bottom", true))
			{
				fill_quad(out[count++], stereo::hud_layout::projected(view, eye_fovs[0], distance, lower_middle_rect));
			}

			LARGE_INTEGER now{}, frequency{};
			QueryPerformanceCounter(&now);
			QueryPerformanceFrequency(&frequency);
			const float seconds = last_gesture_time.QuadPart
				? std::fmin(0.1f, static_cast<float>(now.QuadPart - last_gesture_time.QuadPart) / static_cast<float>(frequency.QuadPart)) : 0.0f;
			last_gesture_time = now;
			const stereo::head_sample head_sample = stereo::latest_head_sample();
			const stereo::hand_sample hands = stereo::latest_hand_sample();
			if (!head_sample.valid)
			{
				return count;
			}
			const stereo::pose head = stereo::eye_math::centre_pose(head_sample);
			const float eye[3] = { head.position[0], head.position[1], head.position[2] };
			const float aspect = static_cast<float>(hud_swapchain_width) / static_cast<float>(hud_swapchain_height);
			if (minimap_gesture.update(head, hands.grip[0], hands.valid[0], seconds) && store.boolean("hud_show_minimap", true))
			{
				const float offset[3] = { 0.03f, 0.0f, 0.12f }; // above the inner forearm, towards the elbow
				fill_quad(out[count++], stereo::hud_layout::on_body(hands.grip[0], offset, eye, store.real("hud_watch_width", 0.12f), minimap_rect, aspect));
			}
			if (ammo_gesture.update(head, hands.grip[1], hands.valid[1], seconds) && store.boolean("hud_show_ammo", true))
			{
				const float offset[3] = { -0.06f, 0.05f, -0.10f }; // beside the gun's left side
				fill_quad(out[count++], stereo::hud_layout::on_body(hands.grip[1], offset, eye, store.real("hud_ammo_width", 0.10f), ammo_rect, aspect));
			}
			return count;
		}

		// The pointer's beam and the dot where it hits: two quads from one static 4x4 swap chain of a
		// single colour.
		XrSwapchain pointer_swapchain{ XR_NULL_HANDLE };
		bool pointer_swapchain_failed{};
		XrCompositionLayerQuad pointer_beam{ XR_TYPE_COMPOSITION_LAYER_QUAD };
		XrCompositionLayerQuad pointer_dot{ XR_TYPE_COMPOSITION_LAYER_QUAD };
		bool pointer_beam_visible{};
		bool pointer_dot_visible{};

		bool ensure_pointer_swapchain()
		{
			if (pointer_swapchain != XR_NULL_HANDLE || pointer_swapchain_failed)
			{
				return pointer_swapchain != XR_NULL_HANDLE;
			}
			pointer_swapchain_failed = true; // one attempt per session
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
			for (const DXGI_FORMAT candidate : { DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_FORMAT_B8G8R8A8_UNORM_SRGB, DXGI_FORMAT_B8G8R8A8_UNORM })
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
			info.createFlags = XR_SWAPCHAIN_CREATE_STATIC_IMAGE_BIT;
			info.usageFlags = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT;
			info.format = chosen;
			info.sampleCount = 1;
			info.width = info.height = 4;
			info.faceCount = info.arraySize = info.mipCount = 1;
			XrSwapchain created{ XR_NULL_HANDLE };
			if (!check(xrCreateSwapchain(session, &info, &created), "xrCreateSwapchain(pointer)"))
			{
				return false;
			}
			std::vector<XrSwapchainImageD3D11KHR> pointer_images;
			std::uint32_t index{};
			XrSwapchainImageAcquireInfo acquire{ XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO };
			XrSwapchainImageWaitInfo wait{ XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO };
			wait.timeout = XR_INFINITE_DURATION;
			bool filled = check(xrEnumerateSwapchainImages(created, 0, &count, nullptr), "xrEnumerateSwapchainImages");
			if (filled)
			{
				pointer_images.assign(count, XrSwapchainImageD3D11KHR{ XR_TYPE_SWAPCHAIN_IMAGE_D3D11_KHR });
				filled = check(xrEnumerateSwapchainImages(created, count, &count, reinterpret_cast<XrSwapchainImageBaseHeader*>(pointer_images.data())), "xrEnumerateSwapchainImages")
					&& check(xrAcquireSwapchainImage(created, &acquire, &index), "xrAcquireSwapchainImage(pointer)")
					&& check(xrWaitSwapchainImage(created, &wait), "xrWaitSwapchainImage(pointer)");
			}
			if (filled)
			{
				ComPtr<ID3D11RenderTargetView> target;
				D3D11_RENDER_TARGET_VIEW_DESC view{};
				view.Format = chosen;
				view.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
				ComPtr<ID3D11DeviceContext> context;
				device->GetImmediateContext(&context);
				const float colour[4] = { 0.25f, 0.85f, 1.0f, 1.0f };
				if (SUCCEEDED(device->CreateRenderTargetView(pointer_images[index].texture, &view, &target)))
				{
					context->ClearRenderTargetView(target.Get(), colour);
				}
				XrSwapchainImageReleaseInfo release{ XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO };
				filled = check(xrReleaseSwapchainImage(created, &release), "xrReleaseSwapchainImage(pointer)");
			}
			if (!filled)
			{
				xrDestroySwapchain(created);
				return false;
			}
			pointer_swapchain = created;
			pointer_swapchain_failed = false;
			log::write("theater: pointer beam ready");
			return true;
		}

		void destroy_pointer_swapchain()
		{
			if (pointer_swapchain != XR_NULL_HANDLE)
			{
				xrDestroySwapchain(pointer_swapchain);
				pointer_swapchain = XR_NULL_HANDLE;
			}
			pointer_swapchain_failed = false;
		}

		// Beam from the controller to where it points (or 3 m along the ray), turned about its own axis
		// to face the head; a 3 cm dot on the screen at the hit.
		void place_pointer_visual(const stereo::pose& aim, const input::pointer_hit& hit)
		{
			pointer_beam_visible = pointer_dot_visible = false;
			if (!ensure_pointer_swapchain())
			{
				return;
			}
			const float* q = aim.orientation;
			const float* p = aim.position;
			// The ray: -z of the aim rotation.
			const float d[3] = { -2.0f * (q[0] * q[2] + q[1] * q[3]), -2.0f * (q[1] * q[2] - q[0] * q[3]), -(1.0f - 2.0f * (q[0] * q[0] + q[1] * q[1])) };
			const float length = hit.hit ? hit.distance : 3.0f;
			const float end[3] = { p[0] + d[0] * length, p[1] + d[1] * length, p[2] + d[2] * length };
			float head[3]{};
			if (const stereo::head_sample h = stereo::latest_head_sample(); h.valid)
			{
				for (int i = 0; i < 3; ++i)
				{
					head[i] = 0.5f * (h.eyes[0].position[i] + h.eyes[1].position[i]);
				}
			}
			// Basis: x along the ray, z (the quad's normal) toward the head, y = z cross x.
			float z[3] = { head[0] - 0.5f * (p[0] + end[0]), head[1] - 0.5f * (p[1] + end[1]), head[2] - 0.5f * (p[2] + end[2]) };
			const float along = z[0] * d[0] + z[1] * d[1] + z[2] * d[2];
			for (int i = 0; i < 3; ++i)
			{
				z[i] -= along * d[i];
			}
			const float zl = std::sqrt(z[0] * z[0] + z[1] * z[1] + z[2] * z[2]);
			if (zl < 1e-4f)
			{
				return;
			}
			for (float& c : z)
			{
				c /= zl;
			}
			const float y[3] = { z[1] * d[2] - z[2] * d[1], z[2] * d[0] - z[0] * d[2], z[0] * d[1] - z[1] * d[0] };
			// Rotation matrix with columns (d, y, z) to a quaternion.
			const float m00 = d[0], m11 = y[1], m22 = z[2];
			float w{}, x{}, yy{}, zz{};
			const float trace = m00 + m11 + m22;
			if (trace > 0.0f)
			{
				const float s = 0.5f / std::sqrt(trace + 1.0f);
				w = 0.25f / s;
				x = (y[2] - z[1]) * s;
				yy = (z[0] - d[2]) * s;
				zz = (d[1] - y[0]) * s;
			}
			else if (m00 > m11 && m00 > m22)
			{
				const float s = 2.0f * std::sqrt(1.0f + m00 - m11 - m22);
				w = (y[2] - z[1]) / s;
				x = 0.25f * s;
				yy = (y[0] + d[1]) / s;
				zz = (z[0] + d[2]) / s;
			}
			else if (m11 > m22)
			{
				const float s = 2.0f * std::sqrt(1.0f + m11 - m00 - m22);
				w = (z[0] - d[2]) / s;
				x = (y[0] + d[1]) / s;
				yy = 0.25f * s;
				zz = (z[1] + y[2]) / s;
			}
			else
			{
				const float s = 2.0f * std::sqrt(1.0f + m22 - m00 - m11);
				w = (d[1] - y[0]) / s;
				x = (z[0] + d[2]) / s;
				yy = (z[1] + y[2]) / s;
				zz = 0.25f * s;
			}
			const auto fill = [&](XrCompositionLayerQuad& layer)
			{
				layer = { XR_TYPE_COMPOSITION_LAYER_QUAD };
				layer.space = space;
				layer.eyeVisibility = XR_EYE_VISIBILITY_BOTH;
				layer.subImage.swapchain = pointer_swapchain;
				layer.subImage.imageRect.extent = { 4, 4 };
			};
			fill(pointer_beam);
			pointer_beam.pose.position = { 0.5f * (p[0] + end[0]), 0.5f * (p[1] + end[1]), 0.5f * (p[2] + end[2]) };
			pointer_beam.pose.orientation = { x, yy, zz, w };
			pointer_beam.size = { length, 0.004f };
			pointer_beam_visible = true;
			if (hit.hit)
			{
				fill(pointer_dot);
				pointer_dot.pose.position = { hit.point[0], hit.point[1], hit.point[2] + 0.005f };
				pointer_dot.pose.orientation.w = 1.0f;
				pointer_dot.size = { 0.03f, 0.03f };
				pointer_dot_visible = true;
			}
		}

		// The settings panel's pointer: the same beam, the dot lying on the panel (a smaller one: the
		// panel is 0.8 m away, the screen several metres).
		void add_panel_pointer(const XrCompositionLayerBaseHeader** layers, std::uint32_t& layer_count)
		{
			const ui::panel_view view = ui::vr_panel_view();
			if (!view.open || !view.pointing)
			{
				return;
			}
			place_pointer_visual(view.aim, view.hit);
			if (pointer_dot_visible)
			{
				const float* q = view.pose.orientation;
				// The panel's normal (+z of its rotation), towards the head.
				const float n[3] = { 2.0f * (q[0] * q[2] + q[1] * q[3]), 2.0f * (q[1] * q[2] - q[0] * q[3]), 1.0f - 2.0f * (q[0] * q[0] + q[1] * q[1]) };
				pointer_dot.pose.position = { view.hit.point[0] + 0.003f * n[0], view.hit.point[1] + 0.003f * n[1], view.hit.point[2] + 0.003f * n[2] };
				pointer_dot.pose.orientation = { q[0], q[1], q[2], q[3] };
				pointer_dot.size = { 0.012f, 0.012f };
			}
			if (pointer_beam_visible)
			{
				layers[layer_count++] = reinterpret_cast<const XrCompositionLayerBaseHeader*>(&pointer_beam);
			}
			if (pointer_dot_visible)
			{
				layers[layer_count++] = reinterpret_cast<const XrCompositionLayerBaseHeader*>(&pointer_dot);
			}
		}

		// Laser pointer on the theater screen (#23, menu_input = pointer): the hand whose trigger was
		// pulled last aims; its trigger is the left mouse button.
		int pointer_hand{ 1 };
		bool menu_shown{};
		// A menu over a level (pause, crates) is shown in front of where the head looked when it came
		// up: at a crate the player faces the crate, not where tracking started (#34).
		bool menu_screen_placed{};
		stereo::pose menu_screen_pose;

		// Menus over a running level (the pause menu) are not part of the eye images: the game draws
		// them straight into its image, in the 16:9 band with exact black above and below, while the
		// world fills the whole square. Two rows near the top and bottom edge are read back (async,
		// every 4th frame); both exactly black = a menu is up, and the flat screen is shown instead.
		ComPtr<ID3D11Texture2D> letterbox_rows;
		bool letterbox_pending{};
		bool letterboxed{};
		unsigned letterbox_tick{};

		void watch_letterbox(ID3D11Texture2D* back_buffer)
		{
			D3D11_TEXTURE2D_DESC desc{};
			back_buffer->GetDesc(&desc);
			const bool four_bytes = desc.Format == DXGI_FORMAT_R8G8B8A8_UNORM || desc.Format == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB
				|| desc.Format == DXGI_FORMAT_R8G8B8A8_TYPELESS || desc.Format == DXGI_FORMAT_B8G8R8A8_UNORM
				|| desc.Format == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB || desc.Format == DXGI_FORMAT_B8G8R8A8_TYPELESS
				|| desc.Format == DXGI_FORMAT_R10G10B10A2_UNORM;
			if (!four_bytes || desc.SampleDesc.Count != 1 || desc.Height < 400)
			{
				return;
			}
			ComPtr<ID3D11DeviceContext> context;
			device->GetImmediateContext(&context);
			D3D11_TEXTURE2D_DESC have{};
			if (letterbox_rows)
			{
				letterbox_rows->GetDesc(&have);
			}
			if (!letterbox_rows || have.Width != desc.Width || have.Format != desc.Format)
			{
				letterbox_rows.Reset();
				letterbox_pending = false;
				D3D11_TEXTURE2D_DESC rows{};
				rows.Width = desc.Width;
				rows.Height = 2;
				rows.MipLevels = rows.ArraySize = 1;
				rows.Format = desc.Format;
				rows.SampleDesc.Count = 1;
				rows.Usage = D3D11_USAGE_STAGING;
				rows.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
				if (FAILED(device->CreateTexture2D(&rows, nullptr, &letterbox_rows)))
				{
					return;
				}
			}
			if (letterbox_pending)
			{
				D3D11_MAPPED_SUBRESOURCE mapped{};
				const HRESULT hr = context->Map(letterbox_rows.Get(), 0, D3D11_MAP_READ, D3D11_MAP_FLAG_DO_NOT_WAIT, &mapped);
				if (hr == DXGI_ERROR_WAS_STILL_DRAWING)
				{
					return;
				}
				if (SUCCEEDED(hr))
				{
					bool black = true;
					for (UINT row = 0; row < 2 && black; ++row)
					{
						const auto* pixels = static_cast<const std::uint32_t*>(mapped.pData) + row * (mapped.RowPitch / 4);
						for (UINT x = desc.Width / 64; x < desc.Width; x += desc.Width / 32)
						{
							if ((pixels[x] & 0x00FFFFFFu) != 0)
							{
								black = false;
								break;
							}
						}
					}
					context->Unmap(letterbox_rows.Get(), 0);
					letterboxed = black;
				}
				letterbox_pending = false;
			}
			if (++letterbox_tick % 4 != 0)
			{
				return;
			}
			const UINT rows_y[2] = { desc.Height / 20, desc.Height - desc.Height / 20 };
			for (UINT row = 0; row < 2; ++row)
			{
				const D3D11_BOX box{ 0, rows_y[row], 0, desc.Width, rows_y[row] + 1, 1 };
				context->CopySubresourceRegion(letterbox_rows.Get(), 0, 0, row, 0, back_buffer, 0, &box);
			}
			letterbox_pending = true;
		}
		void point_at_screen(float width, float height, const stereo::pose& screen_pose)
		{
			pointer_beam_visible = pointer_dot_visible = false;
			if (settings::global().text("menu_input", "pointer") != "pointer")
			{
				return;
			}
			const input::gamepad_state pad = input::latest();
			const bool triggers[2] = { pad.left_trigger > 128, pad.right_trigger > 128 };
			if (triggers[0] != triggers[1])
			{
				pointer_hand = triggers[0] ? 0 : 1;
			}
			const stereo::hand_sample hands = stereo::latest_hand_sample();
			if (!hands.valid[pointer_hand])
			{
				return;
			}
			input::quad_target screen;
			screen.pose = screen_pose;
			screen.width = width;
			screen.height = height;
			const input::pointer_hit hit = input::point_at(hands.aim[pointer_hand], screen);
			place_pointer_visual(hands.aim[pointer_hand], hit);
			if (hit.hit)
			{
				hooks::point_game_cursor(hit.u, hit.v, triggers[pointer_hand]);
			}
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
			if (stage_space != XR_NULL_HANDLE)
			{
				XrSpaceLocation floor{ XR_TYPE_SPACE_LOCATION };
				const bool located = XR_SUCCEEDED(xrLocateSpace(stage_space, space, display_time, &floor))
					&& (floor.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT);
				stereo::publish_floor(located, located ? floor.pose.position.y : 0.0f);
			}
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
				frame_errors = 0;
				destroy_all();
				if (++rebuilds > max_rebuilds)
				{
					log::write("theater: %d rebuilds without a good frame; disabling theater mode", max_rebuilds);
					set_phase(phase::failed);
				}
				else
				{
					log::write("theater: %d consecutive frame errors; rebuilding the connection (%d of %d)", max_frame_errors, rebuilds, max_rebuilds);
					retry_later();
				}
			}
		}

		// The connection is dead or belongs to a SteamVR that is gone: rebuild it, choosing the runtime again.
		void rebuild_if_lost()
		{
			const unsigned long connected = connected_server.load();
			const unsigned long now = server_now.load();
			// Only a sample begun after the instance was created counts.
			const bool fresh = server_samples.load() >= connected_sample.load() + 2;
			const bool server_replaced = instance != XR_NULL_HANDLE && runtime_is_steamvr && connected != 0 && fresh && now != connected;
			if ((!connection_lost && !server_replaced) || current == phase::stopped || current == phase::failed)
			{
				return;
			}
			if (server_replaced)
			{
				log::write("theater: SteamVR's server %s (pid %lu, now %lu); rebuilding the connection",
					now ? "was restarted" : "is gone", connected, now);
			}
			else
			{
				log::write("theater: the runtime lost the instance or session; rebuilding the connection");
			}
			connection_lost = false;
			destroy_all();
			retry_later();
		}

		// Under waiter_lock: this waiter was told to stop, or was given up on and a newer one runs.
		bool waiter_stale(std::uint64_t generation) const
		{
			return waiter_stop || generation != waiter_generation;
		}

		bool waiter_stop_requested(std::uint64_t generation)
		{
			const std::lock_guard guard(waiter_lock);
			return waiter_stale(generation);
		}

		// Only touches the session it was started for, so a waiter given up on (stop_waiter) never
		// reaches a newer one.
		void waiter_loop(std::uint64_t generation, XrSession waited)
		{
			struct on_exit
			{
				state* owner;
				std::uint64_t generation;
				~on_exit()
				{
					{
						const std::lock_guard guard(owner->waiter_lock);
						if (generation == owner->waiter_generation)
						{
							owner->waiter_done = true;
						}
					}
					owner->waiter_exit.notify_all();
				}
			} const exit_signal{ this, generation };

			bool wait_again = false; // the previous frame was completed here; no request needed
			for (;;)
			{
				{
					std::unique_lock guard(waiter_lock);
					if (!wait_again)
					{
						waiter_wake.wait(guard, [this, generation] { return waiter_stale(generation) || wait_requested; });
					}
					if (waiter_stale(generation))
					{
						return; // a pending request belongs to a newer waiter
					}
					if (!wait_again)
					{
						wait_requested = false;
					}
				}
				wait_again = false;

				XrFrameWaitInfo wait_info{ XR_TYPE_FRAME_WAIT_INFO };
				XrFrameState frame{ XR_TYPE_FRAME_STATE };
				const XrResult result = xrWaitFrame(waited, &wait_info, &frame);
				if (waiter_stop_requested(generation))
				{
					return;
				}
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
						if (waiter_stop_requested(generation))
						{
							return;
						}
						Sleep(1);
					}
					if (waiter_stop_requested(generation))
					{
						return;
					}
					XrFrameBeginInfo begin_info{ XR_TYPE_FRAME_BEGIN_INFO };
					XrFrameEndInfo end_info{ XR_TYPE_FRAME_END_INFO };
					end_info.displayTime = frame.predictedDisplayTime;
					end_info.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
					const XrResult begun = xrBeginFrame(waited, &begin_info);
					const XrResult ended = XR_SUCCEEDED(begun) ? xrEndFrame(waited, &end_info) : begun;
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
				if (waiter_stale(generation))
				{
					return;
				}
				ready_result = result;
				ready_frame = frame;
				frame_ready = true;
			}
		}

		void start_waiter()
		{
			stop_waiter();
			std::uint64_t generation{};
			{
				const std::lock_guard guard(waiter_lock);
				waiter_stop = false;
				waiter_done = false;
				frame_ready = false;
				wait_requested = true;
				generation = ++waiter_generation;
			}
			latest_display_time = 0;
			waiter = std::thread([this, generation, waited = session] { waiter_loop(generation, waited); });
		}

		// Must run before the session is ended or destroyed; xrWaitFrame returns at the next headset
		// frame. A runtime that died under the game may never return it: then the waiter is left
		// behind (it never touches a newer session) instead of freezing the game's Present.
		void stop_waiter()
		{
			if (!waiter.joinable())
			{
				return;
			}
			std::unique_lock guard(waiter_lock);
			waiter_stop = true;
			guard.unlock();
			waiter_wake.notify_all();
			guard.lock();
			const bool returned = waiter_exit.wait_for(guard, waiter_stop_timeout, [this] { return waiter_done; });
			guard.unlock();
			if (returned)
			{
				waiter.join();
			}
			else
			{
				log::write("theater: the runtime did not return xrWaitFrame within %lld s; leaving the frame waiter behind",
					static_cast<long long>(waiter_stop_timeout.count()));
				waiter.detach();
			}
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
			XrCompositionLayerProjection projection{ XR_TYPE_COMPOSITION_LAYER_PROJECTION };
			XrCompositionLayerProjectionView views[2]{ { XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW }, { XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW } };
			XrCompositionLayerQuad body_quads[4]{};
			XrCompositionLayerQuad panel_quad{ XR_TYPE_COMPOSITION_LAYER_QUAD };
			XrCompositionLayerQuad vignette_quad{ XR_TYPE_COMPOSITION_LAYER_QUAD };
			XrCompositionLayerQuad reticle_quad{ XR_TYPE_COMPOSITION_LAYER_QUAD };
			XrCompositionLayerQuad zone_quads[weapon::grab_zone_count]{};
			XrCompositionLayerQuad notice_quad{ XR_TYPE_COMPOSITION_LAYER_QUAD };
			XrCompositionLayerQuad scope_quad{ XR_TYPE_COMPOSITION_LAYER_QUAD };
			XrCompositionLayerQuad sight_dot_quads[2]{};
			const XrCompositionLayerBaseHeader* layers[16 + weapon::grab_zone_count]{};
			std::uint32_t layer_count = 0;
			// The pause menu (and other menus over a running level) is drawn straight into the image, not
			// on a head-tracked frame: show the flat screen with the pointer while it is up.
			if (stereo_mode && back_buffer)
			{
				watch_letterbox(back_buffer.Get());
			}
			const bool ui_on_screen = stereo_mode && letterboxed;
			if (ui_on_screen != menu_shown)
			{
				menu_shown = ui_on_screen;
				menu_screen_placed = false;
				if (const stereo::head_sample head = stereo::latest_head_sample(); ui_on_screen && head.valid)
				{
					menu_screen_pose = input::screen_in_front(stereo::eye_math::centre_pose(head), screen_distance);
					menu_screen_placed = true;
				}
				log::write("theater: %s", ui_on_screen ? "menu over the level: flat screen (in front of the head)" : "back to the level: stereo");
			}
			const bool stereo_frame = stereo_mode && can_copy && eye_ready[0] && eye_ready[1] && !ui_on_screen;
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
				// Under the HUD: the HUD stays readable while the edge darkens.
				add_vignette_layer(vignette_quad, layers, layer_count);

				if (hud_mode && settings::global().text("hud_mode", "body") == "body" && copy_hud_to_swapchain())
				{
					const int added = body_hud_layers(body_quads);
					for (int i = 0; i < added; ++i)
					{
						layers[layer_count++] = reinterpret_cast<const XrCompositionLayerBaseHeader*>(&body_quads[i]);
					}
				}
				else if (hud_mode && copy_hud_to_swapchain())
				{
					// The texture holds the HUD over transparent black: premultiplied alpha.
					const int added = head_hud_layers(body_quads);
					for (int i = 0; i < added; ++i)
					{
						layers[layer_count++] = reinterpret_cast<const XrCompositionLayerBaseHeader*>(&body_quads[i]);
					}
				}
				add_grab_zone_layers(zone_quads, layers, layer_count);
				add_scope_layer(scope_quad, layers, layer_count);
				add_sight_dot_layers(sight_dot_quads, layers, layer_count);
				add_reticle_layer(reticle_quad, layers, layer_count);
			}
			else if (frame.shouldRender && can_copy && copy_to_swapchain(back_buffer.Get()))
			{
				quad.space = space;
				quad.eyeVisibility = XR_EYE_VISIBILITY_BOTH;
				quad.subImage.swapchain = swapchain;
				quad.subImage.imageRect.extent = { static_cast<std::int32_t>(swapchain_width), static_cast<std::int32_t>(swapchain_height) };
				stereo::pose screen_pose;
				screen_pose.position[2] = -screen_distance;
				if (menu_shown && menu_screen_placed)
				{
					screen_pose = menu_screen_pose;
				}
				quad.pose.position = { screen_pose.position[0], screen_pose.position[1], screen_pose.position[2] };
				quad.pose.orientation = { screen_pose.orientation[0], screen_pose.orientation[1], screen_pose.orientation[2], screen_pose.orientation[3] };
				quad.size = { screen_width, screen_width * static_cast<float>(swapchain_height) / static_cast<float>(swapchain_width) };
				layers[0] = reinterpret_cast<const XrCompositionLayerBaseHeader*>(&quad);
				if (!ui::vr_panel_view().open)
				{
					point_at_screen(quad.size.width, quad.size.height, screen_pose);
				}
				else
				{
					pointer_beam_visible = pointer_dot_visible = false; // the panel has the pointer
				}
				layer_count = 1;
				if (pointer_beam_visible)
				{
					layers[layer_count++] = reinterpret_cast<const XrCompositionLayerBaseHeader*>(&pointer_beam);
				}
				if (pointer_dot_visible)
				{
					layers[layer_count++] = reinterpret_cast<const XrCompositionLayerBaseHeader*>(&pointer_dot);
				}
			}
			if (layer_count)
			{
				add_panel_layer(panel_quad, layers, layer_count);
				add_panel_pointer(layers, layer_count);
				add_notice_layer(notice_quad, layers, layer_count);
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
			rebuilds = 0;
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
			destroy_scope_swapchain();
			destroy_overlay(panel_overlay);
			destroy_overlay(notice_overlay);
			destroy_overlay(vignette_overlay);
			destroy_overlay(reticle_overlay);
			for (cpu_overlay& overlay : ring_overlay)
			{
				destroy_overlay(overlay);
			}
			for (cpu_overlay& overlay : grenade_overlay)
			{
				destroy_overlay(overlay);
			}
			destroy_pointer_swapchain();
			if (stage_space != XR_NULL_HANDLE)
			{
				xrDestroySpace(stage_space);
				stage_space = XR_NULL_HANDLE;
				stereo::publish_floor(false, 0.0f);
			}
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
			set_input_lost(false);
			stereo::publish_hand_sample({});
			input::publish({});
			if (instance != XR_NULL_HANDLE)
			{
				xrDestroyInstance(instance);
				instance = XR_NULL_HANDLE;
			}
			connected_server = 0;
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
		s.rebuild_if_lost();
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
				const float since = s.last_haptics.QuadPart ? static_cast<float>(t0.QuadPart - s.last_haptics.QuadPart) / static_cast<float>(frequency.QuadPart) : 0.0f;
				s.last_haptics = t0;
				s.actions.vibrate(s.session, input::haptics_frame(std::fmin(since, 0.1f)));
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
			s.set_input_lost(s.session_state == XR_SESSION_STATE_VISIBLE || (s.actions_ready && !s.actions.focused()));
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
			s.set_input_lost(false);
		}
		s.enter("idle");
	}

	theater_status theater::status() const
	{
		const state& s = *state_;
		const char* stage = s.stage.load();
		const double stage_ms = std::strcmp(stage, "idle") == 0 ? 0.0 : static_cast<double>(GetTickCount64() - s.stage_since.load());
		return { phase_name(s.current), s.presents_seen, s.frames_submitted, static_cast<int>(s.session_state),
			s.reported_sync_ms, s.reported_frame_ms, s.stereo_mode, s.eye_images_stored, stage, stage_ms, s.menu_shown,
			state::state_name(s.session_state), s.session_state == XR_SESSION_STATE_FOCUSED && !s.input_lost.load(), s.focus_losses.load(),
			s.unfocused_since.load() ? static_cast<double>(GetTickCount64() - s.unfocused_since.load()) / 1000.0 : 0.0, s.grenade_layers.load(), s.sight_dot_layers.load() };
	}
}
