// Checks the settings registry (src/common/settings): definitions, ini load with clamping and
// warnings, pipe get/set/step/reset text, the revision counter and writing back into ini text.
#include <cstdio>
#include <map>
#include <string>

#include "common/settings.hpp"

using namespace bfvr::settings;

namespace
{
	int failures = 0;

	void expect(bool condition, const char* what)
	{
		if (!condition)
		{
			std::printf("FAIL: %s\n", what);
			++failures;
		}
	}

	bool starts_with(const std::string& text, const char* prefix)
	{
		return text.rfind(prefix, 0) == 0;
	}

	void sample(registry& settings)
	{
		settings.add({ "gun_scale", kind::real, 0.25f, 2.0f, 0.05f, "1.0", {}, "apparent gun size" });
		settings.add({ "hud_layer", kind::boolean, 0, 0, 0, "1", {}, "HUD on its own layer" });
		settings.add({ "stereo_render", kind::choice, 0, 0, 0, "aer", "aer|pair", "eye rendering" });
		settings.add({ "snap_angle", kind::integer, 10, 90, 5, "30", {}, "degrees per snap" });
		settings.add({ "arm_ik_wrist", kind::vec3, -0.3f, 0.3f, 0, "0 0 0.06", {}, "wrist from grip" });
	}

	registry::lookup_fn from(const std::map<std::string, std::string>& values)
	{
		return [values](const std::string& name) -> std::optional<std::string>
		{
			const auto found = values.find(name);
			return found != values.end() ? std::optional<std::string>(found->second) : std::nullopt;
		};
	}

	void test_definitions()
	{
		registry settings;
		sample(settings);
		expect(settings.names().size() == 5, "five settings added");
		expect(!settings.add({ "gun_scale", kind::real, 0, 1, 0, "0.5" }), "duplicate name accepted");
		expect(!settings.add({ "Upper", kind::real, 0, 1, 0, "0.5" }), "upper-case name accepted");
		expect(!settings.add({ "bad range", kind::real, 0, 1, 0, "0.5" }), "name with a space accepted");
		expect(!settings.add({ "out", kind::real, 0, 1, 0, "2" }), "default outside the range accepted");
		expect(!settings.add({ "inverted", kind::real, 1, 0, 0, "0.5" }), "min > max accepted");
		expect(!settings.add({ "nochoice", kind::choice, 0, 0, 0, "a", "" }), "choice without options accepted");
		expect(!settings.add({ "wrongchoice", kind::choice, 0, 0, 0, "c", "a|b" }), "choice default not among options accepted");

		expect(settings.real("gun_scale") == 1.0f, "real default");
		expect(settings.boolean("hud_layer"), "bool default");
		expect(settings.text("stereo_render") == "aer", "choice default");
		expect(settings.integer("snap_angle") == 30, "int default");
		const auto wrist = settings.get("arm_ik_wrist");
		expect(wrist && wrist->number[2] == 0.06f && wrist->text == "0 0 0.06", "vec3 default");
		expect(settings.real("hud_layer", -1) == -1, "typed read of another kind returns the fallback");
		expect(settings.real("missing", 7) == 7, "unknown name returns the fallback");
		const auto mode = settings.definition_of("STEREO_RENDER");
		expect(mode && mode->type == kind::choice && mode->choices == "aer|pair", "definition_of");
		expect(!settings.definition_of("missing"), "definition_of unknown");
		expect(!settings.changed("gun_scale"), "nothing changed yet");
		settings.set("gun_scale", "0.5");
		expect(settings.changed("gun_scale") && !settings.changed("hud_layer"), "changed per setting");
	}

