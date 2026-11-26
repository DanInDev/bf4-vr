#include "weapon/ladders.hpp"

#include <windows.h>
#include <Xinput.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <mutex>
#include <thread>
#include <vector>

#include "body/arm_pose.hpp"
#include "common/controlled.hpp"
#include "common/log.hpp"
#include "input/haptics.hpp"
#include "ui/vr_panel.hpp"
#include "weapon/ladder_climb.hpp"
#include "weapon/physical_grenade.hpp"
#include "weapon/physical_reload.hpp"
#include "weapon/weapon_swap.hpp"

namespace bfvr::weapon
{
	namespace
	{
		std::atomic<bool> enabled{ true };
		std::atomic<bool> claims[2]{};
		// The stick for the game (XInput hook thread): 0 none, 1 climb (y), 2 walk on (x, y).
		std::atomic<int> stick_mode{};
		std::atomic<float> stick_x{}, stick_y{};
		// The game's aim into the ladder (camera hook thread): on, and the world direction.
		std::atomic<bool> aim_on{};
		std::atomic<float> aim_x{}, aim_z{};

#if defined(_M_X64)
		// BF4 179547 (re/notes/ladders.md, re/symbols/bf4-179547.md "Ladders").
		constexpr std::uintptr_t soldier_physics_entity = 0x1b8;   // CharacterPhysicsEntity*
		constexpr std::uintptr_t physics_state = 0x78;             // its character state object
		constexpr std::uintptr_t state_type = 0x20;                // CharacterStateType: 3 Climbing
		constexpr std::uintptr_t state_ladder_foot = 0xd0;         // Vec4: the ladder's foot (its entity's origin)
		constexpr std::uintptr_t state_ladder_out = 0xe0;          // Vec4: out of the wall, towards the climber
		constexpr std::uintptr_t state_ladder_length = 0xf0;       // float: its rails' length
		constexpr std::uintptr_t physics_character = 0x138;        // -> +0x1a0 Vec3 the character's position
		constexpr std::uintptr_t character_position = 0x1a0;
		constexpr std::uintptr_t soldier_data = 0x30;              // SoldierEntityData*
		constexpr std::uintptr_t data_physics = 0x118;             // CharacterPhysicsData*
		constexpr std::uintptr_t physics_states = 0x20;            // Array<CharacterStateData*>, count at -4
		constexpr std::uintptr_t climbing_state_vtable = 0x141db1528; // ClimbingStateData
		constexpr std::uintptr_t state_poses = 0x10;               // Array<CharacterStatePoseInfo*>
		constexpr std::uintptr_t pose_velocity = 0x14;
		constexpr std::uintptr_t ladder_component_vtable = 0x141d3dae0; // ClientLadderComponent
		constexpr std::uintptr_t component_block = 0x08;           // -> the owner's component block (entity + 0x1b0), [0] = entity
		constexpr std::uintptr_t static_model_vtable = 0x141d358f0; // ClientStaticModelEntity
		constexpr std::uintptr_t entity_transform = 0x280;         // LinearTransform: right, up, forward (out), origin
		constexpr int climbing = 3;
		// While the hands climb, the climbing state may go this fast (the game's own is 2 m/s): a strong pull is followed.
		constexpr float hand_speed = 4.0f;
		// The arm rig's points (palms, the view's origin) older than this count as untracked. Longer than the weapon code's 250:
		// at the headless ~4 fps a frame takes 270 ms, and a hold must not flicker with it.
		constexpr ULONGLONG points_max_age_ms = 600;
		constexpr ULONGLONG scan_interval_ms = 60000;
		constexpr std::size_t max_components = 512;

		template <typename T>
		bool read(std::uintptr_t address, T& value)
		{
			SIZE_T done = 0;
			return address && ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<LPCVOID>(address), &value, sizeof(value), &done) && done == sizeof(value);
		}

		template <typename T>
		bool write(std::uintptr_t address, const T& value)
		{
			SIZE_T done = 0;
			return address && WriteProcessMemory(GetCurrentProcess(), reinterpret_cast<LPVOID>(address), &value, sizeof(value), &done) && done == sizeof(value);
		}

		v3 cross(const v3& a, const v3& b) { return { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x }; }
		float dot(const v3& a, const v3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }

		// The level's ladders: from the scan (their entities' transforms) and from the climbing state (its length).
		struct known_ladder
		{
			std::uintptr_t entity{};
			ladder::frame frame;
		};
		std::mutex ladders_lock;
		std::vector<known_ladder> known;
		std::atomic<bool> scanning{};
		std::atomic<ULONGLONG> last_scan_at{};
		std::atomic<std::uint64_t> scans{};
		std::atomic<ULONGLONG> scan_ms{};
		std::atomic<std::uintptr_t> scanned_soldier{};

