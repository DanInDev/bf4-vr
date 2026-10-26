# Verified findings

Facts checked on this PC on 2026-10-22. Each line says how it was verified, so
later work can tell observation from assumption.

## bf3.exe (`E:\EA\Battlefield 3\bf3.exe`)

| Fact | Evidence |
| --- | --- |
| **32-bit (x86)** executable, 33.2 MB | PE machine `0x14C` |
| Large-address-aware (up to 4 GB on 64-bit Windows) | PE characteristics flag `0x20` |
| **No ASLR** — fixed image base, addresses are stable between runs | DllCharacteristics `0x8100` (no `DYNAMIC_BASE`) |
| Built 2013-02-14 | PE linker timestamp |
| Signed by Electronic Arts | Authenticode |
| **`.text` and `ctr` sections are encrypted on disk** (DRM wrapper) | Shannon entropy 8.00 on both; static analysis of the file shows no real code |
| **Frostbite reflection data is plaintext**: `typeinfo` (342 KB) and `fieldinf` (204 KB) sections | Entropy 3.37 / 4.57 |
| Loads `d3d11`, `dxgi`, `dinput8`, `xinput9_1_0`, `nvapi`, `d3dx11_43`, `d3dcompiler_43`, `msvcr90/msvcp90`, `tbbmalloc` | DLL name strings in the image |
| Contains `Stereo` (13) and `WorldRender` (12) strings | Raw string scan; NVAPI stereo is resolved by ID, so `NvAPI_Stereo*` names are absent |
| PunkBuster client DLLs referenced (`pbcl*.dll`, `pbag.dll`) | String scan |

Implications:

- Analyze the **running, decrypted process**, not the file on disk (see [re/README.md](../re/README.md)).
- The `typeinfo`/`fieldinf` sections are the fastest route to class and field layouts.
- `dinput8.dll` is the conventional proxy point; `dxgi.dll` is the alternative if needed.
- The built-in stereo 3D path (2011 patch) is the main shortcut for per-eye rendering.

## bf4.exe (`E:\EA\Battlefield 4\bf4.exe`), checked 2026-10-22

| Fact | Evidence |
| --- | --- |
| **64-bit (x64)**, 40.4 MB; a separate `bf4_x86.exe` also ships | PE machine `0x8664` |
| **No ASLR** — fixed image base | DllCharacteristics `0x8120` |
| Final build 179547, linked 2017-02-20 (PE `TimeDateStamp` `0x58AB1CC0`, checked by the launcher) | `version.json`, PE timestamp |
| `.text`, `ctr`, `.data` **and `typeinfo`/`fieldinf` encrypted on disk** | Entropy 8.00 on all five; unlike BF3, no class data is readable from the file |
| `.ooa` section: EA/Origin Online Activation DRM | Section name |
| Started through `BFLauncher.exe`, which picks x64 or x86 | `__Installer\installerdata.xml` `<runtime>` |
| Loads `d3d11`, `dxgi`, `dinput8`, `xinput9_1_0`, `nvapi64`, `mantle64` | DLL name strings |
| `Stereo` ×32, `Mantle` ×34 strings | String scan; stereo path likely still in engine, unconfirmed |
| Anti-cheat: PunkBuster (`pbsvc.exe`, optional in installer); **no EA Javelin files** | Install folder scan |
| Campaign data present: `Data\Win32\Levels\SP` | Folder listing |
| Frosty Tool Suite lists BF4 as fully supported (assets, EBX, SDK profile) | Frosty docs |

| Video menu has no API option on this PC; `GstRender.MantleEnabled 0` → DX11 active | `Documents\Battlefield 4\settings\PROFSAVE_profile` |
| Hidden stereo option: `GstRender.Stereoscopy 0`, `GstRender.StereoConvergence 1.0`; engine class `DxDisplaySettings` has `StereoEnable`/`StereoDepth`/`StereoSeparationScale` | Profile file, `.rdata` strings, Frosty `BF4SDK.dll` — details in [re/notes/stereo.md](../re/notes/stereo.md) |