	void test_parse()
	{
		const definition flag{ "f", kind::boolean };
		for (const char* yes : { "1", "true", "ON", " yes " })
		{
			expect(parse(flag, yes) && parse(flag, yes)->number[0] == 1, "bool true spelling");
		}
		for (const char* no : { "0", "false", "Off", "no" })
		{
			expect(parse(flag, no) && parse(flag, no)->number[0] == 0, "bool false spelling");
		}
		expect(!parse(flag, "2"), "bool 2 parsed");
		const definition number{ "n", kind::real, 0, 10 };
		expect(parse(number, "+1.5") && parse(number, "+1.5")->text == "1.5", "leading plus");
		expect(!parse(number, "1.5x"), "trailing junk parsed");
		expect(!parse(number, "nan") && !parse(number, "inf"), "non-finite parsed");
		expect(!parse(number, ""), "empty parsed");
		const definition whole{ "i", kind::integer, 0, 10 };
		expect(!parse(whole, "1.5"), "fraction parsed as int");
		const definition axes{ "v", kind::vec3, -1, 1 };
		expect(parse(axes, "0.1, -0.2, 0.3") && parse(axes, "0.1, -0.2, 0.3")->text == "0.1 -0.2 0.3", "vec3 with commas");
		expect(!parse(axes, "1 2"), "vec3 with two components parsed");
		const definition mode{ "m", kind::choice, 0, 0, 0, "aer", "aer|pair" };
		expect(parse(mode, "PAIR") && parse(mode, "PAIR")->text == "pair" && parse(mode, "PAIR")->number[0] == 1, "choice case-insensitive");
	}

	void test_load()
	{
		registry settings;
		sample(settings);
		const auto warnings = settings.load(from({
			{ "gun_scale", "5" },          // clamped to 2
			{ "hud_layer", "maybe" },      // invalid, default kept
			{ "stereo_render", "pair" },
			{ "snap_angle", "45" },
			{ "arm_ik_wrist", "0.01 0.02 0.9" }, // z clamped to 0.3
		}));
		expect(warnings.size() == 3, "three load warnings");
		expect(settings.real("gun_scale") == 2.0f, "load clamps to max");
		expect(settings.boolean("hud_layer"), "invalid text keeps the default");
		expect(settings.text("stereo_render") == "pair", "choice loaded");
		expect(settings.integer("snap_angle") == 45, "int loaded");
		expect(settings.text("arm_ik_wrist") == "0.01 0.02 0.3", "vec3 clamped per component");
		expect(!settings.changed(), "load leaves nothing marked changed");

		// A second load starts from the defaults again.
		settings.load(from({}));
		expect(settings.real("gun_scale") == 1.0f && settings.text("stereo_render") == "aer", "reload without values restores defaults");
	}

	void test_set_and_revision()
	{
		registry settings;
		sample(settings);
		const auto start = settings.revision();
		std::string error;
		expect(settings.set("gun_scale", "0.8", &error), "set in range");
		expect(settings.real("gun_scale") == 0.8f, "set value read back");
		expect(settings.revision() == start + 1, "set bumps the revision");
		expect(settings.set("GUN_SCALE", " 0.8 "), "same value, other case and spaces");
		expect(settings.revision() == start + 1, "setting the same value does not bump the revision");
		expect(!settings.set("gun_scale", "3", &error) && error.find("0.25..2") != std::string::npos, "out of range rejected with the range");
		expect(!settings.set("gun_scale", "big", &error), "text rejected for real");
		expect(!settings.set("nope", "1", &error) && error.find("unknown") != std::string::npos, "unknown name rejected");
		expect(settings.real("gun_scale") == 0.8f, "rejected set leaves the value");
		expect(settings.changed(), "set marks changed");
	}

	void test_step()
	{
		registry settings;
		sample(settings);
		expect(settings.step("gun_scale", 2), "step real");
		expect(settings.text("gun_scale") == "1.1", "real steps by its step size");
		expect(settings.step("gun_scale", 100) && settings.real("gun_scale") == 2.0f, "real step clamps at max");
		expect(settings.step("hud_layer", 1) && !settings.boolean("hud_layer"), "bool toggles");
		expect(settings.step("hud_layer", 1) && settings.boolean("hud_layer"), "bool toggles back");
		expect(settings.step("hud_layer", 2) && settings.boolean("hud_layer"), "two toggles cancel");
		expect(settings.step("stereo_render", 1) && settings.text("stereo_render") == "pair", "choice cycles forward");
		expect(settings.step("stereo_render", 1) && settings.text("stereo_render") == "aer", "choice wraps");
		expect(settings.step("stereo_render", -1) && settings.text("stereo_render") == "pair", "choice cycles back");
		expect(settings.step("snap_angle", -3) && settings.integer("snap_angle") == 15, "int steps");
		expect(settings.step("snap_angle", -10) && settings.integer("snap_angle") == 10, "int step clamps at min");
		std::string error;
		expect(!settings.step("arm_ik_wrist", 1, &error) && error.find("vec3") != std::string::npos, "vec3 step rejected");
		expect(settings.reset("snap_angle") && settings.integer("snap_angle") == 30, "reset restores the default");
	}

