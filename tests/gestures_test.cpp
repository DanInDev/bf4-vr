// Checks the HUD gesture detectors (src/input/gestures) and the laser-pointer hit
// (src/input/pointer) on synthetic poses: a watch glance, an inspected gun, aiming down the sights,
// moving hands and rays against rotated quads. The grip-axis conventions still need the headset.
#include <cmath>
#include <cstdio>

#include "body/arm_ik.hpp"
#include "input/gestures.hpp"
#include "input/pointer.hpp"

using namespace bfvr;
using namespace bfvr::input;
using body::ik::quat;
using body::ik::vec3;
using stereo::pose;

namespace
{
	int failures = 0;
	constexpr float frame = 1.0f / 90.0f;

	void expect(bool condition, const char* what)
	{
		if (!condition)
		{
			std::printf("FAIL: %s\n", what);
			++failures;
		}
	}

	bool near(float a, float b, float tolerance = 1e-3f)
	{
		return std::fabs(a - b) <= tolerance;
	}

	// Orientation whose local x and z axes are the given directions (z is made orthogonal to x).
	quat from_axes(vec3 x, vec3 z)
	{
		x = body::ik::normalize(x);
		z = body::ik::normalize(z - x * body::ik::dot(z, x));
		const vec3 y = body::ik::cross(z, x);
		const float m[3][3] = { { x.x, y.x, z.x }, { x.y, y.y, z.y }, { x.z, y.z, z.z } };
		quat q;
		const float trace = m[0][0] + m[1][1] + m[2][2];
		if (trace > 0)
		{
			const float s = 0.5f / std::sqrt(trace + 1.0f);
			q = { (m[2][1] - m[1][2]) * s, (m[0][2] - m[2][0]) * s, (m[1][0] - m[0][1]) * s, 0.25f / s };
		}
		else if (m[0][0] > m[1][1] && m[0][0] > m[2][2])
		{
			const float s = 2.0f * std::sqrt(1.0f + m[0][0] - m[1][1] - m[2][2]);
			q = { 0.25f * s, (m[0][1] + m[1][0]) / s, (m[0][2] + m[2][0]) / s, (m[2][1] - m[1][2]) / s };
		}
		else if (m[1][1] > m[2][2])
		{
			const float s = 2.0f * std::sqrt(1.0f + m[1][1] - m[0][0] - m[2][2]);
			q = { (m[0][1] + m[1][0]) / s, 0.25f * s, (m[1][2] + m[2][1]) / s, (m[0][2] - m[2][0]) / s };
		}
		else
		{
			const float s = 2.0f * std::sqrt(1.0f + m[2][2] - m[0][0] - m[1][1]);
			q = { (m[0][2] + m[2][0]) / s, (m[1][2] + m[2][1]) / s, 0.25f * s, (m[1][0] - m[0][1]) / s };
		}
		return body::ik::normalize(q);
	}

	pose make_pose(vec3 position, quat rotation)
	{
		pose p;
		p.position[0] = position.x;
		p.position[1] = position.y;
		p.position[2] = position.z;
		p.orientation[0] = rotation.x;
		p.orientation[1] = rotation.y;
		p.orientation[2] = rotation.z;
		p.orientation[3] = rotation.w;
		return p;
	}

	vec3 position_of(const pose& p)
	{
		return { p.position[0], p.position[1], p.position[2] };
	}

	quat rotation_of(const pose& p)
	{
		return { p.orientation[0], p.orientation[1], p.orientation[2], p.orientation[3] };
	}

	// Head at position looking at target, y roughly up.
	pose look_at(vec3 position, vec3 target)
	{
		const vec3 back = body::ik::normalize(position - target);
		const vec3 right = body::ik::normalize(body::ik::cross({ 0, 1, 0 }, back));
		return make_pose(position, from_axes(right, back));
	}

	quat about_y(float radians)
	{
		return { 0, std::sin(radians / 2), 0, std::cos(radians / 2) };
	}

	// Grip whose point at offset lands on target, with the given x and z axes.
	pose grip_with_point(vec3 target, vec3 x_axis, vec3 z_axis, const float (&offset)[3])
	{
		const quat rotation = from_axes(x_axis, z_axis);
		return make_pose(target - body::ik::rotate(rotation, { offset[0], offset[1], offset[2] }), rotation);
	}

	const vec3 eyes{ 0, 1.6f, 0 };

