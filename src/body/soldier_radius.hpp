#pragma once

#include <cstdint>
#include <string>
#include <string_view>

// The soldier's collision radius (#53, re/notes/collision.md): BF4 keeps the soldier 0.35 m from walls,
// so with the stick alone the head cannot reach past a window ledge. The radius lives in the level's
// CharacterPhysicsData asset (soldier +0x30 -> +0x118 -> +0x48) and is read when a soldier spawns: a
// write applies from the next respawn or checkpoint restart. Live setting soldier_radius (0 = the
// game's own).
namespace bfvr::body
{
	inline constexpr float soldier_radius_min = 0.10f;
	inline constexpr float soldier_radius_max = 0.35f;

	struct radius_action
	{
		bool write{};
		float value{};
	};

	// Engine-free: what to write into the asset that holds `current`, for the wanted radius (<= 0: the
	// game's own). Remembers the game's value per asset (a new level brings a new asset) and puts it back
	// once the setting returns to 0. A value that is no radius (a wrong pointer) is left alone. Tested by
	// tests/soldier_radius_test.cpp.
	class radius_keeper
	{
	public:
		radius_action update(std::uintptr_t asset, float current, float wanted);
		std::uintptr_t asset() const { return asset_; }
		float original() const { return original_; }

	private:
		std::uintptr_t asset_{};
		float original_{};
		bool written_{};
	};

	// The spawn builds every character's shapes, AI included (monitor 2026-11-13: "Characters/Soldiers/
	// AISoldierPhysics" beside the player's "Gameplay/Soldiers/DefaultSoldierPhysics_SP"): only the player's
	// physics asset is changed, recognised by its asset name.
	inline bool is_player_physics(std::string_view asset_name)
	{
		return asset_name.rfind("Gameplay/Soldiers/DefaultSoldierPhysics", 0) == 0;
	}

	// Game side (x64; BF3 builds keep the game's radius).
	void set_soldier_radius(float metres);
	// Present thread: finds the local soldier's physics data and keeps its radius (twice a second).
	void update_soldier_radius();
	// Hooks the spawn's pose shape builder (0x140B412A0, prologue checked) so the radius applies from every
	// spawn, a level's first included; without it the setting applies after a respawn.
	bool install_soldier_radius_hook();
	// Pipe "radius": the asset, the game's value, the current and wanted values.
	std::string soldier_radius_report();
}
