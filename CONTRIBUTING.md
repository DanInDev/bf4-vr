# Contributing

## Branches

| Branch | What it holds |
| --- | --- |
| `dev` | The working state. Every change branches from `dev` and merges back into it. |
| `main` | Released versions only. It moves only at a release, as a fast-forward to `dev`, and each release is a tag `vx.y.z` on it. |
| `<area>/<short-name>` | One task, branched from `dev` (e.g. `weapon/foregrip-hold`). Deleted once merged. |

Open pull requests against **`dev`**. A pull request against `main` is retargeted or closed.

## Before a pull request

- Read [docs/SAFETY.md](docs/SAFETY.md). Single-player campaign only. Never commit dumps, screenshots, game files or
  profile data; the commit hook and `tools\gate.ps1` refuse them.
- `tools\gate.ps1` is green: both presets (`bf4`, `bf3`) build without warnings, all unit tests pass, and the Python
  tools compile.
- Say what you checked in the game and what only a headset can judge. A change that needs the headset gets a row in
  [docs/HEADSET-TESTS.md](docs/HEADSET-TESTS.md).
- Conventions: C++20, MSVC, tabs, `bfvr::` namespaces. Every new feature gets a live on/off setting, a row on the in-VR
  settings panel and a line in `tools/bfvr.ini.example`. New engine-free logic gets a unit test under `tests/`.
  Every engine address is verified (bytes or prologue) before it is hooked, with the evidence in `re/notes/`.

## Releases

1. On `dev`: bump `project(bfvr VERSION x.y.z)` in `CMakeLists.txt` and commit.
2. `git switch main`, `git merge --ff-only dev`, `git tag vx.y.z`, `git push origin main vx.y.z`.
3. The `release` workflow checks that the tag is on `main` and matches the version, then builds and publishes the zip.
   The launcher's **Updates?** button finds it.
