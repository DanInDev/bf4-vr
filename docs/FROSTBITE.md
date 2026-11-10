# How the mod handles the Frostbite engine

A summary of what the mod knows about Battlefield 4's engine (Frostbite 3, build 179547, x64) and
how it uses it. Each section names the note that holds the evidence. Addresses and prologue bytes:
[`re/symbols/bf4-179547.md`](../re/symbols/bf4-179547.md). Module layout: [ARCHITECTURE.md](ARCHITECTURE.md).

## Getting at the code

- `bf4.exe` has no ASLR (fixed base `0x140000000`), so addresses are stable between runs.
- `.text`, `.data`, `typeinfo` and `fieldinf` are DRM-encrypted on disk and decrypted in memory
  after the DLL has loaded. Analysis therefore uses a memory dump taken at the main menu
  (`pipe dump`, local only, never committed), and hooks wait until the code is there.
- **Every hook checks bytes first.** A hook is an address plus the 15 to 24 bytes expected there;
  a wait thread polls (up to two minutes) until they match, then installs the MinHook detour. On any
  other build nothing matches and the feature stays off with a log line. Data pointers are checked
  only for null and readability.
- The launcher refuses to install on another build (PE timestamp `0x58AB1CC0`).

## Naming things: the type registry

Frostbite keeps reflection data for its data classes: names, sizes, fields and offsets, enums
(10,210 types). `tools/typeinfo_dump.py` reads it from the dump; `object_probe.py` and
`global_scan.py` name live objects by their vtable, because each class's `getType()` returns its
ClassInfo ([typeinfo.md](../re/notes/typeinfo.md)).

- Data classes (`SoldierWeaponData`, `FiringFunctionData`, `WorldRenderSettings`) have full layouts.
- Runtime classes (`ClientSoldierEntity`, `ClientSoldierWeapon`) have a name and size but no fields.
- `GameRenderer`, `RenderView`, `ClientGameContext` and the input layer are not reflected: their
  offsets were found by experiment (hardware write-watch, live reads, disassembly).

## Camera and views ([camera.md](../re/notes/camera.md), [first_person.md](../re/notes/first_person.md))

- Chain: global `GameRenderer*` → `+0x60` view params → `RenderView`: world transform `+0x40`
  (rows right, up, forward, position; metres), vertical FOV `+0xB4`, aspect `+0xC4`, dirty flags
  `+0xE0` (set them and the engine rebuilds its matrices).
- Once per frame the game thread copies the view params into the renderer. **That copy is the
  mod's main hook**: after it, the mod writes the eye pose and FOV into the copy. It is also the
  mod's game-thread tick.
- The gun and arms use a second camera in the same block (`+0xED0`, soldier space). The head pose is
  applied to both, which is what separates the head from the gun.
- The gun pass has its own FOV (45° at the hip, per zoom level while aiming). The mod writes that
  FOV in game memory so the gun is drawn life size ([weapon.md](../re/notes/weapon.md)).

## Two eyes ([stereo.md](../re/notes/stereo.md), [stereo-rendering.md](../re/notes/stereo-rendering.md))

- BF4 has stereo settings but no stereo renderer: its 3D relied on NVIDIA's driver duplicating
  draws, which no longer exists. The world renderer has room for exactly one view, and running the
  world render module twice crashes.
- What works: hooking the **per-view world render** (G-buffer through tonemap) and calling it twice
  inside one game frame. The frame-level work around it still runs once.
- The second eye's camera cannot simply be set, because the engine has already written the view
  into its constant buffers (large dynamic "arenas"). The mod tracks every constant block the first
  eye binds, reads it back, replaces each matrix or position that belongs to the centre camera with
  the second eye's, and redirects the bindings to the patched copies during the second render
  (`stereo/eye_pass.cpp`, `constant_patch.cpp`). The first-person per-view block is recognised by
  its layout.
- A magnified scope is a third render of the same kind with a zoomed camera, cut to the lens circle.
- Temporal anti-aliasing (SMAA T2x) would blend the two eyes; the mod switches it to SMAA 1x while
  pair rendering is on.

## HUD ([hud.md](../re/notes/hud.md))

