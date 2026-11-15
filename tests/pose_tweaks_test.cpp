// Checks the pose editor's tweaks (src/body/pose_tweaks): the bfvr_poses.ini format both ways, limits,
// the bend and side added to a joint, the thumb base, mirroring for the right hand, and the easing.
#include <cmath>
#include <cstdio>

#include "body/pose_tweaks.hpp"

using namespace bfvr::body;
using namespace bfvr::body::poses;

namespace
{
	int failures = 0;
	constexpr float degrees = 3.14159265f / 180.0f;

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

	ik::quat axis_angle(ik::vec3 axis, float angle)
	{
		const ik::vec3 a = ik::normalize(axis);
		const float s = std::sin(0.5f * angle);
		return { a.x * s, a.y * s, a.z * s, std::cos(0.5f * angle) };
	}

	float angle_between(ik::quat a, ik::quat b)
	{
		const float d = std::fabs(a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w);
		return 2.0f * std::acos(d > 1.0f ? 1.0f : d);
	}
}

static void test_names()
{
	for (int index = 0; index < grip_count; ++index)
	{
		const grip which = static_cast<grip>(index);
		expect(grip_named(name_of(which)) == which, "names: every grip's name finds it again");
	}
	expect(grip_named("off") == grip::none && grip_named("") == grip::none, "names: unknown names are none");
	expect(name_of(grip::none) == "none", "names: none");
}

static void test_parse_and_format()
{
	table values{};
	const auto warnings = parse("# a comment\n"
								"pinch = 5 -2, 10 4, 0 0, 0 0, -3 0, 12 -6 ; trailing comment\n"
								"\n"
								"cup=1 1,2 2,3 3,4 4,5 5,6 6\r\n",
		values);
	expect(warnings.empty(), "parse: a valid file has no warnings");
	const tweak& pinch = values[static_cast<int>(grip::pinch)];
	expect(pinch.fingers[fingers::thumb].bend == 5.0f && pinch.fingers[fingers::thumb].side == -2.0f, "parse: thumb bend and side");
	expect(pinch.fingers[fingers::index].bend == 10.0f && pinch.fingers[fingers::index].side == 4.0f, "parse: index");
	expect(pinch.fingers[fingers::pinky].bend == -3.0f, "parse: pinky");
	expect(pinch.thumb_across == 12.0f && pinch.thumb_out == -6.0f, "parse: thumb base");
	expect(values[static_cast<int>(grip::cup)].fingers[fingers::ring].side == 4.0f && values[static_cast<int>(grip::cup)].thumb_out == 6.0f,
		"parse: no blanks around '=' and ',' and a CRLF ending");
	expect(values[static_cast<int>(grip::fist)].empty(), "parse: grips not in the file stay as they were");

	table again{};
	expect(parse(format(values), again).empty(), "format: its text parses without warnings");
	bool same = true;
	for (int index = 0; index < grip_count; ++index)
	{
		for (int finger = 0; finger < fingers::count; ++finger)
		{
			same = same && again[index].fingers[finger].bend == values[index].fingers[finger].bend
				&& again[index].fingers[finger].side == values[index].fingers[finger].side;
		}
		same = same && again[index].thumb_across == values[index].thumb_across && again[index].thumb_out == values[index].thumb_out;
	}
	expect(same, "format: parse(format(x)) gives x back");
}

static void test_bad_lines()
{
	table values{};
	values[static_cast<int>(grip::hook)].fingers[fingers::index].bend = 7.0f;
	const auto warnings = parse("hook = 1 2, 3 4\n"                         // too few pairs
								"pinch = 1 2, 3 4, 5 6, 7 8, 9 10, 11 12, 13 14\n" // too many
								"grab = 0 0, 0 0, 0 0, 0 0, 0 0, 0 0\n"        // unknown grip
								"fist 0 0\n"                                  // no '='
								"cup = 1 x, 0 0, 0 0, 0 0, 0 0, 0 0\n"        // not a number
								"reach = 500 -500, 0 0, 0 0, 0 0, 0 0, 500 -500\n", // out of range: clamped, used
		values);
	expect(warnings.size() == 5, "bad lines: one warning per unusable line");
	expect(values[static_cast<int>(grip::hook)].fingers[fingers::index].bend == 7.0f, "bad lines: a rejected line leaves the grip as it was");
	expect(values[static_cast<int>(grip::pinch)].empty() && values[static_cast<int>(grip::cup)].empty(), "bad lines: nothing half applied");
	const tweak& reach = values[static_cast<int>(grip::reach)];
	expect(reach.fingers[fingers::thumb].bend == bend_limit && reach.fingers[fingers::thumb].side == -side_limit, "limits: bend and side clamped");
	expect(reach.thumb_across == thumb_base_limit && reach.thumb_out == -thumb_base_limit, "limits: thumb base clamped");
	tweak broken;
	broken.fingers[fingers::middle].bend = NAN;
	expect(clamped(broken).fingers[fingers::middle].bend == 0.0f, "limits: a broken value becomes 0");
}

