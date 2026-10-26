# Task brief: a testing ladder (Overlord-style)

Written 2026-10-25. Read `CLAUDE.md` and `docs/SAFETY.md` first. Companion to
`docs/tasks/per-eye-rendering.md`: every step of that work should land with the test below it.

## Goal

Catch most regressions without the game or the headset, and make each headset test short and
specific. Today we have two offline targets (`bfvr_smoke_theater`, `bfvr_arm_ik_test`), the pipe
`status` counters and `docs/HEADSET-TESTS.md`. Overlord (`github.com/Aeka0/Overlord`, GPLv3: copy
the idea, not the code) has ~300 test files and runs a subset in CI on every push. Its rule is the
one to adopt: **CPU, mock-runtime and WARP results do not prove headset acceptance**, so each layer
says what it proves and what it does not.

## The ladder (cheapest first)

| Layer | Runs where | Proves | Overlord example | Ours: have / add |
| --- | --- | --- | --- | --- |
| 1. Pure-logic unit tests | Any PC, CI | Maths and state machines with no engine, D3D or XR dependency | `camera_rig`, `spatial_math`, controller pose pipeline, per-weapon profiles | Have: `arm_ik_test`. Add: eye-pose maths (head pose + IPD → two view matrices and asymmetric projections, parallel eyes), view-family record (both eyes share one prediction), pair state machine (complete pairs only, retire before reuse), MP guard level-name rules, config parsing |
| 2. Binding / signature checks | Any PC with the decrypted dump (never in CI: no game files in git) | Every hooked address still has the expected prologue bytes | `vr-native-binding-tests`, `tools/native_bindings/cli.py check` | Add: `tools/verify_bindings.py` reading `re/symbols/bf4-179547.md` addresses and expected bytes, checked against `re/dumps/bf4-179547-mem.exe`. Same check the mod already does at startup, run before launching the game |
| 3. Mock runtime / no-loader smoke | Any PC, CI | Our OpenXR code handles "no runtime", lost focus, session restart, partial pairs | `vr-runtime-no-loader-smoke`, `vr-runtime-mock-smoke`, OpenVR lifecycle tests | Have: `smoke_theater` (needs a runtime). Add: a no-runtime run that must fail cleanly, and a fake swap-chain path for the pair ring |
| 4. D3D11 WARP probes | Any PC, CI (WARP is the software rasteriser) | Per-eye resource isolation: binding save/restore, eye targets, history per eye, no cross-eye leaks | `vr-d3d11-eye-resource-isolation-probe`, `vr-engine-ssr-history-probe` | Add: a WARP test for our per-eye save/restore scope: render a known pattern into "eye 0" and "eye 1" targets with swapped constant buffers, read both back, check they differ by exactly the expected offset and that the original bindings are restored |
| 5. In-game counters (no headset) | This PC, unattended via `tools\dev-run.ps1 -Resume` | Engine-side behaviour: both eyes rendered per frame, no hangs, fps | `vr_status` counters, bounded capture files, crash/stall evidence | Have: pipe `status`, `shot`, `threads`, `frametrace`. Add to `status`: pairs rendered, pairs rejected (+ reason), per-eye frame id, eye-2 GPU time. Add `tools/soak.ps1`: resume the campaign, sample `status` every second for N minutes, fail on a stall, a rejected pair rate above 0 or fps below a floor |
| 6. Headset acceptance | User, batched | What only eyes can judge: scale, depth, double images, comfort | `docs/release-validation.md` checklist, `vr-readability-regression-checklist.md` | Have: `docs/HEADSET-TESTS.md`. Keep each request to a short "look at X, expect Y" list, run only after layers 1–5 pass |

## What to build first

1. `tests/` layout and a CTest hook: `enable_testing()` + `add_test()` for every offline target, so
   `ctest --preset bf4-debug` runs them; both presets build them warning-free.
2. Layer 1 for the per-eye maths (`src/stereo/` helpers kept free of engine headers, the same way
   `body/arm_ik` is). This is the piece per-eye rendering needs first.
3. Layer 5 counters and `tools/soak.ps1`, because every per-eye experiment so far ended in a hang
   or freeze that a soak run would have caught and classified automatically (`threads` on stall).
4. Layer 2 binding check.
5. Layer 4 WARP probe once the per-eye save/restore scope exists.
6. CI (`feature/ci`, #18): add `ctest` for layers 1, 3 and 4. Layers 2, 5 and 6 stay local.

## Status

| Item | State (2026-10-25) |
| --- | --- |
| CTest | `ctest --preset bf4-debug` / `bf3-debug` runs `arm_ik` and `eye_math` |
| Layer 1 per-eye maths | `src/stereo/eye_math.*` (pose composition, rendered frustum, eye midpoint, eye angle, complete-pairs rule), `tests/eye_math_test.cpp`; the camera hook uses it |
| Layer 5 counters | status line `eyepass eye_frames= eye_pairs= eye_rejected= eye_abandoned= eye_ms=` |
| Layer 5 soak | `tools/soak.ps1`; first real use caught the `worldpass 2` crash and saved its thread sample automatically |
| Crash evidence | research builds log fatal exceptions with registers and stack to `bfvr.log` (`src/control/crash_log.cpp`) |

## Rules

- A test says in its first comment what it proves and what it does not.
- No game files, dumps or screenshots in tests or fixtures (SAFETY.md); layer 2 reads the dump from
  its ignored local path and skips when it is missing.
- Engine-facing code stays thin: logic that can be tested offline moves into engine-free headers.

## Done when

`ctest` runs layers 1, 3 and 4 locally and in CI, `tools/soak.ps1` reports pass/fail for a
10-minute campaign run, `tools/verify_bindings.py` passes against the current dump, and the
per-eye brief's steps each name the test that covers them.