	void test_helpers()
	{
		const pose head = make_pose(eyes, {});
		const float ahead[3]{ 0, 1.6f, -1 };
		expect(near(gestures::gaze_angle(head, ahead), 0), "point straight ahead has gaze angle 0");
		const float side[3]{ 1, 1.6f, 0 };
		expect(near(gestures::gaze_angle(head, side), 1.5707963f), "point to the right is 90 degrees off");

		const pose turned = make_pose({ 1, 2, 3 }, about_y(1.5707963f));
		const float offset[3]{ 0, 0, -1 };
		float out[3];
		gestures::transform_point(turned, offset, out);
		expect(near(out[0], 0) && near(out[1], 2) && near(out[2], 3), "transform_point rotates the offset (-z turned left is -x)");

		float left[3];
		float right[3];
		gestures::palm_normal(make_pose({}, {}), 0, left);
		gestures::palm_normal(make_pose({}, {}), 1, right);
		expect(near(left[0], 1) && near(right[0], -1), "palm normal is +x left, -x right");
	}

	struct glance_scene
	{
		pose head;
		pose watch;      // palm side towards the eyes
		pose back;       // back of the hand towards the eyes
	};

	// The wrist pulled in to about 40 cm below and ahead of the eyes, the head about 50 degrees down.
	glance_scene glance(const gestures::forearm_glance_config& config)
	{
		const vec3 point{ -0.05f, 1.28f, -0.28f };
		const vec3 to_eyes = eyes - point;
		const float sign = config.hand == 0 ? 1.0f : -1.0f; // palm normal is +x left, -x right
		const vec3 elbow = config.hand == 0 ? vec3{ -1, 0, 0 } : vec3{ 1, 0, 0 };
		float offset[3]{ config.hand == 1 ? -config.offset[0] : config.offset[0], config.offset[1], config.offset[2] };
		return { look_at(eyes, point), grip_with_point(point, to_eyes * sign, elbow, offset), grip_with_point(point, to_eyes * -sign, elbow, offset) };
	}

	int frames_until(gestures::forearm_glance& detector, const pose& head, const pose& grip, bool tracked, float limit, bool want)
	{
		for (int i = 1; static_cast<float>(i) * frame <= limit; ++i)
		{
			if (detector.update(head, grip, tracked, frame) == want)
			{
				return i;
			}
		}
		return -1;
	}

	void test_forearm_glance()
	{
		gestures::forearm_glance detector;
		const glance_scene scene = glance(detector.config());
		expect(gestures::forearm_in_view(scene.head, scene.watch, detector.config()), "watch pose is in view");
		expect(!gestures::forearm_in_view(scene.head, scene.back, detector.config()), "back of the hand does not count");

		const int shown = frames_until(detector, scene.head, scene.watch, true, 1.0f, true);
		expect(shown > 0 && near(static_cast<float>(shown) * frame, detector.config().dwell, 2 * frame), "shows after the dwell time");

		// Looking a little away keeps it, looking well away hides it after the delay.
		const pose slightly = make_pose(eyes, body::ik::normalize(about_y(0.4f) * rotation_of(scene.head)));
		expect(frames_until(detector, slightly, scene.watch, true, 1.0f, false) == -1, "stays while within the hide cone");
		const pose away = make_pose(eyes, {});
		const int hidden = frames_until(detector, away, scene.watch, true, 1.0f, false);
		expect(hidden > 0 && near(static_cast<float>(hidden) * frame, detector.config().hide_delay, 2 * frame), "hides after the delay when looking away");

		// A glance shorter than the dwell time does nothing.
		detector.reset();
		expect(frames_until(detector, scene.head, scene.watch, true, 0.1f, true) == -1, "short glance ignored");
		expect(frames_until(detector, away, scene.watch, true, 0.5f, true) == -1, "looking away after a short glance");
		expect(frames_until(detector, scene.head, scene.back, true, 2.0f, true) == -1, "back of the hand never shows");
		expect(frames_until(detector, scene.head, scene.watch, false, 2.0f, true) == -1, "untracked hand never shows");

		// Losing tracking while shown hides it.
		frames_until(detector, scene.head, scene.watch, true, 1.0f, true);
		expect(frames_until(detector, scene.head, scene.watch, false, 1.0f, false) > 0, "tracking loss hides");

		// Too far away (arm stretched out) does not count.
		gestures::forearm_glance_config near_only = detector.config();
		near_only.max_distance = 0.3f;
		expect(!gestures::forearm_in_view(scene.head, scene.watch, near_only), "beyond max_distance");

		// Right hand mirrored.
		gestures::forearm_glance_config right = detector.config();
		right.hand = 1;
		const glance_scene mirrored = glance(right);
		expect(gestures::forearm_in_view(mirrored.head, mirrored.watch, right), "right-hand watch pose");
		expect(!gestures::forearm_in_view(mirrored.head, mirrored.back, right), "right back of the hand");
	}

