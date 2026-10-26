#pragma once

namespace bfvr::control
{
	// Research: a first vectored exception handler that logs fatal exceptions (access violation,
	// illegal instruction, stack overflow, ...) to bfvr.log with registers and the unwound stack,
	// then lets the game handle them. The game's own unhandled-exception path uploads a crash
	// report over HTTPS and can spin there forever (2026-10-25), so its stack hides the fault.
	void install_crash_log();
}
