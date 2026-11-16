#include "weapon/crate_guns.hpp"

namespace bfvr::weapon::crate
{
	std::string change_watch::update(std::string_view value, std::uint64_t now_ms, bool busy)
	{
		if (!seen_)
		{
			seen_ = true;
			applied_ = current_ = value;
			return {};
		}
		if (value != current_)
		{
			current_ = value;
			since_ = now_ms;
		}
		if (current_ == applied_ || busy || now_ms - since_ < settle_ms_)
		{
			return {};
		}
		applied_ = current_;
		return current_ == "keep" ? std::string() : current_;
	}
}
