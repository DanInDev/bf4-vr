#pragma once

// Feedback sounds for the physical weapon gestures: cloth when a magazine or grenade comes out of a
// pouch, clicks for a magazine going in or out, a rack for chambering, a ping for a grenade pin.
// Synthesised at start; a WAV of the same name in "bfvr_sounds" next to bfvr.ini replaces one
// (e.g. bfvr_sounds\magazine_in.wav), and bfvr_sounds\<sound folder>\<cue>.wav replaces it for one gun
// (bfvr_sounds\AK12\bolt_back.wav). tools/game_cues.py cuts those from the game's own files. Plays
// asynchronously on the default audio device.
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
	// Whether bfvr_sounds holds a WAV for this cue (e.g. one cut from the game's own files by tools/game_cues.py).
	bool has_file(cue which);
	// The same for one gun: its sound folder's WAV ("AK12", the gun's firing sound folder, or "M320"), else as above.
	void play(cue which, const char* folder);
	bool has_file(cue which, const char* folder);
}
