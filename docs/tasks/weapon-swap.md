# Task brief: weapon swapping by body holsters

Written 2026-10-26 (`feature/weapon-swap`). Read `CLAUDE.md`, `docs/SAFETY.md` (binding) and this file
first. Issue: #14 (weapon handling). Builds on `src/weapon/` (reload, grenades) and
`re/notes/weapon.md`.

## Goal

Overlord / Alyx / H3VR style: reach to a spot on the body with the gun hand and squeeze the grip to
draw the weapon kept there. The current weapon is holstered by the switch.

| Holster | Where (body yaw frame, not the head) | Weapon kind |
| --- | --- | --- |
| Primary | over the gun-side shoulder, behind the head | primary |
| Secondary | gun-side hip | sidearm |
| Knife | chest, off-hand side | knife |
| Gadget 1 / 2 | belt, off-hand side, front / back | gadgets |

Mirrored for `dominant_hand = left`. Grip claimed in the zones (as reload/grenade gestures).

## What exists

- Slot array and current slot (client weapons component, `re/notes/aim.md`), weapon names
  (`ClientSoldierWeapon+0x4850 -> +0x20`). Checkpoint "Get Grenade Launcher": 0 SCAR-H, 1 AK-12,
  6 M67, 7 knife, 8 visor; 2-5 empty.
- Pad: Y = next weapon; LB = grenade (now the physical grenade); D-pad up = accessory.
- Keyboard: the game reads it through **Raw Input** (`GetRawInputData`, `RegisterRawInputDevices` in
  the image; no DirectInput keyboard). DirectInput8 is only used for game controllers.

## Making the game switch (in order of preference)

1. **Slot keys through Raw Input**: post `WM_INPUT` with a marker handle and answer it in a hooked
   `GetRawInputData` with a keyboard make/break of the slot's key (1-5, knife key).
2. **Pad cycling with a check**: press the pad's switch inputs and read the slot until it matches.
3. **The game's switch function** (RE), only if 1 and 2 fall short.

Every switch is checked against the slot the game reports; one retry, give up after ~1 s (sprinting,
vehicles, reload).

## Stages

0. **Research on the monitor**: which pad buttons and keys select which slots in the campaign; switch
   time; whether a key injected through Raw Input is taken while the pad is active; slot kinds by name.
1. **Holster module** (pure, `src/weapon/holsters.*`, offline tests): zones in the body frame, claim,
   target choice.
2. **Game side**: slot selection with the check, settings (`holsters`, zone offsets), research pipe
   `holster <primary|secondary|knife|gadget1|gadget2>`.
3. **Later**: haptics on entering a zone, an icon at the zone, knife swing = melee, a wrist menu.

## Results

(filled in as the stages land)
