---
name: Task for an unattended run
about: A change the agent can build and verify without the owner (docs/UNATTENDED.md)
labels: agent-ready
---

## Goal
<!-- One or two sentences: what is different for the player when this is done. -->

## Monitor check
<!-- Required. A command and what it must show, runnable without the headset. Examples:
     - unit test `bfvr_reload_test` has a case for X and passes
     - `tools\pipe.ps1 fakehead 0.064`, `fakehand 0 -20`, then `status` shows `twohand: gripped`
     - `tools\pipe.ps1 shot` shows the grenade at the left hip (describe what must be visible)
     Without this the issue is not agent-ready. -->

## Headset check
<!-- One line the owner can do in a minute: what to do, what should be seen. -->

## Out of scope
<!-- What must not change. -->

## Notes
<!-- Where to start: files, notes in re/notes, related issues. -->
