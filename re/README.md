# Reverse-engineering workspace

`dumps/` and `projects/` are git-ignored and must never be shared (see
[SAFETY.md](../docs/SAFETY.md)). Only notes and symbol lists in this folder are
meant to be kept in version control.

```
re/
├─ notes/      findings per topic: camera.md, stereo.md, input.md …
├─ symbols/    address → name lists you confirm (also used by src/hooks)
├─ dumps/      (ignored) memory snapshots of the running game
└─ projects/   (ignored) Ghidra projects
```

## Workflow

1. **Static data first.** The `typeinfo`/`fieldinf` sections are not encrypted.
   Map Frostbite class names and field offsets from them, especially camera,
   view and render-settings classes.
2. **Live code.** Start the campaign, attach **x32dbg** read-only, and analyze the
   decrypted code in memory. Let Ghidra and REA work on that view, never on the
   encrypted file.
3. **Frame captures.** Use **RenderDoc** to capture a frame, find the passes and
   constant buffers holding view/projection matrices, then follow them back to
   code.
4. **Record everything.** One note per topic in `notes/`, stating how each claim
   was verified: addresses, byte signatures, game version (linker timestamp
   2013-02-14). Overlord's docs are a good model.

## Asking Claude with REA

Example prompts, once REA and Ghidra are set up:

- "Using the Ghidra project for bf3, find the functions that read `WorldRender`
  stereo settings and explain how the eye index is selected."
- "List the Frostbite types in the typeinfo section whose names contain Camera
  or View, with their field offsets."
