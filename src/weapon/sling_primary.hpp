#pragma once

#include <cstdint>
#include <string>

// #58: the primary gun stays on its sling in 3D while another weapon is out. The game draws only the
// selected weapon, so the primary is captured once while it is in hand (stereo/extra_mesh, one render
// with it hidden) and its copy is drawn at the sling meanwhile. This decides when to capture, when to
// show the copy and when a capture is stale (a different primary). Engine-free; tests/sling_primary_test.cpp.
namespace bfvr::weapon
{
	struct sling_primary_frame
	{
		bool enabled{};          // setting sling_show_primary
		bool slot_valid{};
		int slot{ -1 };          // the game's current weapon slot (0 = primary)
		std::string name;        // the current weapon's name
		float seconds{};         // since the last update
		bool captured{};         // stereo::extra_mesh has a capture of the primary
		std::uint64_t attempts{}; // capturing renders since the last forget
		bool sling_valid{};      // the body frame (sling point) is known
	};

	struct sling_primary_result
	{
		bool capture{}; // hide the primary for a capture now
		bool show{};    // draw the copy at the sling
		bool forget{};  // the capture is of another gun: drop it
	};

	struct sling_primary_config
	{
		float settle_seconds{ 1.0f }; // the primary in hand this long (the draw animation over) before a capture
		std::uint64_t max_attempts{ 3 }; // each attempt hides the gun for one render; then give up for this gun
	};

	class sling_primary
	{
	public:
		explicit sling_primary(const sling_primary_config& config = {}) : config_(config) {}
		sling_primary_result update(const sling_primary_frame& frame);
		const std::string& primary_name() const { return name_; }
		void reset();

	private:
		sling_primary_config config_;
		std::string name_; // the primary the capture belongs to
		float in_hand_{};  // seconds the primary has been the weapon
	};
}
