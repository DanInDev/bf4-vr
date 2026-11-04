#pragma once

#include <dxgi.h>

#include <string>

namespace bfvr::control
{
	// Text commands over \\.\pipe\bfvr, one command per connection (local clients only):
	//   status          runtime state, frame rate, back buffer and pad state
	//   shot <path>     PNG of the next presented frame, straight from the back buffer
	//   dump <path>     in-memory (decrypted) game image as a PE file for local analysis only
	//   peek <addr> <n> n bytes of game memory as hex (research; unreadable addresses fail cleanly)
	//   watch <addr> [len] [ms]  instructions that write addr (hardware breakpoint, research)
	//   help
	using status_provider = std::string (*)();

	void start(status_provider provider);

	// Marks this moment (mark_tap): writes the state reports, the live settings and a screenshot to
	// bfvr_marks\mark-<time>-<n>.txt/.png beside the game, on a worker thread; returns n. Any thread.
	// Pipe "mark [note]" does the same and waits for it.
	int request_mark(const char* source);

	// Call from the Present hook: measures frame rate and services screenshot requests.
	void on_present(IDXGISwapChain* swap_chain);

	// Call on the game thread (view-params copy): runs queued research calls such as
	// "entityevent" (an engine entity event, re/notes/sound.md), which must not run on the pipe thread.
	void on_game_thread();
}
