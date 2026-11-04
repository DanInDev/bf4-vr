#pragma once

#include <cstdint>
#include <string>

namespace bfvr::control
{
	// Research tool: arms a hardware data-write breakpoint (debug register DR0) on every thread
	// of the game for `duration_ms`, then reports each instruction that wrote `address`, with
	// hit counts. Equivalent to a debugger's "find what writes to this address", in-process.
	// `length` must be 1, 2, 4 or 8 and `address` aligned to it. With `reads`, reads trap too
	// ("find what accesses this address").
	std::string watch_writes(std::uintptr_t address, unsigned length, unsigned duration_ms, bool reads = false);
}
