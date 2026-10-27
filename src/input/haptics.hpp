#pragma once

#include <string>

// Controller haptics (#32): the game's pad rumble (XInputSetState: low-frequency motor = explosions,
// damage, vehicles; high-frequency motor = firing) and short pulses from the mod's own gestures,
// mixed into one vibration per controller each frame. Hand index 1 is the gun hand, 0 the off hand,
// in either handedness (input/handedness). The mixer is engine-free (tests/haptics_test.cpp); the
// global functions are what the hooks and the theater use.
namespace bfvr::input
{
	struct haptic_pulse
	{
		float amplitude{ 0.5f }; // 0..1 before haptics_strength
		float seconds{ 0.03f };
	};

	// Gesture pulses, named so the strengths live in one place.
	namespace pulses
	{
		constexpr haptic_pulse tick{ 0.25f, 0.02f };  // a hand enters a zone (holster, handguard)
		constexpr haptic_pulse grab{ 0.5f, 0.04f };   // grabbed something (magazine, grenade, handguard)
		constexpr haptic_pulse click{ 0.35f, 0.025f }; // menu click, accessory toggle
		constexpr haptic_pulse seat{ 0.8f, 0.06f };   // magazine seated, bolt racked, weapon drawn
		constexpr haptic_pulse strike{ 1.0f, 0.08f }; // melee, throw
		constexpr haptic_pulse recoil{ 0.9f, 0.035f }; // a shot (full auto chains them)
	}

	struct haptics_config
	{
		bool enabled{ true };
		float strength{ 0.8f };
		bool game{ true };     // the game's rumble
		bool gestures{ true }; // the mod's pulses
	};

	struct haptic_output
	{
		float amplitude[2]{}; // per hand (0 = off hand, 1 = gun hand), after strength; 0 = still
	};

	class haptics_mixer
	{
	public:
		void configure(const haptics_config& config) { config_ = config; }
		const haptics_config& config() const { return config_; }
		// The game's motors, 0..1; they run until the game sets them again.
		void game_rumble(float low, float high) { low_ = low; high_ = high; }
		// The off hand holds the gun: it feels the firing too.
		void set_two_handed(bool held) { two_handed_ = held; }
		void pulse(int hand, const haptic_pulse& pulse, bool from_game = false);
		// A shot (counts as the game's rumble): the gun hand, and the off hand too while it holds the gun.
		void recoil(const haptic_pulse& pulse);
		// Advances the pulses by seconds and returns this frame's vibration.
		haptic_output update(float seconds);

	private:
		static constexpr int max_pulses = 8;
		struct running
		{
			int hand{};
			float amplitude{};
			float left{}; // seconds
			bool game{};  // follows haptics_game, not haptics_gestures
		};
		haptics_config config_;
		float low_{};
		float high_{};
		bool two_handed_{};
		running pulses_[max_pulses]{};
		int count_{};
	};

	// Global mixer (thread safe).
	void configure_haptics(const haptics_config& config);
	void set_game_rumble(float low, float high);
	void set_two_handed_haptics(bool held);
	void haptic(int hand, const haptic_pulse& pulse);
	void recoil(const haptic_pulse& pulse = pulses::recoil);
	haptic_output haptics_frame(float seconds);
	// Pipe "haptics": settings, the game's motors, the last output, counts. "haptics <hand 0|1>
	// <amplitude> <ms>" (research) queues a pulse.
	std::string haptics_pipe_command(const std::string& argument);
}
