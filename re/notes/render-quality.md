# Render quality: more pixels per eye (#1, #8, smooth-stereo direction D)

Session 2026-10-25, branch `feature/render-quality`. Monitor-only measurements (no headset test yet).

## Why the headset image is soft (analysis)

- Each eye image is the whole 1920x1080 back buffer, drawn anamorphic at the headset aspect. With
  `view_crop 0.78/0.72` the rendered frustum is about ±47° horizontal and ±46° vertical, so only
  **1080 lines cover ~92° vertically**; the Quest 3 panel has 2208 per eye. Vertical pixel density is
  roughly half the panel's: this is the main blur, more than any post effect.
- The frustum is symmetric around the eye's forward axis (`camera_hook.cpp`: max of both sides), but
  the visible area is not: about +44° up / −30° down (HEADSET-TESTS 2026-10-25). About 25 % of the
  vertical lines are spent below the visible area. Idea (not built): tilt the render camera ~7° up and
  submit that tilted pose with the symmetric FOV; the compositor reprojects rotation exactly, so the
  same lines cover only the visible range (≈ +36 % vertical density, no engine RE). Must keep the arm
  IK and `+0xED0` composition on the untilted head sample.
- Post AA in the profile: `AntiAliasingPost 3` (FXAA-type, blurs the whole image) and
  `AntiAliasingDeferred 2`. Motion blur amount is 0 (`MotionBlurEnabled 1`, `MotionBlur 0`), Weapon
  DOF 0. Not measured yet.

## Facts found

| Fact | Evidence |
| --- | --- |
| BF4 is in **exclusive fullscreen**, not windowed. Editing `GstRender.FullscreenEnabled 0` in `PROFSAVE_profile` does not stick: the game writes 1 back (the earlier "windowed" note in HEADSET-TESTS is probably wrong) | Profile reread after a run; `SetFullscreenState 1` in the log |
| Profile `GstRender.ResolutionWidth/Height` beyond the monitor is ignored (closest real mode wins) | Log: `FindClosestMatchingMode 2160x2160 -> 1920x1080` |
| Start-up sequence: `CreateSwapChain 1280x720` windowed → `FindClosestMatchingMode(profile res)` → `ResizeTarget` → `ResizeBuffers` → `SetFullscreenState 1` → `ResizeBuffers` | `dxgi:` log lines, `src/hooks/swap_chain_size.cpp` |
| The mode is applied in `0x140C0B0B0` (FindClosestMatchingMode call at `0x140C0B320`); it first calls the mode lookup `0x140BE4EB0` and skips everything when that returns true | Disassembly (dump), return address logged from the hook |
| `0x140BE4EB0` (`bool find_mode(list, DXGI_MODE_DESC* inout, format, float hz, UINT w, UINT h)`): reads a cached mode list through an engine object (`[list+8]`, vtable +0x20, at most 100 entries, into a static buffer), returns true when w/h/format match (copies the entry). Called every frame from `0x140C0B2B7`, also from `0x140BF20E4` | Disassembly; prologue `48 89 6C 24 18 48 89 74 24 20 57 48 83 EC 40 80 79 18 00` |
| Forcing a bigger back buffer under the game (ResizeBuffers override only) gives a **black image**: the engine keeps its 1920x1080 targets and its final copy into the bigger buffer does nothing | Pipe shot: all pixels 0 |
| Offering a virtual 2160x2160 mode (FindClosestMatchingMode answer, fullscreen kept virtual = swap chain windowed) makes the **engine render at 2160x2160** | `back_buffer=2160x2160` in pipe status |
| …but it then re-applies the mode **every frame** (FindClosest + ResizeBuffers per frame, ~57 fps) because the virtual mode is not in the cached list. Adding the mode to `IDXGIOutput::GetDisplayModeList` did not help (the cache is filled through another path); hooking `0x140BE4EB0` to accept the virtual size **stopped the loop** (1 mode query per run) | `rendersize` pipe report: `mode_queries=1` |
| The remaining per-frame rebuild came from the renderer's window check `0x140BF4E60`: every frame it reads the client size (`GetClientRect`, IAT call at `0x140BF4F42`; `IsIconic` at `0x140BF4F30`) into window+0x60/0x64 and, in fullscreen, rebuilds the swap chain (`0x140C0A4C0` → `0x140C10F00` → `0x140C0AFC0`, ResizeBuffers at `0x140C0B03C`) when it differs from the mode. Reporting the virtual size from `GetClientRect` for the game window (mouse coordinates scaled to match) **fixed it: 120 fps (VR cap) in menu and level at 2160x2160**, 4 rebuilds at start-up only, menu clicks still land | IAT entries resolved live against user32; pipe status |
| On focus loss the engine leaves fullscreen: `SetFullscreenState 0` → `ResizeBuffers 1280x720` (this is the 720p drop from the headset tests) | Log 17:52:59 |

