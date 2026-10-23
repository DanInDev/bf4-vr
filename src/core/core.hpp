#pragma once

namespace bfvr::core
{
	// Starts the in-game runtime on its own thread. Safe to call from DllMain.
	void start();
}
