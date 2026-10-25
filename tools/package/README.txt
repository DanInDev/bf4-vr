bf-vr: VR for the Battlefield 4 single-player campaign
======================================================

Hobby research project, not affiliated with EA or DICE. You need your own copy of
Battlefield 4 (EA app, final build 179547). This package contains only the mod's own
code: no game files.

Contents
  bfvr_launcher.exe       installs/removes the mod, edits settings, starts BF4
  dinput8.dll             the mod; the launcher copies it into the BF4 folder
  bfvr.ini.example        settings template (the launcher creates bfvr.ini from it)
  LICENSE.txt             licence of the mod
  THIRD-PARTY-NOTICES.txt licences of MinHook and the OpenXR loader built into the mod

Use
  1. Unzip anywhere (not into the BF4 folder) and run bfvr_launcher.exe.
  2. The launcher finds BF4 through the registry and checks the build. "Install mod"
     is only possible for build 179547 and while BF4 is closed.
  3. Pick the settings, "Save settings", then "Start Battlefield 4" (goes through the EA app).
  4. Problems: "Open bfvr.log" shows the mod's log from the game folder.

SAFETY: CAMPAIGN ONLY
  Never join a multiplayer server while the mod is installed. PunkBuster bans for foreign
  DLLs in the game process. Click "Remove mod (required before multiplayer)" first; it
  deletes dinput8.dll from the BF4 folder. Do not run the mod alongside games that use
  kernel anti-cheat (Battlefield 6, EasyAntiCheat titles).

Uninstall
  "Remove mod" in the launcher, then delete this folder. bfvr.ini and bfvr.log stay in the
  BF4 folder as plain text files; delete them too if you like. EA app "Repair" restores the
  original game files if anything else is wrong.

There is no installer yet: the zip is the release format.
