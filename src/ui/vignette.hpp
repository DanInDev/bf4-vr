#pragma once

#include <cstdint>
#include <vector>

// Comfort vignette (issue #10): darkens the edge of the view while the game moves or turns the player
// artificially (stick turning and aiming, walking, vehicles), not when the head moves. Engine-free:
// strength from the game camera's yaw rate and speed, and the overlay texture. Tested by
// tests/vignette_test.cpp.
namespace bfvr::ui
{
	enum class vignette_mode
	{
		off,
		turning, // rotation only
		moving,  // translation only
		both,
	};

	struct vignette_config
	{
		vignette_mode mode{ vignette_mode::turning };
		float strength{ 0.7f };      // largest strength (0..1)
		float turn_start{ 0.3f };    // rad/s where it starts (slow stick aiming stays clear)
		float turn_full{ 1.5f };     // rad/s for full strength (a snap turn is far above)
		float move_start{ 1.0f };    // m/s
		float move_full{ 5.0f };     // m/s (sprint)
		float attack{ 0.08f };       // seconds to reach a higher target
		float release{ 0.35f };      // seconds to fade out
	};

	class vignette
	{
	public:
		explicit vignette(const vignette_config& config = {}) : config_(config) {}
		void configure(const vignette_config& config) { config_ = config; }
		const vignette_config& config() const { return config_; }
		// yaw_rate rad/s (sign ignored), speed m/s; returns the strength to draw (0..config.strength).
		float update(float yaw_rate, float speed, float seconds);
		float strength() const { return strength_; }
		void reset() { strength_ = 0; }

	private:
		vignette_config config_;
		float strength_{};
	};

	// Square texture, premultiplied black: clear inside a circle that shrinks with strength, opaque
	// towards the edge. Top-down BGRA (0xAARRGGBB with RGB 0).
	void vignette_texture(float strength, int size, std::vector<std::uint32_t>& pixels);
}
