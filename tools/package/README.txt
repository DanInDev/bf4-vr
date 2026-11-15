bf-vr: VR for the Battlefield 4 single-player campaign
======================================================

Hobby research project, not affiliated with EA or DICE. You need your own copy of
Battlefield 4 (EA app, final build 179547). This package contains only the mod's own
code: no game files.

Contents
  bfvr_launcher.exe       sets BF4 up for VR, installs/removes the mod, edits settings, starts BF4
  dinput8.dll             the mod; the launcher copies it into the BF4 folder
  bfvr.ini.example        settings template (the launcher creates bfvr.ini from it)
  quest3-controls.svg     what each controller button and gesture does (open in a browser)
  grab-zones.svg          where on the body to reach for weapons, magazines and grenades
  LICENSE.txt             licence of the mod
  THIRD-PARTY-NOTICES.txt licences of MinHook and the OpenXR loader built into the mod

Use
  1. Unzip anywhere (not into the BF4 folder) and run bfvr_launcher.exe.
  2. The launcher finds BF4 through the registry and checks the build. Setting up is
     only possible for build 179547 and while BF4 is closed. Start BF4 once without the
     mod first if you never have, so it creates its settings.
  3. "Set up for VR" does everything in one go: installs the mod, sets the mod's
     bfvr.ini for VR (head-tracked 3D, sharp 2160x2160 image, view crop, HUD layer) and
     changes BF4's own settings that VR needs (DirectX 11, post anti-aliasing off, no
     weapon depth of field or motion blur, vsync off, pad vibration on). BF4's Video
     option "Resolution Scale" still works on top: higher is sharper and slower. Your normal BF4
     settings are kept for normal mode. The "VR setup" lines show what is still
     to change and which VR runtime the mod will use.
  4. Start SteamVR (Steam Link) or Meta Horizon Link, then "Start Battlefield 4" (goes
     through the EA app). In VR, hold the left menu button for the mod's own settings.
  5. The "VR mode" switch on the Play tab flips between VR and normal play (BF4 closed). Each
     mode keeps its own BF4 settings (PROFSAVE_profile.bfvr-normal / .bfvr-vr in
     Documents\Battlefield 4\settings); switching swaps them and installs or removes the mod.
     Campaign progress is shared.
  6. Problems: "Open bfvr.log" shows the mod's log from the game folder.

SAFETY: CAMPAIGN ONLY
  Never join a multiplayer server while the mod is installed. PunkBuster bans for foreign
  DLLs in the game process. Click "Remove mod (required before multiplayer)" first; it
  deletes dinput8.dll from the BF4 folder. Do not run the mod alongside games that use
  kernel anti-cheat (Battlefield 6, EasyAntiCheat titles).

Uninstall
  Switch "VR mode" off in the launcher (removes the mod, brings back your normal settings), then delete this folder. bfvr.ini and bfvr.log stay in the
  BF4 folder as plain text files; delete them too if you like. EA app "Repair" restores the
  original game files if anything else is wrong.

There is no installer yet: the zip is the release format.