	// #55: the minimap only on a deliberate watch glance, never while the support arm braces the gun.
	void test_watch_glance_is_deliberate()
	{
		const gestures::forearm_glance_config config;
		gestures::forearm_glance_config before = config; // the thresholds before #55
		before.offset[2] = 0.12f;
		before.facing = 0.5f;
		before.max_distance = 0.8f;
		before.min_head_pitch = 0;

		const pose level = make_pose(eyes, {});
		expect(near(gestures::head_pitch_down(level), 0), "level head has pitch 0");
		const glance_scene scene = glance(config);
		expect(gestures::head_pitch_down(scene.head) > 0.75f, "the glance scene looks about 50 degrees down");

		// Two-handed brace: off hand on the handguard 44 cm ahead and a little low, forearm back to the
		// elbow, inner forearm turned to a level head looking along the gun.
		const vec3 handguard{ -0.03f, 1.47f, -0.42f };
		const vec3 elbow_side{ -0.3f, -0.3f, 1.0f };
		const pose brace_before = grip_with_point(handguard, eyes - handguard, elbow_side, before.offset);
		const pose brace = grip_with_point(handguard, eyes - handguard, elbow_side, config.offset);
		expect(gestures::forearm_in_view(level, brace_before, before), "the old thresholds showed the map on a brace");
		expect(!gestures::forearm_in_view(level, brace, config), "a brace with a level head does not show");
		gestures::forearm_glance detector;
		bool shown = false;
		for (int i = 0; i < 270; ++i)
		{
			shown |= detector.update(level, brace, true, frame);
		}
		expect(!shown, "three seconds of bracing never show the map");

		// The same wrist pose with the head level.
		expect(!gestures::forearm_in_view(level, scene.watch, config), "watch pose with a level head does not show");
		gestures::forearm_glance_config any_pitch = config;
		any_pitch.min_head_pitch = 0;
		expect(gestures::forearm_in_view(level, brace, any_pitch), "pitch 0 turns the pitch check off: the brace counts again");

		// Head down, but the arm stretched out 65 cm.
		const vec3 far_point = eyes + body::ik::normalize(vec3{ -0.1f, -1.0f, -1.0f }) * 0.65f;
		const vec3 far_eyes = eyes - far_point;
		const float offset[3]{ config.offset[0], config.offset[1], config.offset[2] };
		const pose far_wrist = grip_with_point(far_point, far_eyes, { -1, 0, 0 }, offset);
		const pose far_head = look_at(eyes, far_point);
		expect(!gestures::forearm_in_view(far_head, far_wrist, config), "an outstretched arm does not show");
		gestures::forearm_glance_config long_reach = config;
		long_reach.max_distance = 0.8f;
		expect(gestures::forearm_in_view(far_head, far_wrist, long_reach), "the same arm within a longer reach shows");

		// Inner wrist turned 50 degrees away from the eyes: outside 40 degrees, inside the old 60.
		const vec3 to_eyes = body::ik::normalize(eyes - vec3{ -0.05f, 1.28f, -0.28f });
		const vec3 side = body::ik::normalize(body::ik::cross(to_eyes, { -1, 0, 0 }));
		const vec3 turned = to_eyes * std::cos(0.87f) + side * std::sin(0.87f);
		const pose half_turned = grip_with_point({ -0.05f, 1.28f, -0.28f }, turned, { -1, 0, 0 }, offset);
		expect(!gestures::forearm_in_view(scene.head, half_turned, config), "wrist turned 50 degrees away does not show");
		gestures::forearm_glance_config wide = config;
		wide.facing = 0.5f;
		expect(gestures::forearm_in_view(scene.head, half_turned, wide), "the same wrist within 60 degrees shows");

		// Busy off hand (gun, magazine, grenade): never shows; shown and then busy hides after the delay.
		detector.reset();
		shown = false;
		for (int i = 0; i < 90; ++i)
		{
			shown |= detector.update(scene.head, scene.watch, true, frame, true);
		}
		expect(!shown, "a busy off hand never shows the map");
		frames_until(detector, scene.head, scene.watch, true, 1.0f, true);
		int hidden = -1;
		for (int i = 1; i <= 90 && hidden < 0; ++i)
		{
			if (!detector.update(scene.head, scene.watch, true, frame, true))
			{
				hidden = i;
			}
		}
		expect(hidden > 0 && near(static_cast<float>(hidden) * frame, config.hide_delay, 2 * frame), "grabbing the gun hides the map after the delay");
	}

