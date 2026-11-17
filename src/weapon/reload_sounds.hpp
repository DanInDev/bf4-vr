#pragma once

// Feedback sounds for the physical weapon gestures: cloth when a magazine or grenade comes out of a
// pouch, clicks for a magazine going in or out, a rack for chambering, a ping for a grenade pin.
// Synthesised at start (no game files); a WAV of the same name in "bfvr_sounds" next to bfvr.ini
// replaces one (e.g. bfvr_sounds\magazine_in.wav). Plays asynchronously on the default audio device.
namespace bfvr::weapon
{
	enum class cue
	{
		cloth,
		magazine_in,
		magazine_out,
		chamber,   // a whole rack: back and home
		pin,
		bolt_back, // the charging handle reaching its rear stop
		bolt_home, // released from the rear: the bolt slams home
		count,
	};

	void set_sounds(bool enabled);
	// #82: the grenade pin pull's level, 0..1 (setting pin_volume); a WAV in bfvr_sounds still replaces the sound.
	void set_pin_volume(float volume);
	void play(cue which);
}