## Built (all behind `bfvr.ini` switches)

| Switch | Default | What | State |
| --- | --- | --- | --- |
| `render_size = WxH` | 0 (off) | Virtual fullscreen mode of that size (`src/hooks/swap_chain_size.cpp`): mode query answered with the size, exclusive fullscreen faked, borderless monitor-sized window, mode lookup hook `0x140BE4EB0`, same-size `ResizeBuffers` skipped; the game window reports the virtual client size (`src/hooks/game_window.cpp`) | Works at 2160x2160, 120 fps |
| `keep_focus = 1` | 1 | `src/hooks/game_window.cpp`: filters the window's deactivation messages, `GetForegroundWindow` answers with the game window, `ClipCursor`/`SetCursorPos` only act while BF4 really is in front | Works: focus losses hidden, no 720p drop |
| pipe `rendersize` | – | Report: render_size, buffer, game_fullscreen, resizes, same-size skips, mode queries (also appended to `status`) | Works |

## Results table (monitor)

| Lever | Setting | Back buffer | fps (menu / level) | Image | Notes |
| --- | --- | --- | --- | --- | --- |
| Baseline | profile, fullscreen | 1920x1080 | 200 / 200 (engine cap) | ok | earlier sessions: 330–400 uncapped |
| ResizeBuffers override only | 2160x2160 | 2160x2160 | 200 / 200 | **black** | dead end |
| Profile resolution 2160x2160 | – | 1920x1080 | 200 | ok | ignored by the game |
| Virtual mode | 2160x2160 | 2160x2160 | 57 / 57 | not checked | per-frame mode re-apply |
| + mode list entries | 2160x2160 | 2160x2160 | 58 / 55 | – | no change |
| + same-size resize skip | 2160x2160 | 2160x2160 | 32 / 19 | – | VR session was live (session_state 5) |
| + `0x140BE4EB0` hook | 2160x2160 | 2160x2160 → 1280x720 | 18.8 / 115 | – | loop gone; menu still slow; focus loss dropped to 720p |
| + `keep_focus`, virtual `GetClientRect` | 2160x2160 | 2160x2160 | **120 / 120** (VR cap) | full square image, HUD laid out for it | user played it: "helped a ton", look and performance |
| Render.ResolutionScale, AA, DOF, sharpening | – | – | – | – | not measured yet |

## Next steps

2. Measure `Render.ResolutionScale` 1.25/1.5 and `AntiAliasingPost 0` (user.cfg / console), per lever.
3. ~~Prototype the up-tilt of the render frustum (above).~~ Built 2026-11-04 as `view_crop_bottom` (see below).
4. HUD in the corners: try `GstRender.ScreenSafeAreaWidth/Height` < 1 to pull the HUD into the visible area
   (coordinate with the `feature/hud` worktree / #3).
5. User-side fallback that needs no code: NVIDIA DSR/DLDSR (e.g. 2880x1620 or 3840x2160 as a real mode).

## Bottom crop with an up-tilted camera (`view_crop_bottom`, 2026-11-04)

The lens shows about +44° up but only ~30° down, while the rendered frustum is symmetric (about ±46°
at `view_crop_v 0.72`). `view_crop_bottom = d` (degrees, 0–30, default 0) keeps the top edge,
raises the bottom edge by d, and pitches each eye's camera up by d/2 about its own x axis
(`eye_math::crop_bottom`, `eye_math::pitched`, applied in `camera_hook.cpp` after physical stance).
The compositor gets the same tilted pose and the smaller symmetric FOV, so it reprojects exactly.

- Only the rendered views turn. The arms, gun and body are placed in the body frame from tracking-space
  positions (`arm_pose.cpp camera_frame`), the second eye is moved along the (unchanged) right axis,
  and the scope and the projected HUD use the rendered pose. So all of them stay where they were in the world.
- Monitor check (Release, fake head facing the gun, fake hand pitched −15°): the sight top at
  y = 455/720 with d = 0 is predicted at y = 543 with d = 14 by the projection, and measured at about 540.
  The wall corner and the gun keep their columns. 116.5 fps either way.
- Gain at `view_crop_v 0.72`, d = 14: half angle 45.8° → 38.8°, so tan 1.028 → 0.804, which gives **28 % more
  lines per degree vertically**. Too much cut shows a black band at the bottom; headset row V1.
