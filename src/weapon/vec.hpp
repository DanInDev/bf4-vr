#pragma once

// Plain vector and quaternion for the engine-free weapon code (OpenXR LOCAL metres, x y z w).
namespace bfvr::weapon
{
	struct v3
	{
		float x{}, y{}, z{};
	};

	struct q4
	{
		float x{}, y{}, z{}, w{ 1 };
	};
}
