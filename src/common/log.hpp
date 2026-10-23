#pragma once

#include <windows.h>

namespace bfvr::log
{
	// Log lines go to bfvr.log beside the executable that loaded us.
	// Uses only kernel32 file calls, so it is safe inside DllMain.
	void write(const char* format, ...);
}
