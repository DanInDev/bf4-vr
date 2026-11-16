#include "weapon/crate_guns.hpp"

namespace bfvr::weapon::crate
{
	bool change_watch::update(std::string_view value)
	{
		const bool first = !seen_;
		const bool changed = value != last_;
		seen_ = true;
		last_ = value;
		return !first && changed && value != "keep" && !value.empty();
	}
}
