// Checks the haptics mixer (src/input/haptics): the game's motors per hand (firing on the gun hand,
// and the off hand only while holding the gun two-handed; explosions on both), gesture pulses and
// their duration, strength and the toggles, and a full pulse list.
#include <cmath>
#include <cstdio>

#include "input/haptics.hpp"

using namespace bfvr::input;

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

	bool near(float a, float b)
	{
		return std::fabs(a - b) < 1e-4f;
	}

	haptics_mixer full_strength()
	{
		haptics_mixer mixer;
		mixer.configure({ true, 1.0f, true, true });
		return mixer;
	}

	void test_game_rumble()
	{
		haptics_mixer mixer = full_strength();
		expect(mixer.update(frame).amplitude[1] == 0.0f, "still without rumble");
		mixer.game_rumble(0.0f, 0.6f); // firing
		haptic_output out = mixer.update(frame);
		expect(near(out.amplitude[1], 0.6f) && out.amplitude[0] == 0.0f, "firing: gun hand only");
		mixer.set_two_handed(true);
		out = mixer.update(frame);
		expect(near(out.amplitude[0], 0.6f), "firing two-handed: the off hand too");
		mixer.set_two_handed(false);
		mixer.game_rumble(0.9f, 0.0f); // explosion
		out = mixer.update(frame);
		expect(near(out.amplitude[0], 0.9f) && near(out.amplitude[1], 0.9f), "explosion: both hands");
		expect(near(mixer.update(1.0f).amplitude[1], 0.9f), "the motors run until the game changes them");
		mixer.game_rumble(0.0f, 0.0f);
		expect(mixer.update(frame).amplitude[0] == 0.0f, "stopped");
	}

	void test_pulses()
	{
		haptics_mixer mixer = full_strength();
		mixer.pulse(0, { 0.5f, 0.05f });
		haptic_output out = mixer.update(frame);
		expect(near(out.amplitude[0], 0.5f) && out.amplitude[1] == 0.0f, "a pulse on the off hand");
		int frames = 1;
		while (mixer.update(frame).amplitude[0] > 0.0f && frames < 100)
		{
			++frames;
		}
		expect(frames == 5, "a 50 ms pulse lasts 5 frames at 90 Hz");
		mixer.pulse(1, { 0.3f, 0.001f });
		expect(near(mixer.update(frame).amplitude[1], 0.3f), "a pulse shorter than a frame still plays once");
		expect(mixer.update(frame).amplitude[1] == 0.0f, "and only once");
		mixer.pulse(1, { 0.3f, 0.1f });
		mixer.pulse(1, { 0.9f, 0.02f });
		expect(near(mixer.update(frame).amplitude[1], 0.9f), "overlapping pulses: the stronger wins");
		mixer.update(frame);
		expect(near(mixer.update(frame).amplitude[1], 0.3f), "then the longer one remains");
		mixer.pulse(2, { 1.0f, 1.0f });
		mixer.pulse(-1, { 1.0f, 1.0f });
		mixer.pulse(0, { 0.0f, 1.0f });
		expect(mixer.update(frame).amplitude[0] == 0.0f, "bad hands and silent pulses are ignored");
		mixer.pulse(0, { 3.0f, 0.05f });
		expect(near(mixer.update(frame).amplitude[0], 1.0f), "amplitude clamped to 1");
	}

	void test_recoil()
	{
		haptics_mixer mixer = full_strength();
		mixer.recoil(pulses::recoil);
		haptic_output out = mixer.update(frame);
		expect(near(out.amplitude[1], pulses::recoil.amplitude) && out.amplitude[0] == 0.0f, "a shot one-handed: gun hand");
		mixer.update(1.0f);
		mixer.set_two_handed(true);
		mixer.recoil(pulses::recoil);
		out = mixer.update(frame);
		expect(near(out.amplitude[0], pulses::recoil.amplitude) && near(out.amplitude[1], pulses::recoil.amplitude), "a shot two-handed: both hands");
		mixer.update(1.0f);
		mixer.configure({ true, 1.0f, true, false });
		mixer.recoil(pulses::recoil);
		expect(near(mixer.update(frame).amplitude[1], pulses::recoil.amplitude), "recoil is the game's: stays with gestures off");
		mixer.update(1.0f);
		mixer.configure({ true, 1.0f, false, true });
		mixer.recoil(pulses::recoil);
		expect(mixer.update(frame).amplitude[1] == 0.0f, "recoil goes with haptics_game off");
	}

	void test_full_list()
	{
		haptics_mixer mixer = full_strength();
		for (int i = 0; i < 8; ++i)
		{
			mixer.pulse(1, { 0.1f, 1.0f });
		}
		mixer.pulse(1, { 0.7f, 1.0f });
		expect(near(mixer.update(frame).amplitude[1], 0.7f), "a full list makes room for a strong pulse");
	}

	void test_settings()
	{
		haptics_mixer mixer;
		mixer.configure({ true, 0.5f, true, true });
		mixer.game_rumble(0.0f, 1.0f);
		expect(near(mixer.update(frame).amplitude[1], 0.5f), "strength scales");
		mixer.configure({ true, 0.5f, false, true });
		expect(mixer.update(frame).amplitude[1] == 0.0f, "game rumble off");
		mixer.pulse(1, { 1.0f, 0.1f });
		expect(near(mixer.update(frame).amplitude[1], 0.5f), "gestures still on");
		mixer.configure({ true, 0.5f, false, false });
		expect(mixer.update(frame).amplitude[1] == 0.0f, "gestures off");
		mixer.configure({ false, 1.0f, true, true });
		expect(mixer.update(frame).amplitude[1] == 0.0f, "haptics off: still");
	}

	void test_global()
	{
		configure_haptics({ true, 1.0f, true, true });
		haptic(1, pulses::seat);
		expect(near(haptics_frame(frame).amplitude[1], pulses::seat.amplitude), "global pulse");
		expect(haptics_pipe_command("").find("pulses=1") != std::string::npos, "report counts the pulse");
		expect(haptics_pipe_command("2 1 10").rfind("error:", 0) == 0, "pipe rejects a bad hand");
	}
}

int main()
{
	test_game_rumble();
	test_pulses();
	test_recoil();
	test_full_list();
	test_settings();
	test_global();
	if (failures)
	{
		std::printf("%d failure(s)\n", failures);
		return 1;
	}
	std::printf("haptics: all checks passed\n");
	return 0;
}
