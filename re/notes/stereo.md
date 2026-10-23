# Stereo rendering in BF4 (Frostbite 3)

Status: **closed 2026-10-23: BF4 has no game-side stereo path; it relied on driver 3D Vision. Next: alternate-eye rendering.**

## Experiment results (2026-10-22, automated)

| Test | Result |
| --- | --- |
| Console `list` | Stereo variables exposed: `RenderDevice.StereoSeparationScale`, `RenderDevice.StereoConvergenceScale`, `RenderDevice.StereoSoldierZoomConvergenceScale` (all read `1`). There is no console `StereoEnable`; `DxDisplay.*`, `GstRender.*` and `RenderDevice.StereoEnable` are unknown commands |
| Other notable console vars | `RenderDevice.Dx11Enable` (true), `Dx11Dot1Enable`, `RenderAheadLimit`, `TripleBufferingEnable`, `GameTime.MaxVariableFps` (default cap 200), `Render.ResolutionScale`, `WorldRender.*`, `PostProcess.*`, `Screenshot.*` |
| Profile `GstRender.Stereoscopy 1` | Accepted and kept by the game on exit, but **no change**: same checkpoint (Baku "Reach the Safe House"), uncapped 330–390 fps with stereo on vs 340–400 fps off, identical single view, no eye offset, back buffer unchanged at 1920×1080 R8G8B8A8 |
| Earlier "half fps" reading (~170 vs ~370) | Not caused by stereo: the two runs showed different scenes (outdoor firefight vs indoor) |

### Why: the game has no stereo path of its own (probes, 2026-10-22/23)

| Probe (`bfvr.ini`) | Stereoscopy 0 | Stereoscopy 1 |
| --- | --- | --- |
| `nvapi_probe = 1`: every `nvapi_QueryInterface` ID through main menu and a level load | 11 IDs: `NvAPI_Initialize`, interface/driver version, GPU enumeration, `D3D_GetCurrentSLIState`, `DISP_GetGDIPrimaryDisplayId`, `Mosaic_GetDisplayViewportsByResolution`, one undocumented, and `NvAPI_Diag_ReportCallStart/Return` (not provided by the current driver) | **identical**; no `NvAPI_Stereo_*` call at all |
| `dxgi_probe = 1`: DXGI factory calls | – | only legacy `IDXGIFactory::CreateSwapChain` 1280×720, R8G8B8A8, 3 buffers, DISCARD; no `IsWindowedStereoEnabled`, no `RegisterStereoStatusWindow`, no stereo swap chain |

Logs: `re/notes/nvapi-probe-stereo{0,1}.log`, `re/notes/dxgi-probe-stereo1.log`; names via
`tools/nvapi-names.ps1` (IDs from NVIDIA's `re/ref/nvapi_interface.h`, MIT; the `Diag` names
are from RenderDoc's and OptiScaler's NVAPI tables).

Conclusion: BF4's stereo relied on **NVIDIA 3D Vision Automatic**, where the driver itself
duplicated every draw with a per-eye vertex offset and the game never called a stereo API. That
driver feature no longer exists (last driver: Release 418, April 2019; an RTX 5070 needs drivers
years newer), and there is no game-side hook to fake. **Real 3D Vision cannot be set up, and
faking NVAPI stereo would change nothing.**

## Options for stage 2

