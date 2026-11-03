#pragma once

#include "body/finger_pose.hpp"

// The controllers' finger sensors (squeeze, trigger, touches), per rig arm: 0 = the off hand (left
// rig arm), 1 = the gun hand, with dominant_hand already applied. Drives the drawn fingers.
namespace bfvr::input
{
	void publish_fingers(const body::fingers::finger_input (&hands)[2]);
	// The last sync's values; invalid when none arrived for 250 ms (no session, unfocused). A test
	// value set below replaces a hand's reading.
	void latest_fingers(body::fingers::finger_input (&hands)[2]);
	// Pipe "fingers": a fixed reading for one hand (monitor tests without a headset).
	void set_finger_override(int hand, bool active, const body::fingers::finger_input& input);
}
