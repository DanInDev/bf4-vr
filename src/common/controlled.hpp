#pragma once

#include <cstdint>

// What the player controls (BF4 179547). The global the mod took for "the local soldier" (0x14230F260) is
// the player's controlled entity: a ClientSoldierEntity on foot, the ClientVehicleEntity while driving
// (live 2026-11-19, Baku car checkpoint; re/symbols/bf4-179547.md). Soldier offsets read through it in a
// vehicle walk the vehicle's memory, so every soldier read goes through local_soldier(), which answers 0
// unless the object really is a soldier. x86 (BF3) has none of this: 0 / false.
namespace bfvr::game
{
	std::uintptr_t controlled_entity();
	// The controlled entity when it is a ClientSoldierEntity, else 0.
	std::uintptr_t local_soldier();
	// The controlled entity when it is a ClientVehicleEntity (driver or any seat), else 0.
	std::uintptr_t controlled_vehicle();
	inline bool in_vehicle() { return controlled_vehicle() != 0; }
}
