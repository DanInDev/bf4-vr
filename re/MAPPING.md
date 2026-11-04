# Engine mapping plan (BF4 / Frostbite 3)

What a VR mod like this needs to know about the game, why, and how to find it, so each
reverse-engineering session produces something reusable. Verified results go into
[`symbols/bf4-179547.md`](symbols/bf4-179547.md); experiments and reasoning go into `notes/`.

## How to work a session

1. Pick one element below; state the question it answers ("which function computes the soldier's aim?").
2. Find it with the cheapest method that gives proof (table below), at a fixed checkpoint in the campaign.
3. Verify by **changing it** and observing the effect (screenshot, pipe status), not just by reading it.
4. Record: address or offset, prologue bytes for functions, how verified, build (179547), what uses it.
5. Never commit dumps, screenshots or game files ([SAFETY](../docs/SAFETY.md)).

| Method | Good for | Tool |
| --- | --- | --- |
| Live memory reads | Following pointer chains, reading structures | pipe `peek`, `tools/camera_probe.py` |
| Hardware write-watch | "Who writes this value every frame?" plus a backtrace | pipe `watch <addr> [len] [ms]` |
| Single-function disassembly | Reading a writer/caller found by `watch` | `tools/disasm.py`, `tools/callers.py` (decrypted dump) |
| Full static analysis | Cross-references, strings, larger subsystems | REA + Ghidra on the dump (about 10 min per `open_binary`; docs/TOOLING.md) |
| Type information | Class names, field names/offsets of engine types | Frosty `Profiles\BF4SDK.dll`; in-memory `typeinfo` section |
| Frame capture | Render passes, constant buffers, which draw is the weapon/HUD | RenderDoc |
| Console | Live engine settings and their names | `tools/console-probe.ps1` (`list`, `<Setting> <value>`) |
| A/B experiments | Proving cause and effect | pipe `eye`, `fov`, `view`, screenshots |

## Elements to map, by area

Status: ✅ mapped and verified · 🟡 partly known · ⬜ not started. Issue numbers refer to GitHub issues.

### 1. Engine infrastructure (reuse multipliers, map first)