| Option | What it is | Effort | Fit for this PC |
| --- | --- | --- | --- |
| **A. Alternate-eye rendering (AER)** | Move the game camera half the eye distance left on even frames, right on odd frames; each eye's latest image goes into its OpenXR view. The approach used by Luke Ross's R.E.A.L. VR mods | Needs one reverse-engineered hook: the camera/view transform | **Recommended.** BF4 runs at 330–400 fps uncapped here, so ~180 fps (90 per eye) is easily reachable. Known artefact: fast motion differs slightly between eyes |
| B. Geometry duplication (what 3D Vision Automatic did) | Intercept every draw, render it twice with a per-eye clip-space shift in the vertex shader (like 3Dmigoto / VorpX "Geometry 3D") | Very high: shader patching across the whole renderer, post-effects break | Poor |
| C. True per-eye rendering | Run the scene render twice per frame with per-eye view/projection (Overlord's approach) | Highest: deep renderer reverse engineering | Best quality, much later |

Next step for A: locate the camera in the running process (Ghidra + REA on the decrypted image,
plus Frosty's `BF4SDK.dll` for class layouts), hook it, and offset it per frame.

## Evidence (2026-10-22)

| Source | Finding |
| --- | --- |
| `Documents\Battlefield 4\settings\PROFSAVE_profile` | `GstRender.Stereoscopy 0`, `GstRender.StereoConvergence 1.000000` — a stereo option exists but is hidden in the video menu |
| same file | `GstRender.MantleEnabled 0` — DX11 renderer is active (the API option is hidden on NVIDIA) |
| `bf4.exe` `.rdata` strings (plaintext) | `StereoEnable`, `StereoMode`, `StereoDepth`, `StereoSeparationScale`, `StereoConvergenceScale`, `StereoSoldierZoomConvergenceScale`, `StereoSplitScreenEnable`, `StereoCrosshair{DampingFactor,MaxHitDepth,Radius}`, `LayerStereoPostProcess`, `ShaderProgram_Twinkle_StereoPostProcess` |
| Frosty `Profiles\BF4SDK.dll` (reflection-only load) | Owning classes below |

| Class | Stereo / FOV fields |
| --- | --- |
| `DxDisplaySettings` | `StereoEnable`, `StereoDepth`, `StereoSeparationScale`, `StereoConvergenceScale`, `StereoSoldierZoomConvergenceScale`, `StereoSplitScreenEnable` |
| `GameRenderSettings` | `FovMultiplier`, `StereoCrosshairDampingFactor`, `StereoCrosshairMaxHitDepth`, `StereoCrosshairRadius` |
| `CameraComponentData`, `FPSCameraData`, `CameraEntityData`, `RegularCameraViewData`, `ZoomLevelData` (`FieldOfViewSP`) | camera FOV sources (relevant for the camera module) |
| `TonemapComponentData`, `GlobalPostProcessSettings` | `Chromostereopsis*` — a colour effect, **not** stereo rendering |

## Open questions

1. Does `StereoEnable` render two views without NVIDIA 3D Vision? 3D Vision was removed from
   NVIDIA drivers in 2019 (R425+). If the engine gates on NVAPI stereo, the path may be dead on
   the RTX 5070 and we either satisfy the gate or drive the per-eye passes ourselves.
2. Console name of the settings group. Frostbite exposes settings as `<Group>.<Field>`; candidates
   to try in the console (`½` key) or `user.cfg`: `DxDisplay.StereoEnable 1`,
   `GstRender.Stereoscopy 1`. Record which ones the console accepts.
3. Where the per-eye view offset is applied (search for `StereoSeparationScale` readers once the
   decrypted image is available).

## Next experiment (scripted; needs ~5 minutes of the game without you)

1. Console probes with the output kept visible, then a back-buffer shot of the answer:
   `tools\game.ps1 console -KeepOpen -Text "DxDisplay.StereoEnable"`, then `tools\pipe.ps1 shot`.
   Repeat for `DxDisplay.StereoEnable 1`, `GstRender.Stereoscopy 1`, `RenderDevice.StereoEnable`.
2. Profile switch: back up `PROFSAVE_profile`, set `GstRender.Stereoscopy 1`, restart, then compare
   `tools\pipe.ps1 status` fps and a shot against the baseline. Scene rendered twice per frame should
   show as a large fps drop. Restore the backup afterwards.
3. If either shows a change, follow up with a RenderDoc capture to see both eye passes.