static void test_joint()
{
	// An animated joint with some spread (about y) and a 40 degree bend (about z).
	const ik::quat animated = axis_angle({ 0, 1, 0 }, 8 * degrees) * axis_angle({ 0, 0, 1 }, 40 * degrees);
	tweak none;
	expect(angle_between(tweaked(animated, fingers::index, 1, none, false), animated) < 1e-4f, "joint: no tweak, unchanged");

	tweak bend;
	bend.fingers[fingers::index].bend = 15.0f;
	const ik::quat bent = tweaked(animated, fingers::index, 1, bend, false);
	expect(near(fingers::bend_of(bent), 55 * degrees), "joint: the bend is added (40 + 15)");
	expect(angle_between(fingers::with_bend(bent, 40 * degrees), animated) < 1e-3f, "joint: the spread is kept");
	expect(angle_between(tweaked(animated, fingers::middle, 1, bend, false), animated) < 1e-4f, "joint: other fingers untouched");
	expect(near(fingers::bend_of(tweaked(animated, fingers::index, 1, bend, true)), 55 * degrees), "joint: bend means the same on the right hand");

	tweak thumb;
	thumb.fingers[fingers::thumb].bend = 20.0f;
	expect(near(fingers::bend_of(tweaked(animated, fingers::thumb, 1, thumb, false)), 60 * degrees), "joint: the thumb bends at Thumb3");
	expect(angle_between(tweaked(animated, fingers::thumb, 2, thumb, false), animated) < 1e-4f, "joint: not at Thumb4 (the tip end)");

	tweak side;
	side.fingers[fingers::ring].side = 10.0f;
	expect(angle_between(tweaked(animated, fingers::ring, 1, side, false), animated) < 1e-4f, "joint: side only at the first joint");
	const ik::quat left = tweaked(animated, fingers::ring, 0, side, false);
	const ik::quat right = tweaked(animated, fingers::ring, 0, side, true);
	expect(near(fingers::bend_of(left), 40 * degrees), "joint: side keeps the bend");
	expect(near(angle_between(left, animated), 10 * degrees, 0.01f), "joint: side turns by its degrees");
	// The side turn goes about the joint's own y before its bend: undoing the bend shows it.
	const ik::quat left_unbent = fingers::with_bend(left, 0.0f);
	const ik::quat right_unbent = fingers::with_bend(right, 0.0f);
	const ik::quat animated_unbent = fingers::with_bend(animated, 0.0f);
	expect(angle_between(left_unbent, animated_unbent * axis_angle({ 0, 1, 0 }, 10 * degrees)) < 1e-3f, "joint: side about y, left hand +");
	expect(angle_between(right_unbent, animated_unbent * axis_angle({ 0, 1, 0 }, -10 * degrees)) < 1e-3f, "joint: right hand mirrored (-)");
	expect(angle_between(tweaked(animated, fingers::count, 0, side, false), animated) < 1e-4f, "joint: out of range finger untouched");
}

static void test_thumb_base()
{
	const ik::quat animated = axis_angle({ 1, 0.3f, -0.2f }, 70 * degrees);
	tweak value;
	expect(angle_between(tweaked_thumb_base(animated, value, false), animated) < 1e-4f, "thumb base: no tweak, unchanged");
	value.thumb_across = 20.0f;
	expect(angle_between(tweaked_thumb_base(animated, value, false), animated * axis_angle({ 0, 0, 1 }, 20 * degrees)) < 1e-3f, "thumb base: across about its z");
	expect(angle_between(tweaked_thumb_base(animated, value, true), animated * axis_angle({ 0, 0, 1 }, 20 * degrees)) < 1e-3f, "thumb base: across the same on the right");
	value = {};
	value.thumb_out = 15.0f;
	expect(angle_between(tweaked_thumb_base(animated, value, false), animated * axis_angle({ 0, 1, 0 }, 15 * degrees)) < 1e-3f, "thumb base: out about its y");
	expect(angle_between(tweaked_thumb_base(animated, value, true), animated * axis_angle({ 0, 1, 0 }, -15 * degrees)) < 1e-3f, "thumb base: out mirrored on the right");
	// The mirrored rotation of a left-hand tweak is the right hand's tweak (fingers::mirrored_hand convention).
	value.thumb_across = 25.0f;
	const ik::quat left = tweaked_thumb_base(animated, value, false);
	const ik::quat mirrored_animated{ -animated.x, -animated.y, animated.z, animated.w };
	const ik::quat right = tweaked_thumb_base(mirrored_animated, value, true);
	expect(angle_between(ik::quat{ -left.x, -left.y, left.z, left.w }, right) < 1e-3f, "thumb base: the right hand mirrors the left");
}

static void test_ease()
{
	tweak current;
	tweak target;
	target.fingers[fingers::index].bend = 20.0f;
	target.thumb_across = -10.0f;
	ease(current, target, 0.05f, 0.05f);
	expect(current.fingers[fingers::index].bend > 10.0f && current.fingers[fingers::index].bend < 15.0f, "ease: one time constant is ~63 %");
	for (int frame = 0; frame < 60; ++frame)
	{
		ease(current, target, 0.011f);
	}
	expect(current.fingers[fingers::index].bend == 20.0f && current.thumb_across == -10.0f, "ease: settles exactly");
	ease(current, {}, 0.0f, 0.0f);
	expect(current.empty(), "ease: no time constant jumps");
}

static void test_defaults()
{
	// The built-in text must parse cleanly (a typo there would silently drop a grip).
	table parsed{};
	const auto warnings = parse(builtin_text(), parsed);
	for (const std::string& warning : warnings)
	{
		std::printf("builtin: %s\n", warning.c_str());
	}
	expect(warnings.empty(), "defaults: the built-in text parses without warnings");
}

int main()
{
	test_names();
	test_parse_and_format();
	test_bad_lines();
	test_joint();
	test_thumb_base();
	test_ease();
	test_defaults();
	if (failures == 0)
	{
		std::printf("pose_tweaks: all tests passed\n");
	}
	return failures == 0 ? 0 : 1;
}
