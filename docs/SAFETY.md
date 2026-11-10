# Safety rules

These protect your EA account, your Battlefield 6 install and the legal standing
of the project. They apply to every stage.

## Anti-cheat

1. **Campaign only.** Never join a multiplayer server while any mod file is in the
   game folder. PunkBuster scans the game process on MP servers and bans for foreign
   DLLs; BF4 servers also run FairFight server-side.
2. **Remove the proxy before multiplayer.** `dinput8.dll` in the game folder is
   loaded every time `bf4.exe` starts. Remove it (launcher: **Remove mod**, or
   `tools\deploy.ps1 -Action remove`) before playing online.
3. **Battlefield 6 uses kernel-level EA AntiCheat (Javelin).** Do not run debuggers,
   memory tools, Cheat Engine, x32dbg, RenderDoc or REA analysis sessions while
   Battlefield 6 is running, and close BF6 completely first. Its driver can flag
   such tools system-wide, independent of which game they target.
4. Never use these tools on any game that has active anti-cheat. Close every game
   using EasyAntiCheat (`EasyAntiCheat_EOS` is installed on this PC) before RE sessions.
5. PunkBuster was removed from this PC on 2026-10-22, which only costs ranked MP. If
   the EA app re-installs it during a repair, run `pbsvc.exe -u` again.

## DRM and copyright

6. `bf4.exe` (like `bf3.exe`) is DRM-encrypted on disk. Analyze the decrypted code **in memory, for
   your own interoperability research**. Do not patch out, bypass or redistribute
   the DRM, a decrypted executable, or memory dumps.
7. Game assets (meshes, textures, UI) may be read from your own install or from game memory
   and used by the mod. Do not commit game files, dumps or Ghidra/IDA databases: `.gitignore`
   excludes `re/dumps/`, `re/projects/` and `re/assets/`.
8. If the mod is ever published, ship only your own code (like Overlord does) and
   require users to own the game.

## Your machine

9. Keep a note of everything copied into the game folder. EA app **Repair**
   restores the original files if something breaks.
10. Install tools from official sources only (Ghidra from NSA's GitHub releases,
   x64dbg from x64dbg.com/GitHub, RenderDoc from renderdoc.org, REA from
   `morluto/rea` / `rea-agents` on npm).

## Runtime guard (2026-10-25)

Backup only: removing `dinput8.dll` before multiplayer stays rule 2. `src/safety/mp_guard.cpp`
refuses to start on a multiplayer command line (`State_ConnectToGameId`, `State_ClaimReservation`,
`-webmode MP`) and polls the loaded level four times a second. Allowed: `Levels/FrontEnd/FrontEnd`
(its IsMultiplayer flag is 1) and `Levels/SP/...` with IsMultiplayer 0. Anything else disables every
MinHook hook for the rest of the process and blocks the pipe's commands except `status`.
Verified in single-player only: front end and campaign allowed; a deliberate false trip disabled all
hooks (theater idle) as intended.