		std::size_t scan_region(const std::uintptr_t* begin, std::size_t words, std::uintptr_t* out, std::size_t room)
		{
			std::size_t found = 0;
			__try
			{
				for (std::size_t i = 0; i < words && found < room; ++i)
				{
					if (begin[i] == ladder_component_vtable)
					{
						out[found++] = reinterpret_cast<std::uintptr_t>(begin + i);
					}
				}
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
			}
			return found;
		}

		// A ladder entity's frame from its transform; false unless it is a static model standing upright.
		bool ladder_of_entity(std::uintptr_t entity, ladder::frame& f)
		{
			std::uintptr_t vtable{};
			float m[16]{};
			if (!read(entity, vtable) || vtable != static_model_vtable || !read(entity + entity_transform, m))
			{
				return false;
			}
			f.right = { m[0], m[1], m[2] };
			f.up = { m[4], m[5], m[6] };
			f.out = { m[8], m[9], m[10] };
			f.foot = { m[12], m[13], m[14] };
			const auto unit = [](const v3& v) { return std::fabs(dot(v, v) - 1.0f) < 0.02f; };
			return unit(f.right) && unit(f.up) && unit(f.out) && f.up.y > 0.98f && std::isfinite(f.foot.x) && std::isfinite(f.foot.y) && std::isfinite(f.foot.z);
		}

		// Background: every ClientLadderComponent in committed private read-write memory, and its entity.
		void scan_ladders()
		{
			const ULONGLONG started = GetTickCount64();
			std::vector<std::uintptr_t> components(max_components);
			std::size_t count = 0;
			MEMORY_BASIC_INFORMATION region{};
			for (std::uintptr_t at = 0x10000; count < max_components && VirtualQuery(reinterpret_cast<LPCVOID>(at), &region, sizeof(region)) == sizeof(region);
				at = reinterpret_cast<std::uintptr_t>(region.BaseAddress) + region.RegionSize)
			{
				if (region.State == MEM_COMMIT && region.Type == MEM_PRIVATE && region.Protect == PAGE_READWRITE)
				{
					count += scan_region(static_cast<const std::uintptr_t*>(region.BaseAddress), region.RegionSize / sizeof(std::uintptr_t),
						components.data() + count, max_components - count);
				}
			}
			std::vector<known_ladder> found;
			for (std::size_t i = 0; i < count; ++i)
			{
				std::uintptr_t block{}, entity{};
				known_ladder l;
				if (read(components[i] + component_block, block) && read(block, entity) && ladder_of_entity(entity, l.frame))
				{
					l.entity = entity;
					found.push_back(l);
				}
			}
			const std::size_t ladders = found.size();
			{
				const std::lock_guard guard(ladders_lock);
				// Keep the lengths the climbing state told us.
				for (known_ladder& l : found)
				{
					for (const known_ladder& old : known)
					{
						if (old.entity == l.entity)
						{
							l.frame.length = old.frame.length;
						}
					}
				}
				known = std::move(found);
			}
			scan_ms = GetTickCount64() - started;
			++scans;
			log::write("ladders: %zu in the level (%zu components, scan %llu ms)", ladders, count, static_cast<unsigned long long>(scan_ms.load()));
			scanning = false;
		}

		void request_scan(bool now_please)
		{
			const ULONGLONG now = GetTickCount64();
			bool idle = false;
			if ((!now_please && now - last_scan_at.load() < scan_interval_ms) || !scanning.compare_exchange_strong(idle, true))
			{
				return;
			}
			last_scan_at = now;
			std::thread([] {
				SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
				scan_ladders();
			}).detach();
		}

		// The climbing state's speed (CharacterStatePoseInfo velocity of each pose), found once per physics asset.
		struct speed_slots
		{
			std::uintptr_t physics{};
			std::vector<std::uintptr_t> velocities;
			float original{ 2.0f };
			bool raised{};
		} speed;

