#include "body/soldier_radius.hpp"

#include <algorithm>
#include <cmath>

namespace bfvr::body
{
	namespace
	{
		bool plausible(float radius)
		{
			return std::isfinite(radius) && radius >= 0.05f && radius <= 1.0f;
		}

		bool same(float a, float b)
		{
			return std::fabs(a - b) < 1e-4f;
		}
	}

	radius_action radius_keeper::update(std::uintptr_t asset, float current, float wanted)
	{
		if (!asset || !plausible(current))
		{
			return {};
		}
		if (asset != asset_)
		{
			asset_ = asset;
			original_ = current;
			written_ = false;
		}
		if (wanted > 0.0f)
		{
			const float value = std::clamp(wanted, soldier_radius_min, soldier_radius_max);
			if (same(current, value))
			{
				return {};
			}
			written_ = true;
			return { true, value };
		}
		if (written_ && !same(current, original_))
		{
			written_ = false;
			return { true, original_ };
		}
		written_ = false;
		return {};
	}
}
