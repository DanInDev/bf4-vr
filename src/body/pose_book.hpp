#pragma once

#include <string>

#include "body/pose_tweaks.hpp"

// The pose editor's live tweaks per grip (body/pose_tweaks), shared by the settings (writer) and the
// finger shaping (reader), and their file bfvr_poses.ini beside bfvr.ini. Starts from the built-in
// defaults with the file read over them on first use.
namespace bfvr::body::pose_book
{
	poses::table current();
	poses::tweak get(poses::grip which);
	void set(poses::grip which, const poses::tweak& value);

	// Built-in defaults, then the file over them; "" or an "error: ..." line, warnings in bfvr.log.
	std::string load();
	// Writes every grip's tweak to the file; "saved <path>" or "error: ...".
	std::string save();
	// Pipe "poses": the file, and the live tweaks in its format.
	std::string report();
}