		void find_speed(std::uintptr_t soldier)
		{
			std::uintptr_t data{}, physics{}, states{};
			if (!read(soldier + soldier_data, data) || !read(data + data_physics, physics) || !physics || physics == speed.physics)
			{
				return;
			}
			speed = {};
			speed.physics = physics;
			std::uint32_t count{};
			if (!read(physics + physics_states, states) || !states || !read(states - 4, count) || count > 32)
			{
				return;
			}
			for (std::uint32_t i = 0; i < count; ++i)
			{
				std::uintptr_t state{}, vtable{}, poses{};
				std::uint32_t pose_count{};
				if (read(states + 8 * i, state) && read(state, vtable) && vtable == climbing_state_vtable && read(state + state_poses, poses) && poses
					&& read(poses - 4, pose_count) && pose_count <= 4)
				{
					for (std::uint32_t p = 0; p < pose_count; ++p)
					{
						std::uintptr_t pose{};
						float v{};
						if (read(poses + 8 * p, pose) && read(pose + pose_velocity, v) && v > 0.0f && v < 20.0f)
						{
							speed.velocities.push_back(pose + pose_velocity);
							speed.original = v;
						}
					}
				}
			}
			log::write("ladders: climbing speed %.2f m/s in %zu pose(s)", speed.original, speed.velocities.size());
		}

		void raise_speed(bool raise)
		{
			if (raise == speed.raised)
			{
				return;
			}
			const float v = raise ? hand_speed : speed.original;
			for (std::uintptr_t slot : speed.velocities)
			{
				write(slot, v);
			}
			speed.raised = raise;
		}

		ladder::climber climb;
		ladder::climb_out last_out;
		ladder::climb_in last_in;
		int last_state = -1;
		ladder::frame current;
		bool have_current{};
		float last_body_y{};
		std::uint64_t grabs{}, mounts{}, frames_driven{};
		ULONGLONG last_tick{};

		v3 rotate(const float (&q)[4], const v3& v)
		{
			// q * v * conj(q)
			const v3 u{ q[0], q[1], q[2] };
			const float s = q[3];
			const v3 t = cross(u, v);
			const v3 t2{ 2.0f * t.x, 2.0f * t.y, 2.0f * t.z };
			const v3 c = cross(u, t2);
			return { v.x + s * t2.x + c.x, v.y + s * t2.y + c.y, v.z + s * t2.z + c.z };
		}

