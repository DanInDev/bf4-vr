#pragma once

#include <string>

namespace bfvr::control
{
	// Research: suspends each game thread in turn and returns its call stack (up to 16 return
	// addresses, unwound with the image's unwind data) plus its CPU time, one line per thread.
	// Finds where a hung or spinning thread is stuck.
	std::string thread_stacks();
}
