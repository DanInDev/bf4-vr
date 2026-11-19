#include "common/controlled.hpp"

#include <windows.h>

namespace bfvr::game
{
#if defined(_M_X64)
	namespace
	{
		constexpr std::uintptr_t controlled_global = 0x14230F260;
		constexpr std::uintptr_t soldier_vtable = 0x141E600C0; // ClientSoldierEntity (typeinfo vtable map)
		constexpr std::uintptr_t vehicle_vtable = 0x141D3D200; // ClientVehicleEntity (typeinfo; live in the Baku car)

		std::uintptr_t read_pointer(std::uintptr_t address)
		{
			std::uintptr_t value{};
			SIZE_T done{};
			return address && ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<LPCVOID>(address), &value, sizeof(value), &done) && done == sizeof(value)
				? value
				: 0;
		}

		std::uintptr_t controlled_of(std::uintptr_t vtable)
		{
			const std::uintptr_t entity = controlled_entity();
			return entity && read_pointer(entity) == vtable ? entity : 0;
		}
	}

	std::uintptr_t controlled_entity() { return read_pointer(controlled_global); }
	std::uintptr_t local_soldier() { return controlled_of(soldier_vtable); }
	std::uintptr_t controlled_vehicle() { return controlled_of(vehicle_vtable); }
#else
	std::uintptr_t controlled_entity() { return 0; }
	std::uintptr_t local_soldier() { return 0; }
	std::uintptr_t controlled_vehicle() { return 0; }
#endif
}