	struct gun_scene
	{
		pose head;
		pose inspect; // side of the gun turned to the eyes
		pose aiming;  // aiming down the sights, head looking along the barrel
		pose aim_head;
	};

	gun_scene gun(const gestures::gun_inspect_config& config)
	{
		const vec3 point{ 0.1f, 1.35f, -0.35f };
		gun_scene scene;
		scene.head = look_at(eyes, point);
		scene.inspect = grip_with_point(point, eyes - point, { 1, 0, 0 }, config.offset);
		scene.aiming = grip_with_point({ 0, 1.58f, -0.4f }, { 1, 0, 0 }, { 0, 0, 1 }, config.offset);
		scene.aim_head = make_pose(eyes, {});
		return scene;
	}

	int frames_until(gestures::gun_inspect& detector, const pose& head, const pose& grip, float limit, bool want)
	{
		for (int i = 1; static_cast<float>(i) * frame <= limit; ++i)
		{
			if (detector.update(head, grip, true, frame) == want)
			{
				return i;
			}
		}
		return -1;
	}

	void test_gun_inspect()
	{
		gestures::gun_inspect detector;
		const gun_scene scene = gun(detector.config());
		expect(gestures::gun_in_inspect_pose(scene.head, scene.inspect, detector.config()), "inspect pose recognised");
		expect(!gestures::gun_in_inspect_pose(scene.aim_head, scene.aiming, detector.config()), "aiming down the sights is not inspecting");

		// Steady inspection shows after about a second (one frame to get a previous pose).
		const int shown = frames_until(detector, scene.head, scene.inspect, 2.0f, true);
		expect(shown > 0 && near(static_cast<float>(shown) * frame, detector.config().hold, 3 * frame), "shows after the hold time");

		// Turning the gun back while still looking at it keeps the counter.
		const pose back_to_aim = grip_with_point({ 0.1f, 1.35f, -0.35f }, { 1, 0, 0 }, { 0, 0, 1 }, detector.config().offset);
		expect(frames_until(detector, scene.head, back_to_aim, 1.0f, false) == -1, "stays while looking at the gun");
		const pose away = make_pose(eyes, about_y(1.2f));
		const int hidden = frames_until(detector, away, back_to_aim, 1.0f, false);
		expect(hidden > 0 && near(static_cast<float>(hidden) * frame, detector.config().hide_delay, 2 * frame), "hides after looking away");

		// Aiming for a long time never shows it.
		detector.reset();
		expect(frames_until(detector, scene.aim_head, scene.aiming, 3.0f, true) == -1, "aiming never shows ammo");

		// Rolling the gun about its barrel in the inspect pose (up to 85 degrees per second) is not steady.
		detector.reset();
		bool shown_while_turning = false;
		for (int i = 0; i < 180; ++i)
		{
			const float angle = 0.25f * std::sin(static_cast<float>(i) * frame * 6.0f);
			const quat wobble = body::ik::normalize(rotation_of(scene.inspect) * quat{ 0, 0, std::sin(angle / 2), std::cos(angle / 2) });
			const pose turning = make_pose(position_of(scene.inspect), wobble);
			shown_while_turning |= detector.update(scene.head, turning, true, frame);
		}
		expect(!shown_while_turning, "turning gun is not steady");

		// Tracking noise (half a millimetre per frame, a tenth of a degree) still counts as steady.
		detector.reset();
		bool shown_with_tremor = false;
		for (int i = 0; i < 135; ++i)
		{
			const float jitter = 0.0005f * static_cast<float>((i % 3) - 1);
			const vec3 position = position_of(scene.inspect) + vec3{ jitter, -jitter, jitter };
			const float angle = 0.002f * static_cast<float>((i % 2) * 2 - 1);
			const quat rotation = body::ik::normalize(rotation_of(scene.inspect) * quat{ std::sin(angle / 2), 0, 0, std::cos(angle / 2) });
			shown_with_tremor |= detector.update(scene.head, make_pose(position, rotation), true, frame);
		}
		expect(shown_with_tremor, "tremor still counts as steady");

		// Moving the gun interrupts the hold: the timer starts again.
		detector.reset();
		frames_until(detector, scene.head, scene.inspect, 0.8f, true);
		const pose moved = make_pose(position_of(scene.inspect) + vec3{ 0, 0.05f, 0 }, rotation_of(scene.inspect));
		detector.update(scene.head, moved, true, frame);
		expect(detector.progress() == 0, "a jump resets the progress");
		expect(frames_until(detector, scene.head, moved, 0.8f, true) == -1, "needs a full second again");

		// Untracked hand loses the previous pose: no speed from a stale sample.
		detector.reset();
		detector.update(scene.head, scene.inspect, false, frame);
		detector.update(scene.head, scene.inspect, true, frame);
		expect(detector.progress() == 0, "first tracked frame after a loss is not steady yet");
	}