		void release_all()
		{
			const float zero[3]{};
			for (int h = 0; h < 2; ++h)
			{
				body::set_rung_hold(h, false, zero, zero);
				claims[h] = false;
			}
			stick_mode = 0;
			aim_on = false;
			raise_speed(false);
		}
#endif
	}

	void set_ladder_hands(bool on)
	{
		enabled = on;
	}

	bool ladder_aim_world(float (&direction)[3])
	{
		if (!enabled.load(std::memory_order_relaxed) || !aim_on.load(std::memory_order_relaxed))
		{
			return false;
		}
		direction[0] = aim_x.load(std::memory_order_relaxed);
		direction[1] = 0.0f;
		direction[2] = aim_z.load(std::memory_order_relaxed);
		return true;
	}

	bool ladder_claims_hand(int hand)
	{
		return hand >= 0 && hand < 2 && claims[hand].load(std::memory_order_relaxed);
	}

	input::gamepad_state filter_ladder_pad(const input::gamepad_state& pad)
	{
		const int mode = stick_mode.load(std::memory_order_relaxed);
		if (mode == 0)
		{
			return pad;
		}
		input::gamepad_state out = pad;
		const auto axis = [](float v) { return static_cast<std::int16_t>(std::lround(std::clamp(v, -1.0f, 1.0f) * 32767.0f)); };
		out.left_x = mode == 2 ? axis(stick_x.load(std::memory_order_relaxed)) : 0;
		out.left_y = axis(stick_y.load(std::memory_order_relaxed));
		out.connected = true;
		return out;
	}

	void update_ladders()
	{
#if defined(_M_X64)
		const ULONGLONG now = GetTickCount64();
		const float dt = last_tick ? std::min(0.1f, static_cast<float>(now - last_tick) / 1000.0f) : 0.0f;
		last_tick = now;
		const std::uintptr_t soldier = game::local_soldier();
		if (!enabled.load(std::memory_order_relaxed) || !soldier)
		{
			climb.reset();
			release_all(); // the climbing speed back to the game's first, while its data is still there
			if (!soldier)
			{
				speed = {}; // a new level or a respawn: find the state again
			}
			last_state = -1;
			return;
		}
		if (scanned_soldier.exchange(soldier) != soldier)
		{
			request_scan(true); // a new soldier: a new level or checkpoint
		}
		else
		{
			request_scan(false);
		}
		find_speed(soldier);

		std::uintptr_t physics{}, state_object{}, character{};
		std::uint32_t state = 0xffffffffu;
		float position[3]{};
		const bool known_state = read(soldier + soldier_physics_entity, physics) && read(physics + physics_state, state_object) && read(state_object + state_type, state);
		const bool placed = read(physics + physics_character, character) && read(character + character_position, position);
		last_state = known_state ? static_cast<int>(state) : -1;

		const body::weapon_points points = body::latest_weapon_points();
		const bool fresh = now - points.at < points_max_age_ms;

		ladder::climb_in in;
		in.dt = dt;
		in.on_ladder = known_state && state == climbing && placed;
		// What the hands move is the view: the tracking space's origin in the world (it rides with the drawn camera).
		// The character's own position read here jumps by a metre while the game places him on the ladder (monitor
		// 2026-11-26, its prediction replay), and the mount lifts the view 0.6 m against 0.2 m of it.
		// Never the character's position as a stand-in: it differs from the view by metres on and around the ladder, and a
		// switch between the two read as a jump (monitor 2026-11-26: 73.19 and 74.82 by turns at 4 fps).
		if (fresh)
		{
			last_body_y = points.world_origin[1];
		}
		in.body_y = last_body_y;
		in.speed = speed.raised ? hand_speed : speed.original;
		if (in.on_ladder)
		{
			float foot[4]{}, out_dir[4]{}, length{};
			if (read(state_object + state_ladder_foot, foot) && read(state_object + state_ladder_out, out_dir) && read(state_object + state_ladder_length, length))
			{
				current.foot = { foot[0], foot[1], foot[2] };
				current.up = { 0.0f, 1.0f, 0.0f };
				current.out = { out_dir[0], out_dir[1], out_dir[2] };
				current.right = cross(current.up, current.out);
				current.length = length;
				have_current = true;
				const std::lock_guard guard(ladders_lock);
				bool listed = false;
				for (known_ladder& l : known)
				{
					const v3 d{ l.frame.foot.x - current.foot.x, l.frame.foot.y - current.foot.y, l.frame.foot.z - current.foot.z };
					if (dot(d, d) < 0.01f)
					{
						l.frame.length = length;
						listed = true;
					}
				}
				if (!listed)
				{
					known.push_back({ 0, current }); // the scan has not seen it yet: climbed onto, so it is one
				}
			}
			in.have_ladder = have_current;
			in.ladder = current;
		}

		const std::uint16_t buttons = input::latest().buttons;
		const bool panel = ui::vr_panel_view().open;
		for (int h = 0; h < 2; ++h)
		{
			ladder::hand_in& hand = in.hands[h];
			hand.tracked = fresh && points.hand_valid[h];
			hand.grip = (buttons & (h == 0 ? XINPUT_GAMEPAD_LEFT_SHOULDER : XINPUT_GAMEPAD_RIGHT_SHOULDER)) != 0;
			hand.palm = { points.palm[h][0], points.palm[h][1], points.palm[h][2] };
			float back[4]{ -points.world_to_local[0], -points.world_to_local[1], -points.world_to_local[2], points.world_to_local[3] };
			const v3 offset = rotate(back, hand.palm);
			hand.palm_world = { points.world_origin[0] + offset.x, points.world_origin[1] + offset.y, points.world_origin[2] + offset.z };
		}
		// A hand with something else in it takes no rung: the gun (free hands: let go of it first), a grenade, a
		// magazine, the panel.
		in.hands[0].busy = panel || grenade_in_hand() || reload_owns_off_hand() || gun_in_off_hand();
		in.hands[1].busy = panel || grenade_in_hand() || gun_hand_holds_gun() || reload_claims_free_hand();

		if (!in.on_ladder)
		{
			// On the ground: the nearest known ladder within reach of a palm (a reach walks onto it).
			const std::lock_guard guard(ladders_lock);
			float best = 2.5f * 2.5f;
			for (const known_ladder& l : known)
			{
				for (int h = 0; h < 2; ++h)
				{
					if (!in.hands[h].tracked)
					{
						continue;
					}
					const v3 local = ladder::to_local(l.frame, in.hands[h].palm_world);
					const float side = std::max(0.0f, std::fabs(local.x) - 0.3f);
					const float d2 = side * side + local.z * local.z;
					if (local.y > -0.5f && d2 < best)
					{
						best = d2;
						in.have_ladder = true;
						in.ladder = l.frame;
					}
				}
			}
		}

		const ladder::climb_out out = climb.update(in);
		for (int h = 0; h < 2; ++h)
		{
			const float at[3]{ out.rung[h].x, out.rung[h].y, out.rung[h].z };
			const float across[3]{ in.ladder.right.x, in.ladder.right.y, in.ladder.right.z };
			body::set_rung_hold(h, out.holding[h], at, across);
			claims[h] = out.holding[h];
			if (out.events[h] == ladder::event::grabbed)
			{
				++grabs;
				input::haptic(h, input::pulses::grab);
			}
		}
		if (out.mount)
		{
			// Straight ahead: the game takes the soldier onto a ladder only while he faces it (LadderAcceptAngle, 33
			// degrees), and a hand reaching for a rung has him facing it. (The soldier's yaw copies at +0x34C/+0x4D8 read
			// 0 off the ladder on the monitor, 2026-11-26, so no turned push is built from them.)
			stick_x = 0.0f;
			stick_y = 0.8f;
			if (stick_mode.exchange(2) != 2)
			{
				++mounts;
			}
			raise_speed(false);
		}
		else if (out.drive)
		{
			raise_speed(true);
			stick_y = out.stick_y;
			stick_mode = 1;
			++frames_driven;
		}
		else
		{
			stick_mode = 0;
			raise_speed(false);
		}
		last_out = out;
		{
			const v3 into{ -in.ladder.out.x, 0.0f, -in.ladder.out.z };
			const float l = std::sqrt(dot(into, into));
			if ((in.on_ladder || out.mount) && in.have_ladder && l > 0.5f)
			{
				aim_x = into.x / l;
				aim_z = into.z / l;
				aim_on = true;
			}
			else
			{
				aim_on = false;
			}
		}
		last_in = in;
#endif
	}

	std::string ladder_command(const std::string& argument)
	{
#if defined(_M_X64)
		if (argument == "scan")
		{
			request_scan(true);
			return "scanning for the level's ladders (pipe `ladder` again in a few seconds)";
		}
		char text[1024];
		std::snprintf(text, sizeof(text),
			"ladder_hands=%d state=%d body_y=%.3f speed=%.2f raised=%d slots=%zu\n"
			"on=%d foot=(%.3f,%.3f,%.3f) out=(%.2f,%.2f,%.2f) length=%.3f\n"
			"hold left=%d (%.3f,%.3f,%.3f) right=%d (%.3f,%.3f,%.3f) drive=%d stick=%.3f target_y=%.3f mount=%d\n"
			"grabs=%llu mounts=%llu frames_driven=%llu",
			enabled.load() ? 1 : 0, last_state, last_body_y, speed.raised ? hand_speed : speed.original, speed.raised ? 1 : 0, speed.velocities.size(),
			last_state == climbing ? 1 : 0, current.foot.x, current.foot.y, current.foot.z, current.out.x, current.out.y, current.out.z, current.length,
			last_out.holding[0] ? 1 : 0, last_out.rung[0].x, last_out.rung[0].y, last_out.rung[0].z, last_out.holding[1] ? 1 : 0, last_out.rung[1].x,
			last_out.rung[1].y, last_out.rung[1].z, last_out.drive ? 1 : 0, last_out.drive ? last_out.stick_y : 0.0f, last_out.target_y, last_out.mount ? 1 : 0,
			static_cast<unsigned long long>(grabs), static_cast<unsigned long long>(mounts), static_cast<unsigned long long>(frames_driven));
		std::string report = text;
		for (int h = 0; h < 2; ++h)
		{
			const ladder::hand_in& hand = last_in.hands[h];
			std::snprintf(text, sizeof(text), "\n%s tracked=%d grip=%d busy=%d palm_world=(%.3f,%.3f,%.3f) palm=(%.3f,%.3f,%.3f)", h == 0 ? "left " : "right", hand.tracked ? 1 : 0,
				hand.grip ? 1 : 0, hand.busy ? 1 : 0, hand.palm_world.x, hand.palm_world.y, hand.palm_world.z, hand.palm.x, hand.palm.y, hand.palm.z);
			report += text;
		}
		const std::lock_guard guard(ladders_lock);
		std::snprintf(text, sizeof(text), "\nknown=%zu scans=%llu scan_ms=%llu%s", known.size(), static_cast<unsigned long long>(scans.load()),
			static_cast<unsigned long long>(scan_ms.load()), scanning.load() ? " (scanning)" : "");
		report += text;
		for (const known_ladder& l : known)
		{
			std::snprintf(text, sizeof(text), "\n  entity=%p foot=(%.3f,%.3f,%.3f) out=(%.2f,%.2f,%.2f) length=%.3f", reinterpret_cast<void*>(l.entity), l.frame.foot.x,
				l.frame.foot.y, l.frame.foot.z, l.frame.out.x, l.frame.out.y, l.frame.out.z, l.frame.length);
			report += text;
		}
		return report;
#else
		(void)argument;
		return "ladders: BF4 x64 only";
#endif
	}
}
