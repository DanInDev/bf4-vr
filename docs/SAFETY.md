# Safety rules

These protect your EA account, your Battlefield 6 install and the legal standing
of the project. They apply to every stage.

## Anti-cheat

1. **Campaign only.** Never join a multiplayer server while any mod file is in the
   BF3 folder. PunkBuster (`PnkBstrA`, running on this PC) scans the game process
   on MP servers and bans for foreign DLLs.
2. **Remove the proxy before multiplayer.** `dinput8.dll` in the game folder is
   loaded every time `bf3.exe` starts. Delete it, or keep it disabled, before
   playing online.
3. **Battlefield 6 uses kernel-level EA AntiCheat (Javelin).** Do not run debuggers,
   memory tools, Cheat Engine, x32dbg, RenderDoc or REA analysis sessions while
   Battlefield 6 is running, and close BF6 completely first. Its driver can flag
   such tools system-wide, independent of which game they target.
4. Never use these tools on any game that has active anti-cheat. Close every game
   using EasyAntiCheat (`EasyAntiCheat_EOS` is installed on this PC) before RE sessions.
5. PunkBuster is being removed from this PC, which only costs ranked BF3 MP. If
   the EA app re-installs it during a repair, run `pbsvc.exe -u` again.

## DRM and copyright

6. `bf3.exe` is DRM-encrypted on disk. Analyze the decrypted code **in memory, for
   your own interoperability research**. Do not patch out, bypass or redistribute
   the DRM, a decrypted executable, or memory dumps.
7. Never commit or share game files, dumps, Ghidra/IDA databases or extracted
   assets. `.gitignore` excludes `re/dumps/` and `re/projects/`.
8. If the mod is ever published, ship only your own code (like Overlord does) and
   require users to own the game.

## Your machine

9. Keep a note of everything copied into the game folder. EA app **Repair**
   restores the original files if something breaks.
10. Install tools from official sources only (Ghidra from NSA's GitHub releases,
   x64dbg from x64dbg.com/GitHub, RenderDoc from renderdoc.org, REA from
   `morluto/rea` / `rea-agents` on npm).
