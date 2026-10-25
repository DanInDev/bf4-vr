# Headset test log

Quest 3 via Steam Link, SteamVR 2.18.4 OpenXR, RTX 5070. Newest first.

## 2026-10-25 afternoon — combined build (main + feature/virtual-pad)

Reported per-eye FOV (logged): left eye left −54°, right 40°, up 44°, down −55°; right eye mirrored.
The user's visible area in the Quest (SteamVR "VR View" with the visible region marked) is roughly
40° to the sides, 44° up and ~30° down: about two thirds of the rendered image per axis.

| Item | Result |
| --- | --- |
| Gun stays on the aim while the head looks around (morning) | ✅ all five checks passed |
| Sharpness | ❌ blurry; low resolution; very blurry during fast head turns (one or both eyes) |
| Cause 1: rendered FOV wider than visible | measured above → **view crop** added (pipe `viewcrop <h> <v>`, config `view_crop_h/v`); estimate h ≈ 0.61, v ≈ 0.68; not tuned yet |
| Cause 2: 720p drops | exclusive fullscreen falls back to a 1280×720 window on focus loss (Claude, SteamVR overlay) → BF4 switched to windowed (`GstRender.FullscreenEnabled 0`, profile backed up in `re/`); windowed starts at 1280×720 in the menu but goes to 1920×1080 in the level |
| Cause 3: fast head motion | alternate-eye rendering (60 Hz per eye, #6) and Steam Link encoding |
| Gun size | feels too large/close ("head further back or higher FOV") → `gun_scale` sweep pending |
| HUD | unreadable: the bottom HUD lies outside the visible area (#3: needs its own layer) |
| 7 fps while the headset is off / dashboard open | fixed (hidden frames completed on the waiter thread) — but see freeze below |
| SteamVR kept restarting while connecting Steam Link | caused by the mod (instance retries launch SteamVR) → fixed: never launch SteamVR, wait for it |
| Entering via Link Dash (Oculus) | menu upside down, controllers dead → use Steam Link only |
| Controllers without the Cepter | ✅ game reads the virtual pad continuously after the "present while idle" fix; in-game feel not tested yet |
| **Main menu glued to the view** | ❌ the menu was drawn head-tracked over the whole view (only the middle third readable) instead of the theater screen: menu-camera detection missed → fix: front end always theater |
| **Freeze after reconnecting Steam Link during level load** | ❌ SteamVR showed "BF-VR Loading", desktop black; BF4 stopped presenting (presents frozen, session state 3) until closed → suspected concurrent SteamVR calls from the waiter (hidden frames) and Present (swap chain/copy); fix: serialise XR calls, plus a stage readout in `status` |

Test procedure notes: stay in BF4 (don't click out to Claude or the desktop, don't reconnect
Steam Link mid-run); connect Steam Link first, then start BF4.
