#pragma once

// The game's pad rumble before its own gates (#32, re/notes/haptics.md): BF4's rumble setter
// (0x14094D490, build 179547) takes the wanted low/high motor levels (0..1) every frame but zeroes
// them unless the game window has keyboard focus (GetFocus), which a headset player's window often
// lacks. The hook hands the wanted levels to input/haptics. Waits for the DRM to decrypt the code;
// x64 only. With it in place the XInputSetState capture stays out of the mix.
namespace bfvr::hooks
{
	bool install_rumble_hook();
	bool rumble_hook_active();
}