Implications: x64 removes the 4 GB ceiling and matches Overlord's x64 patterns;
all analysis, including type layouts, must come from the running process (or
Frosty's BF4 SDK profile); force the DX11 renderer, not Mantle.

## This PC

| Item | State |
| --- | --- |
| CPU / RAM / GPU | Ryzen 7 9800X3D, 31 GB, RTX 5070 |
| Active OpenXR runtime (64-bit) | Meta Horizon `oculus_openxr_64.json` |
| Active OpenXR runtime (**32-bit**) | Meta Horizon `oculus_openxr_32.json` — a 32-bit app like BF3 can use OpenXR through Meta Horizon Link |
| SteamVR | Installed; ships a 32-bit OpenXR runtime (`steamxr_win32.json`), not registered as active |
| EA app | Installed; BF3 freshly installed 2026-10-22 |
| PunkBuster services `PnkBstrA`/`PnkBstrB` | **Removed 2026-10-22** (leftover `PnkBstrB.ex0`/`.xtr` in SysWOW64 are inert). Previously running, Automatic. Official uninstaller: `"E:\EA\Battlefield 3\pbsvc.exe" -u` (needs admin). The BF3 installer marks PunkBuster optional: without it only ranked MP servers are lost |
| EA AntiCheat (Javelin) service | Registered **on demand**: present earlier today, absent later. Assume it is active whenever an EA game runs |
| EasyAntiCheat (`EasyAntiCheat_EOS`) | Installed by another game; same rules as Javelin |
| Claude desktop app | Runs in an MSIX container; its shells cannot elevate or do user-wide `npm -g` installs |
| Battlefield 6 | Installed via Steam — uses kernel-level EA AntiCheat; see [SAFETY.md](SAFETY.md) |

## In-game results (2026-10-22)

| Topic | Result |
| --- | --- |
| Theater mode | Works in BF4 via Steam Link (user confirmed in the headset); runtime chosen automatically |
| Controllers | Quest Touch → Xbox pad via XInput hook confirmed by the user in BF4 |
| Built-in stereo | `GstRender.Stereoscopy 1` has no effect (single view, same fps). BF4 makes no NVAPI stereo or DXGI stereo calls: its 3D relied on driver-side 3D Vision Automatic, which cannot run on an RTX 5070. See [re/notes/stereo.md](../re/notes/stereo.md) |
| Frame cap | BF4 caps at 200 fps (`GameTime.MaxVariableFps`); unfocused window drops to 30 fps |
| Camera | `[0x142672378]` GameRenderer → `+0x60` RenderView → `+0x40` camera world transform (verified live); details in [re/notes/camera.md](../re/notes/camera.md) |
| Decrypted image | The pipe `dump` at the main menu gives a fully readable image (`.text` entropy 6.49, `typeinfo` 3.25; 0 unreadable pages) |
| Startup config | `user.cfg` in the install folder runs console commands at launch; command-line settings are ignored |
| Automation | Console, clicks, campaign resume and back-buffer screenshots all scriptable (`game.ps1`, `console-probe.ps1`, `pipe.ps1`) |

## Smoke test results (2026-10-22, `bfvr_smoke_theater`)

| Build | Result |
| --- | --- |
| x64 | Present hook installed and called ~144×/s. OpenXR runtime "Oculus 1.208.0", system found, session created on the RTX 5070 (LUID matches). Session stayed `IDLE` because the headset was not active, so no frames were shown yet |
| x86 | Hook works; **Meta's 32-bit runtime crashes** (access violation in `RuntimeIPCServiceClient_32.dll`) inside `xrCreateSession`. Not our code; BF3 would need SteamVR's 32-bit runtime |

## Open questions

- Does the campaign still launch through Battlelog/`BF3WebHelper.exe`, or directly from the EA app?
- Is the stereo 3D path gated on NVIDIA 3D Vision drivers (removed from modern drivers), or can it be forced via engine settings?
- Does the DRM wrapper include anti-debugging that blocks attaching a debugger to the campaign?
