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

**Stage 0 (2026-10-26, monitor):** `re/notes/weapon.md` "Weapon slots and switching". Pad Y toggles the
two guns, the right stick click is the quick knife stab, nothing on the pad selects a slot. Keys typed
into the game's Raw Input keyboard (new `hooks::press_game_key`, pipe `key`) work without focus:
1 = slot 0 (primary), 2 = slot 1, 5 = grenade, 6 = knife (stays), F = stab; 3/4 = gadgets (none in
this kit). Switches land within 0.25 s. The visor's input was not found.

**Stage 1:** `src/weapon/holsters.*` (pure, tests in `tests/reload_test.cpp`): five zones in a body frame
that follows the head's yaw past 0.6 rad of slack; mirrored for left-handed; a grip press in a zone
draws, a grip carried into a zone does not.

**Stage 2:** `src/weapon/weapon_swap.*`, live setting `holsters` (default on; needs head and hand
tracking). Draw = the slot key (80 ms), checked after 0.6 s against the game's slot, one retry, then
"no switch" (empty holster or refused); the weapon already in hand is skipped. RB is kept from the
game while the gun hand is in a zone; while a grenade is in the off hand the gun-hand grip is the pin's.
Research pipe `holster [name]`.

**Monitor-verified:** pipe switches primary / secondary / knife, the empty gadget holster gives up
cleanly; with `fakehead`, synthetic gun-hand targets and pad RB the shoulder, chest and hip zones drew
the SCAR-H, the knife and the AK-12; a grip at the aiming position did nothing.

**Squad order (same day):** `src/weapon/hand_signal.*` (pure, tested): the off hand raised at least
5 cm above the eyes, 35 cm or more ahead along the head's yaw, within 40 cm sideways, plus a grip
squeeze = the campaign's "engage" (key Q through Raw Input, 1 s cooldown). Live setting `squad_signal`
(default on); LB is kept from the game while the hand is in that pose; a grenade in the off hand is
never an order. Monitor: the support-hand position gave no order, the raised pose did (Q read by the
game). The engage effect itself needs enemies in view (headset test 17).

**For the headset:** zone positions and radii, the body-yaw slack, whether RB should keep its game action
outside the zones (not identified yet). **Later:** haptics, zone hints, knife swing = melee, the visor.
