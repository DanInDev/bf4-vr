# Architecture and roadmap

The design borrows Overlord's proven split: a thin loader, then infrastructure
(hooks, VR runtime, rendering), then gameplay on top. Gameplay code never talks
to the VR runtime directly; it consumes one published input/pose snapshot per frame.

```
bf4.exe ──loads──► dinput8.dll (src/loader)          proxy: forwards DirectInput8Create
                        │
                        ▼
                 core (src/core)                       start-up, config, log
                        │
     ┌──────────────────┼──────────────────────┐
     ▼                  ▼                      ▼
 hooks (MinHook)   render (D3D11/DXGI)     xr (OpenXR)
 engine functions  Present hook, eye       session on the game's D3D11 device,
 by fixed address  targets, stereo path    swapchains, poses, actions
     │                  │                      │
     └──────────► camera ◄─────────────────────┘    per-eye view/projection
                        │
                        ▼
                 input / gameplay                   controller → game input, aim, later hands
```

Implemented: `loader`, `common`, `core`, `hooks` (Present only) and `xr` (theater
mode). The present hook currently feeds `xr` directly; `render`, `camera`, `ui`
and `input` are added when their stage starts.

### Theater mode (stage 1) as built

- `core::start()` runs from `DllMain` on a new thread, creates the theater and installs the Present hook.
- The hook reads the `Present` address from a throwaway swap chain on a hidden window, then detours it with MinHook.
- On each game Present, `xr::theater`:
  1. Lazily creates the OpenXR instance (`XR_KHR_D3D11_enable`), finds the headset (retrying every 2 s while none is available), checks the adapter LUID and creates the session on **the game's own device**.
  2. Each frame: `xrWaitFrame` (paces the game to the headset), copies the back buffer into an sRGB swap chain of the same format family, and submits one quad layer 2.5 m ahead, 3 m wide, in LOCAL space.
  3. Handles READY/STOPPING/EXITING/LOSS_PENDING, device recreation and window resizes. It gives up after 30 consecutive frame errors.
- `xrWaitFrame` runs on a dedicated waiter thread. Present submits only when a headset frame is ready and never blocks, so a throttled headset (taken off, dashboard open) cannot slow the game. Each waited frame is always begun and ended, then the next wait is requested.
- Controller actions (Oculus Touch bindings) are synced on each Present and published as an Xbox pad. The XInput hook merges that pad into `XInputGetState(0)` in every loaded XInput DLL.
- Open risks: a single-threaded D3D11 device, an MSAA back buffer (shows an empty frame) and Present called from multiple threads. Under investigation: 6.8 fps observed while SteamVR held the session in `SYNCHRONIZED`; per-call timings in `pipe.ps1 status` will show which call blocks.

## Module responsibilities

| Module | Responsibility | Key unknowns to reverse-engineer |
| --- | --- | --- |
| `loader` | Be loaded as `dinput8.dll`, forward to the system DLL, start `core` | none |
| `core` | Logging, config file, module start/stop order | none |
| `hooks` | Install/remove MinHook detours, verify bytes before patching (as Overlord does) | target function addresses (code only exists decrypted in memory) |
| `render` | Hook `IDXGISwapChain::Present`, own eye render targets, drive the engine's stereo path | where Frostbite selects left/right eye; render target creation |
| `xr` | OpenXR instance/session bound to the game's D3D11 device, swapchains, frame loop, actions | none (standard API) |
| `camera` | Replace view/projection per eye with HMD pose and per-eye FOV | camera/view-matrix structures (use `typeinfo`) |
| `ui` | Capture the Scaleform HUD/menus into a quad/cylinder layer | HUD render pass |
| `input` | OpenXR actions → game input (sticks, buttons), later decoupled aim | input/command structures |

## Stages

| Stage | Goal | Done when |
| --- | --- | --- |
| **0. In-process** | Our DLL loads inside the game and logs | `bfvr.log` shows the game exe, matching `pointer_bits` (BF4: 64) and a forwarded `DirectInput8Create` |
| **1. Theater mode** | OpenXR session shows the flat game on a virtual screen in the Quest | Campaign playable on a floating screen; proves D3D11 ↔ OpenXR inside the game process |
| **2. Head-tracked stereo** | Each eye from the HMD pose via alternate-eye rendering (the engine has no usable stereo path; see `re/notes/stereo.md`). **Implemented behind `stereo = 1`; camera control verified on screen, headset test pending** (`re/notes/camera.md`) | 3D world with correct scale and head tracking; gamepad controls |
| **3. Arms and weapon (top priority after the camera)** | Visible arms and shoulders IK-solved to the tracked controllers, holding the weapon; weapon direction from the controller | You see your hands on the gun and shoot where you point |
| **4+. Full VR** | Weapon handling (reloads, grenades), utility interactions, scripted-scene camera rules | Overlord-like interactions; vehicles stay on the joystick for now |

End goal, set by the user 2026-10-23: Overlord-level quality. Its most valuable parts are the
skeleton (arms/shoulders), weapon handling and utility interactions, so stage 3 follows the camera
directly.

Stage 1 is the recommended first VR milestone. It needs no reverse engineering,
and every later stage builds on it.

## Target game: BF4 vs BF3

The code targets either game; `BFVR_GAME` and the CMake presets (`bf4` x64,
`bf3` x86) select the architecture. BF4 is the current default.

| Aspect | BF3 | BF4 | Effect of choosing BF4 |
| --- | --- | --- | --- |
| Process | x86, 4 GB limit | **x64** | Easier: room for eye targets, standard x64 hooking and tooling |
| Type layouts | `typeinfo` readable on disk | encrypted on disk | Harder: dump from the running game or use Frosty's BF4 SDK profile |
| Asset/campaign data tooling | thin | **Frosty: full BF4 support** | Easier: inspect and tweak cameras, HUD and level logic as data |
| Built-in stereo | official (2011 patch) | strings present, not advertised | Must be verified first; stage 2 risk |
| Renderer | DX11 | DX11 or Mantle | Force DX11 |
| Anti-cheat | PunkBuster | PunkBuster (+ server-side FairFight) | Same rules |
| Launch | `bf3.exe` | `BFLauncher.exe` → `bf4.exe` | Confirm the x64 binary is what runs |

| Meta 32-bit OpenXR runtime | crashes in `xrCreateSession` (smoke test) | x64 runtime works | BF3 via Meta Link is blocked; BF3 would need SteamVR |

Net: the stages and modules stay the same. BF4 lowers risk in stages 0–1 and in
data work, but moves the "stereo path exists" question to the front.

## Stage 0 procedure (manual, when ready)

1. `cmake --preset bf4` then `cmake --build --preset bf4-debug`
2. Read [SAFETY.md](SAFETY.md). Close Battlefield 6 and BF4.
3. `tools\deploy.ps1 -Action deploy` (refuses while the game runs or a foreign `dinput8.dll` exists).
4. Start the campaign, then `tools\deploy.ps1 -Action status` shows the last log lines from `E:\EA\Battlefield 4\bfvr.log`.
5. `tools\deploy.ps1 -Action remove` before any multiplayer.

For BF3, use the `bf3` presets and `E:\EA\Battlefield 3\`.
