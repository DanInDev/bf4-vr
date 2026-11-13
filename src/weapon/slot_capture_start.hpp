#pragma once

// #41: the grenade at its pouch and the knife in its holster are copies of the game's own meshes,
// recorded while each is the weapon in hand. Until then their slots were empty (headset 2026-11-12).
// At level start, once the gun has settled in hand, each is selected for a moment and recorded
// (weapon_swap's capture sequence), the grenade first, then the knife, once per level.
// Engine-free; tests/slot_capture_start_test.cpp.
namespace bfvr::weapon
{
	enum class slot_capture_request
	{
		none,
		grenade,
		knife,
	};

	struct slot_capture_frame
	{
		bool enabled{};        // setting slot_models_at_start
		bool slot_valid{};     // a soldier with weapons (false while loading, in menus, dead)
		int slot{ -1 };        // the current weapon slot (0, 1 = the guns)
		float seconds{};
		bool busy{};           // a weapon switch or a capture is running
		bool primary_pending{}; // the primary's own capture (#58) is still to come: let it go first
		bool grenade_captured{};
		bool knife_captured{};
	};

	struct slot_capture_config
	{
		float settle_seconds{ 2.5f };  // a gun in hand this long before anything is selected
		float rearm_seconds{ 3.0f };   // no soldier this long (a level load, a death): try again
		int tries{ 2 };                // per item and level (a fresh level drew the grenade too late once)
	};

	class slot_capture_start
	{
	public:
		explicit slot_capture_start(const slot_capture_config& config = {}) : config_(config) {}
		slot_capture_request update(const slot_capture_frame& frame);
		bool armed() const { return armed_; }

	private:
		slot_capture_config config_;
		bool armed_{ true };
		int tried_grenade_{};
		int tried_knife_{};
		float gun_for_{};
		float invalid_for_{};
	};
}