	void test_command()
	{
		registry settings;
		sample(settings);
		const std::string list = settings.command("list");
		expect(list.find("gun_scale = 1  [float 0.25..2, default 1]  apparent gun size\n") != std::string::npos, "list line format");
		expect(list.find("stereo_render = aer  [choice aer|pair, default aer]") != std::string::npos, "list shows choices");
		expect(settings.command("get hud_layer").find("hud_layer = 1  [bool 0|1, default 1]") == 0, "get");
		expect(starts_with(settings.command("set gun_scale 0.75"), "ok gun_scale = 0.75"), "set reply");
		expect(settings.command("get gun_scale").find(" *") != std::string::npos, "changed values are starred");
		expect(starts_with(settings.command("set arm_ik_wrist 0.01 0.02 0.03"), "ok arm_ik_wrist = 0.01 0.02 0.03"), "set vec3 takes the rest of the line");
		expect(starts_with(settings.command("step snap_angle -1"), "ok snap_angle = 25"), "step reply");
		expect(starts_with(settings.command("step hud_layer"), "ok hud_layer = 0"), "step defaults to one");
		expect(starts_with(settings.command("step snap_angle x"), "error:"), "bad step count");
		expect(starts_with(settings.command("set gun_scale"), "error: usage"), "set without a value");
		expect(starts_with(settings.command("set gun_scale 9"), "error:"), "set out of range");
		expect(starts_with(settings.command("get nope"), "error: unknown"), "get unknown");
		expect(starts_with(settings.command("frobnicate"), "error: usage"), "unknown verb");
		expect(starts_with(settings.command("reset gun_scale"), "ok gun_scale = 1"), "reset reply");
	}

	void test_merge_ini()
	{
		registry settings;
		sample(settings);
		const std::string ini =
			"# bf-vr settings\r\n"
			"theater = 1\r\n"
			"gun_scale = 1.0   # tune in the headset\r\n"
			"  stereo_render=aer\r\n"
			"# snap_angle = 30\r\n";
		settings.load(from({ { "gun_scale", "1.0" }, { "stereo_render", "aer" } }));
		expect(settings.merge_ini(ini) == ini, "nothing changed: text unchanged");

		settings.set("gun_scale", "0.8");
		settings.set("stereo_render", "pair");
		settings.set("snap_angle", "45");
		const std::string merged = settings.merge_ini(ini);
		const std::string expected =
			"# bf-vr settings\r\n"
			"theater = 1\r\n"
			"gun_scale = 0.8 # tune in the headset\r\n"
			"  stereo_render = pair\r\n"
			"# snap_angle = 30\r\n"
			"snap_angle = 45\r\n";
		expect(merged == expected, "changed keys rewritten in place, comment kept, missing key appended, CRLF kept");
		if (merged != expected)
		{
			std::printf("merged:\n%s\n", merged.c_str());
		}

		// The merged text loads back to the same values (the round trip config::get does).
		registry reloaded;
		sample(reloaded);
		reloaded.load(from({ { "gun_scale", "0.8" }, { "stereo_render", "pair" }, { "snap_angle", "45" } }));
		expect(reloaded.real("gun_scale") == 0.8f && reloaded.text("stereo_render") == "pair" && reloaded.integer("snap_angle") == 45, "round trip");

		expect(settings.merge_ini("") == "gun_scale = 0.8\nstereo_render = pair\nsnap_angle = 45\n", "empty file gets the changed keys");
		expect(settings.merge_ini("theater = 1") == "theater = 1\ngun_scale = 0.8\nstereo_render = pair\nsnap_angle = 45", "no trailing newline kept");
		settings.mark_saved();
		expect(!settings.changed() && settings.merge_ini(ini) == ini, "after mark_saved nothing is written");
	}
}

int main()
{
	test_definitions();
	test_parse();
	test_load();
	test_set_and_revision();
	test_step();
	test_command();
	test_merge_ini();
	if (failures)
	{
		std::printf("%d failure(s)\n", failures);
		return 1;
	}
	std::printf("settings: all checks passed\n");
	return 0;
}