| Element | Why | How | Status |
| --- | --- | --- | --- |
| **Runtime type registry** (`typeinfo`/`fieldinf` in memory): class and field names with offsets | Turns every later search into a lookup; replaces guessing offsets | Done: `tools/typeinfo_dump.py`, vtable → class naming of live objects ([typeinfo.md](notes/typeinfo.md)) | ✅ |
| Game-thread per-frame hook point | Apply camera/input/IK at the right moment each frame | Have one: view-params copy `0x14083A710` (once per frame) | ✅ |
| Render-thread frame boundary | Pair eyes and frames reliably (#6) | Present hook (have); find the render-frame begin to tag frames | 🟡 |
| Console/settings setter | Our settings menu can set game options (DOF, blur, FOV) without the console UI (#1, #7) | `watch` a setting's storage after changing it through the console; disassemble the writer | ⬜ |
| Entity iteration / lookup by type | Find the local soldier, weapons, grenades, vehicles | TypeInfo + ClientGameContext → level → entity lists | ⬜ |
| Build identity check | Refuse to hook other builds | PE timestamp + prologue checks (have per hook) | ✅ |

### 2. Rendering and camera

| Element | Why | How | Status |
| --- | --- | --- | --- |
| GameRenderer → RenderView: transform, FOV, aspect, dirty flags, matrices | Head tracking and stereo | Done ([camera.md](notes/camera.md)) | ✅ |
| **First-person weapon pass camera/projection** | Gun glued to the view (#2); prerequisite for arms | Camera done: view-params `+0xED0`, eye pose applied ([first_person.md](notes/first_person.md)). Projection/FOV open: not a CPU-side matrix; RenderDoc next | 🟡 |
| Render resolution independent of the window | Sharp image, per-eye aspect (#1) | Find where the render-target size comes from (window size × `Render.ResolutionScale`); override for VR | ⬜ |
| Post-process settings (DOF, motion blur, TAA history) | Blurry centre (#1), per-eye history artefacts | Console names known (`WorldRender.*`, `PostProcess.*`); map their storage + setter | 🟡 |
| HUD / UI pass (Scaleform) | Head-locked crosshair and HUD (#3); render HUD to its own layer | RenderDoc: last passes before Present; find the UI render target and its draw call site | ⬜ |
| Zoom / ADS / scope FOV | Optics in VR; FOV changes while aiming | Gun pass FOV in ADS measured live (17° on the AK-12 red dot). World zoom: the game's FOV at the view copy (45/40/27°, [weapon.md](notes/weapon.md) "Magnified sights"), used by the scope render; the REA candidate `0x140FA8270` is not it. Open: other optics' lens geometry | 🟡 |
| Shadow/culling views using the camera | Culling errors when the head turns far from the game camera | Check whether culling uses RenderView 0 or another frustum (view 2 `+0x9C0`?) | ⬜ |

### 3. Player, input and aiming

| Element | Why | How | Status |
| --- | --- | --- | --- |
| **Local player chain**: ClientGameContext → player manager → local player → soldier | Everything player-related hangs off it | Global `[0x14230F260]` = local `ClientSoldierEntity`, valid across a section change; components in [symbols](symbols/bf4-179547.md). ClientPlayer itself not needed so far | ✅ |
| Authoritative view angles (yaw/pitch) | Neutralise game pitch, snap turn, head-driven yaw (#5) | Done: aimer `[[weapon+0x4988]+0x10]` +0x14/+0x18, verified by writing ([aim.md](notes/aim.md)). Open: vehicles and scripted sections | ✅ |
| **Input concepts** (Frostbite input actions such as move, fire, reload, jump) | Drive the game directly from VR controllers, no physical pad needed (#4), and gestures (M4) | Find the input-concept array the game reads each frame; `watch` it while pressing keys | ⬜ |
| Device detection for gamepads | Why XInput is never polled without a pad (#4) | Wrap `IDirectInput8::EnumDevices` (we are the dinput8 proxy); log Raw Input/SetupAPI calls | 🟡 ([gamepad.md](notes/gamepad.md)) |
| Weapon fire origin and direction | Aim with the controller (#12) | Find where a shot's ray/projectile transform is built (watch the camera-forward read during firing; Frosty `WeaponFiringData`) | ⬜ |
| Weapon sway/recoil | Keep the controller authoritative, add recoil as an offset | Frosty `GunSwayData`; watch the sway transform. Data mapped and used for the drawn gun's kick (2026-11-03, re/notes/weapon.md "Recoil data"); the runtime sway object and its offset writer `0x140FA6640` not followed (the offset stays 0 under controller aim) | 🟡 |

### 4. Body and skeleton (top priority after the camera)

| Element | Why | How | Status |
| --- | --- | --- | --- |
| **First-person arms mesh and skeleton**: bone names, hierarchy, indices | Arms/shoulders IK to the controllers (#11) | Done live: Warsaw1pAntSke01, 179 bones, hierarchy, world-space root, gun on Wep_Root; arm IK headset-tested ([skeleton.md](notes/skeleton.md)) | ✅ |
| **Pose buffer after animation, before skinning** | Overwrite shoulder/elbow/wrist bones each frame | Done: local pose Animatable+0x48, edited in a hook on `0x140716050`; moving the left hand is visible ([skeleton.md](notes/skeleton.md)) | ✅ |
| Weapon attachment bone and grip points | Hands on the gun; weapon follows the controller (#12, #14) | Frosty weapon mesh/sockets; the bone the weapon is parented to | ⬜ |
| Third-person body | Body presence, shadows (#13) | Same chain, 3P mesh | ⬜ |

### 5. Gameplay interactions (M4)

| Element | Why | How | Status |
| --- | --- | --- | --- |
| Weapon state: magazine/ammo, reload state machine | Physical reloads (#14) | TypeInfo fields of the soldier weapon; watch ammo while firing | ⬜ |
| Grenade throw: projectile spawn + velocity | Physical throwing (#14) | Watch/trace the throw; find the spawn function and its velocity input | ⬜ |
| Melee and interaction ("use") triggers | Knife, doors, pickups (#15) | Input concepts + the functions they call | ⬜ |
| Weapon switching / inventory | Holsters, body slots | Soldier weapons component | ⬜ |
| Haptic sources: fire, damage, explosions | Controller vibration | Hook the events or XInputSetState (we already own XInput) | 🟡 |

### 6. Game state and safety

| Element | Why | How | Status |
| --- | --- | --- | --- |
| **Single-player vs multiplayer** | Disable all hooks in MP (#19) | Level path / game mode from ClientGameContext; server connection state | ⬜ |
| Menu, loading, pause states | Theater fallback, input routing | Have a heuristic (menu camera identity at (0,1,0)); replace with real state | 🟡 |
| Cutscenes and scripted cameras | Camera policies (head lock vs free look) (M4) | Camera-owner fields; watch the transform source switching | ⬜ |
| Vehicles | Keep joystick control, seat-relative camera | Soldier → vehicle entry state | ⬜ |
| Current level and checkpoint | Reproducible tests, per-mission rules | Level name string / ClientLevel | ⬜ |

## Suggested session order

1. **Type registry dumper:** makes every later step faster.
2. **Local player chain and view angles:** pitch fix and turning (#5); a base for aiming.
3. **First-person weapon pass** (RenderDoc + watch): gun fix (#2), prerequisite for the arms.
4. **Input concepts:** controllers without a pad (#4) and direct VR actions.
5. **Arms skeleton and pose buffer:** arms IK (#11).
6. **Weapon fire origin and direction:** aim with the controller (#12).
7. **Game state (SP/MP, menus, cutscenes):** MP guard (#19) and a better menu detection.
8. **HUD pass and settings setter:** HUD layer (#3) and the in-game settings menu (#7).

## Reuse beyond BF4

Most items exist in every Frostbite 2/3 game of that era (BF3, BF Hardline, Battlefront 2015) with
different addresses: the GameRenderer/RenderView pattern, TypeInfo, input concepts and the
soldier/weapon component layout. Keep notes in terms of **type and field names** (from TypeInfo)
next to the raw offsets, so a port starts from names rather than from scratch.
