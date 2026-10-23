# Reverse-engineering workspace

`dumps/` and `projects/` are git-ignored and must never be shared (see
[SAFETY.md](../docs/SAFETY.md)). Only notes and symbol lists in this folder are
meant to be kept in version control.

```
re/
├─ MAPPING.md  what to map next, why, how, and in which order (start here)
├─ notes/      findings and experiments per topic: camera.md, stereo.md, gamepad.md …
├─ symbols/    verified addresses/offsets per build, e.g. bf4-179547.md
├─ dumps/      (ignored) memory snapshots of the running game
└─ projects/   (ignored) Ghidra projects and REA snapshots
```

## Workflow

See [MAPPING.md](MAPPING.md) for the per-session method and the list of engine elements.
In short, for BF4 (build 179547, linked 2017-02-20):

1. **Decrypted image only.** `.text`, `typeinfo`, `fieldinf` and `.data` are encrypted on disk;
   take a dump at the main menu (`tools\pipe.ps1 dump <path>`) and analyse that, never the file.
2. **Live first.** Find writers with `watch`, follow pointers with `peek`, then read the function
   with `tools/disasm.py`. Use REA/Ghidra for cross-references across larger subsystems.
3. **Frame captures.** RenderDoc shows the passes and constant buffers holding view/projection
   matrices; follow them back to code.
4. **Record everything.** Verified results go into `symbols/`, reasoning and experiments into
   `notes/`, each with how it was verified and the prologue bytes for functions.

## Asking Claude with REA

Example prompts, once REA and Ghidra are set up:

- "Using the Ghidra project of the bf4 dump, list the callers of `0x14083A710` and
  explain what each passes as the view-params source."
- "List the Frostbite types in the typeinfo section whose names contain Camera
  or View, with their field offsets."