	void test_pointer()
	{
		quad_target screen;
		screen.pose.position[2] = -2.5f;
		screen.width = 3.0f;
		screen.height = 1.6875f;

		pointer_hit hit = point_at(make_pose({}, {}), screen);
		expect(hit.hit && near(hit.distance, 2.5f) && near(hit.u, 0.5f) && near(hit.v, 0.5f), "centre hit");
		hit = point_at(make_pose({ 0.75f, 0.4f, 0 }, {}), screen);
		expect(hit.hit && near(hit.u, 0.75f) && near(hit.v, 0.5f - 0.4f / 1.6875f) && near(hit.point[2], -2.5f), "offset hit, v grows downwards");
		expect(!point_at(make_pose({}, about_y(3.14159265f)), screen).hit, "pointing away misses");
		expect(!point_at(make_pose({ 2.0f, 0, 0 }, {}), screen).hit, "outside the quad misses");
		expect(!point_at(make_pose({}, {}), screen, 2.0f).hit, "beyond max distance misses");
		expect(!point_at(make_pose({ 0, 0, -3.0f }, {}), screen).hit, "quad behind the ray origin misses");

		quad_target backwards = screen;
		const quat flip = about_y(3.14159265f);
		backwards.pose.orientation[1] = flip.y;
		backwards.pose.orientation[3] = flip.w;
		expect(!point_at(make_pose({}, {}), backwards).hit, "back face misses");

		// A quad to the left facing +x, ray turned left.
		quad_target side = screen;
		side.pose.position[0] = -2.0f;
		side.pose.position[2] = 0;
		const quat left = about_y(1.5707963f);
		side.pose.orientation[1] = left.y;
		side.pose.orientation[3] = left.w;
		hit = point_at(make_pose({ 0, 0, -0.5f }, left), side);
		expect(hit.hit && near(hit.distance, 2.0f) && near(hit.u, 0.5f + 0.5f / 3.0f) && near(hit.v, 0.5f), "rotated quad, viewer's right is u up");

		quad_target empty;
		expect(!point_at(make_pose({}, {}), empty).hit, "zero-size quad misses");

		// Menus over a level (crates, 2026-11-05): the screen goes where the head looks, level.
		const quat turned = about_y(1.5707963f); // 90 deg left: looks along -x
		const quat down{ std::sin(-0.2f), 0.0f, 0.0f, std::cos(-0.2f) }; // and 23 deg down
		const pose head = make_pose({ 0.3f, 1.6f, -0.2f }, turned * down);
		quad_target ahead = screen;
		ahead.pose = screen_in_front(head, 2.5f);
		expect(near(ahead.pose.position[0], 0.3f - 2.5f) && near(ahead.pose.position[1], 1.6f) && near(ahead.pose.position[2], -0.2f),
			"screen 2.5 m along the head's yaw, at head height (pitch ignored)");
		hit = point_at(make_pose({ 0.3f, 1.6f, -0.2f }, turned), ahead);
		expect(hit.hit && near(hit.u, 0.5f) && near(hit.v, 0.5f) && near(hit.distance, 2.5f), "a ray along the head's yaw hits its centre");
		expect(!point_at(make_pose({ 0.3f, 1.6f, -0.2f }, {}), ahead).hit, "the old fixed direction (-z) misses it");
	}
}

int main()
{
	test_helpers();
	test_forearm_glance();
	test_watch_glance_is_deliberate();
	test_gun_inspect();
	test_pointer();
	if (failures)
	{
		std::printf("%d failure(s)\n", failures);
		return 1;
	}
	std::printf("gestures: all checks passed\n");
	return 0;
}