- The HUD (Scaleform-like "Twinkle" UI) is drawn by a UI render module straight into the back
  buffer after the world. The mod hooks that function and swaps the render target while it runs, so
  the HUD lands in its own transparent layer and is shown as quads in the headset.
- HUD elements are widget entities sharing one `isVisible` function; hooking it hides single
  elements by class.

## Soldier, aim and input ([aim.md](../re/notes/aim.md), [gamepad.md](../re/notes/gamepad.md))

- A global holds the local `ClientSoldierEntity`. From it: the weapons component and current
  weapon, the aimer (yaw and pitch, the source of the view angles), stance, health.
- **View angles** are written into the aimer; the game integrates stick input on top. Snap and
  smooth turn, controller aim and neutral pitch all work this way.
- **Input** goes through a virtual Xbox pad: XInput hooks merge the controllers into user 0, and a
  wrapped DirectInput enumeration plus a hook on the game's "is this an XInput device" check make
  the game poll without a physical pad. Keys (weapon slots) go through Raw Input, the menu cursor
  through window message and cursor hooks. The engine's own input actions are not mapped yet.
- **Shots**: each weapon has a shot frame (origin and direction). The function that sets it is
  hooked, so bullets start at the drawn gun and fly along its barrel.

## Skeleton ([skeleton.md](../re/notes/skeleton.md))

- The first-person rig (179 bones) hangs off the soldier's animatable component: a local pose
  (per-bone transform relative to the parent) and a model pose.
- The mod hooks the local-to-model pose conversion, which runs after animation and before skinning,
  and edits bones there: two-bone arm IK to the controllers, the gun on the hand, fingers, torso.
- Bone names come from the skeleton asset; weapon parts are bones too (`Wep_Root`, `Wep_Mag`,
  `Wep_Slide`), which is how the mod knows where the magazine well and the sight line are.

## Weapons and gameplay ([weapon.md](../re/notes/weapon.md), [meshes.md](../re/notes/meshes.md), [sound.md](../re/notes/sound.md), [haptics.md](../re/notes/haptics.md))

- **The campaign's server runs inside the game process**, and it owns the ammo. Physical reload
  writes the magazine and reserve counts in the server-side weapon.
- **Grenades**: the throw's launch data (position, direction, speed) is rewritten for one throw.
- **Extra meshes**: skinned draws carry a 64-matrix bone palette in vertex-shader slot 0. The mod
  re-issues the game's own draw with an edited palette to show a second magazine or a held grenade;
  it finds the right draw by a tiny bone-scale tag written in the pose hook.
- **Sounds**: the game's reload sounds are sound-entity events; the mod fires them at the physical
  reload's steps.
- **Rumble**: the game zeroes rumble without keyboard focus, so the mod hooks the setter before
  that gate, and makes its own firing feedback from the magazine count dropping.
- **Recoil** data (`GunSwayData`) drives the drawn gun's kick.

## Game state and safety

- Level path and its `IsMultiplayer` flag come from the game context; the guard allows only the
  front end and `Levels/SP/...` ([SAFETY.md](SAFETY.md)).
- Menus are recognised by the menu camera (identity at (0, 1, 0)); cutscenes by the camera leaving
  the aimer's direction; the pause menu by its letterbox. These are heuristics, not engine state.

## Multiplayer in BF4: why it stopped ([server.md](../re/notes/server.md))

The retail client still contains a hosted mode and a direct-connect join, and the engine-level
server checks no EA login. But the client has no LAN server backend, and an MP map on the local
server ended with "The connection with EA Online was lost" after 1.5 minutes: the client wants an
EA Online session for MP levels. The research code is on the branch `re/local-server`. This is why
multiplayer work moved to Battlefield 3 and Venice Unleashed (repository `bf3-vr`).

## What carries over to another Frostbite game

The same structures exist in Frostbite 2 (BF3) under the same names, at other addresses: the
renderer and view chain, the type registry, the soldier and weapon components, the pose buffers.
See [`re/MAPPING.md`](../re/MAPPING.md) for the list of elements and the method for finding each.
Engine-free code (IK, reload and grenade state machines, eye maths, constant patching, UI, OpenXR)
moves unchanged; hooks, offsets and asset names must be found again.
