# Task: every crate gun handles like the real one

Status: **design and work order (2026-11-15)**; picker (0b) and the firing-data catalog (0c) built 2026-11-16/17. Tracking: #64 (per-gun reload mechanics),
plus the issues listed under "Work order".

Goal: any of the 51 guns in the campaign weapon crate can be picked, and each one loads, charges, cycles
and fires the way the real weapon does, with the controls where the drawn model has them. Today only
two reload kinds exist (`weapon/profiles.cpp`: AK and AR), with measured handle points for the AK-12 and
SCAR-H only.

## What is established

- **Unlocks:** with `SyncedBFSettings.AllUnlocksUnlocked` (+0x54, three instances) and the crate menu's
  `UIWeaponSelectorGridWidgetData.UnlockEverything` (+0xA0) poked to 1, every crate gun became
  selectable after the crate was closed and opened again (owner, 2026-11-15). **Built (step 0a):** the
  setting `unlock_all_weapons` holds `AllUnlocksUnlocked` alone at 1 while a campaign level runs; on a
  fresh start the owner found every gun selectable, so the crate's own flag is not needed.
- **The crate offers 51 guns** in eight rows (`re/notes/weapon.md` "The weapon crate and unlocks").
  Every BF4 gun and gadget (167 unlock assets) is loaded in a level, the crate shows a fixed subset.
- **Per-gun data without spawning:** each unlock asset's `StoredWeaponData` (+0x78) has the magazine
  capacity (with the chambered round) and the spare magazines. `tools/weapon_catalog_probe.py` reads it.
- **Per-gun data once spawned:** the firing data streams in with the gun: `ReloadType` (single round,
  magazine, magazine with a shorter tactical reload), `FireLogicType` (single, single with bolt action,
  auto, burst) and the other modes, rate of fire, reload times, bolt-action times, zoom levels.
  `weapon_catalog_probe.py --held` reads the guns in the slots. Verified: AK-12 `magazine+short`, auto +
  single/burst, 650 rpm, 31; SCAR-H `magazine+short`, auto + single, 620 rpm, 21.
- **Per-gun points from the rig:** every gun hangs its bones under `Wep_Root`: `Wep_Mag`, `Wep_Slide`,
  `Wep_Belt`, `Wep_Bipod`, `IK_Joint_LeftHand`, `IK_Joint_RightHand` (`re/notes/skeleton.md`). The
  magwell, slide/bolt, belt and support-hand spot can come from each gun's own bones. The charging
  handle needs the game's empty-reload animation (as for the AK-12) when the handle is not the slide bone.
- **Bug found:** `ak_family` in `weapon/profiles.cpp` lists `U_Saiga12`, but the crate's Saiga is
  `U_SAIGA_20K`, so it gets the AR reload (a button it does not have).

## How to describe a gun: five properties, not one class

The real guns don't fall into clean classes (a FAMAS is a bullpup with an AR-like magazine and a top
handle; a Saiga is a shotgun with an AK action). Each gun gets a profile row with five independent
properties. The reload state machine reads the first three, the zones read the fourth, and the trigger
reads the fifth.

| Property | Values | What it changes in VR |
| --- | --- | --- |
| **Feed** | `box` straight in, `rock` (hook the front, rock back: AK, M14, FAL, SVD), `top` (P90), `drum`, `belt` (lid, box, belt), `tube` (shell by shell), `cylinder` (revolver) | the magazine/round steps and their zones |
| **Release** | `button`, `paddle`, `none` (the hand rocks it out), `lever` (belt lid, revolver latch) | what frees the magazine and with which hand |
| **Hold-open** | yes: the last shot locks the bolt back, a bolt catch or a rack closes it. No: the bolt rides forward on an empty chamber, so racking is needed | whether the bolt-catch press chambers |
| **Layout** | magwell `ahead` of the grip, `grip` (in the pistol grip), `behind` (bullpup), `top`; handle `rear-T`, `right`, `left`, `top`, `front-left` (HK), `both`; open or closed bolt | zone positions; which hand charges |
| **Cycle** | `none` (semi/auto), `bolt` (lift, back, forward, down after every shot), `pump` (back and forward after every shot), `double-action` (revolver) | whether the next shot needs a hand action first |

The game's own data decides `cycle` and part of `feed`: `FireLogicType = single with bolt action` marks a
bolt or pump gun, and `ReloadType = single bullet` marks a tube or a round-by-round reload. The rest
comes from the real gun, checked against the drawn model, because the player sees the model.

## The 51 guns

Real-world facts; `?` = check against the model in the catalog sweep (phase 1). "Game" is the crate
data: capacity counting the chambered round, times the spare magazines.

### Sidearm (1)

| Gun | Real action | Feed / release | Hold-open | Layout | Game |
| --- | --- | --- | --- | --- | --- |
| M412 REX | Double-action revolver, **top-break** | cylinder, latch, extractor throws all six | n/a | break forward | 6x4 |

### PDW (8)

| Gun | Real action | Feed / release | Hold-open | Layout | Game |
| --- | --- | --- | --- | --- | --- |
| CBJ-MS | Closed-bolt PDW | box, button? | ? | magwell ahead of the grip, handle ? | 51x3 |
| CZ-3A1 (`U_Scorpion`) | Blowback SMG | box, paddle + buttons | yes, bolt catch | handle front-left (non-reciprocating), ahead | 31x5 |
| JS2 | Blowback SMG | box, button? | ? | handle ? | 31x5 |
| MX4 | Beretta Mx4 Storm | box **through the pistol grip**, button | ? | handle side ? | 31x5 |
| P90 | Blowback bullpup | **top** horizontal magazine, release behind it | no | handles both sides front | 51x4 |
| PDW-R (`U_MagpulPDR`) | AR-pattern bullpup | box (STANAG), button | yes | magwell behind the grip, handle front ? | 31x5 |
| PP-2000 | Blowback | box **in the pistol grip** | ? | handle top | 45x4 |
| UMP-45 | Blowback, HK controls | box, paddle behind the magwell | **no** | handle front-left (HK) | 26x5 |

### Shotgun (8)

| Gun | Real action | Feed / release | Hold-open | Layout | Game |
| --- | --- | --- | --- | --- | --- |
| 870 MCS | Pump | tube, shell by shell into the loading port underneath | (pump stays back on empty) | fore-end pump | 8x5 |
| Hawk 12G | Pump ? (game data decides) | tube ? | | | 6x7 |
| M1014 | Semi-auto (Benelli M4) | tube, shell by shell | yes, bolt release button | bolt handle right | 8x5 |
| QBS-09 | Pump or semi ? (game data decides) | tube | | | 6x7 |
| Saiga 12K (`U_SAIGA_20K`) | Kalashnikov semi-auto | **rock**, none (paddle) | no | handle right | 9x5 |
| SPAS-12 | Pump/semi dual mode (game data decides which BF4 uses) | tube | | | 9x6 |
| USAS-12 | Auto shotgun, AR-like layout | box/drum, button | ? | handle top under the carry handle | 15x5 |
| UTS-15 | Bullpup pump | two tubes over the barrel, loaded from the top ? | | pump forward | 15x3 |

### Carbine (6)

| Gun | Real action | Feed / release | Hold-open | Layout | Game |
| --- | --- | --- | --- | --- | --- |
| A-91 | Bullpup, forward ejection | **rock** (AK-74 magazines), none | no | behind the grip, handle right ? | 31x5 |
| ACE 52 CQB | Galil ACE (Kalashnikov action) | rock ?, paddle | no | handle **left** | 26x5 |
| AK 5C | FNC derivative | box, button | ? | handle right | 31x5 |
| G36C | HK short-stroke | box, paddle behind the magwell | yes, catch in the trigger guard | handle **top**, folding | 31x5 |
| M4 | AR-15 | box, button | yes, bolt catch | handle rear-T, forward assist | 31x5 |
| SG553 | SIG 550 family | box, paddle/button | yes | handle right | 31x5 |

### Assault rifle (10)

| Gun | Real action | Feed / release | Hold-open | Layout | Game |
| --- | --- | --- | --- | --- | --- |
| AEK-971 | Balanced-action Kalashnikov | **rock**, none | no | handle right | 31x5 |
| AK-12 | Kalashnikov | rock, none | no (as modelled) | handle right (measured) | 31x5 |
| AUG | Bullpup | box, button behind the magwell | yes, bolt release button | behind the grip, handle **left** | 31x5 |
| CZ-805 BREN | Gas piston | box, paddle + button | yes | handle side, non-reciprocating ? | 31x5 |
| FAMAS | Lever-delayed bullpup | box, buttons on the magwell | no ? | behind the grip, handle **top** under the carry handle | 26x6 |
| M16A4 | AR-15 | box, button | yes | rear-T | 31x5 |
| M416 | HK416 (AR controls) | box, button | yes | rear-T | 31x5 |
| QBZ-95-1 | Bullpup | box, release behind the magwell | ? | behind the grip, handle **top** under the carry handle | 31x5 |
| SAR-21 | Bullpup | box (STANAG), button | yes ? | behind the grip, handle left ? | 31x5 |
| SCAR-H | Gas piston | box, button | yes, bolt catch | handle left, reciprocating (point set by eye) | 21x6 |

### LMG (7)

| Gun | Real action | Feed / release | Hold-open | Layout | Game |
| --- | --- | --- | --- | --- | --- |
| M240B | Open-bolt GPMG | **belt** from a box on the left, feed-tray cover | open bolt | handle right | 100x3 |
| M249 | Open-bolt LMG | belt (200 box), cover | open bolt | handle right | 200x2 |
| MG4 | Open-bolt LMG | belt (200 box), cover | open bolt | handle right ? | 200x2 |
| PKP Pecheneg | Open-bolt GPMG | belt from the **right**, cover | open bolt | handle right, low | 100x3 |
| QBB-95-1 | Bullpup LMG | **drum** (75), release behind it | ? | behind the grip, handle top | 76x4 |
| Type 88 LMG | GPMG | belt (200), cover; feed side ? | open bolt | handle ? | 200x2 |
| U-100 MK5 (`U_Ultimax`) | Open-bolt, constant recoil | box (30), button | open bolt | handle left | 31x8 |

### DMR (6)

| Gun | Real action | Feed / release | Hold-open | Layout | Game |
| --- | --- | --- | --- | --- | --- |
| MK11 MOD 0 | AR-10 (SR-25) | box, button | yes | rear-T | 21x5 |
| M39 EMR | M14 action | **rock**, paddle in front of the trigger guard | yes, bolt stop | op-rod handle right | 21x5 |
| QBU-88 | Bullpup | box, release behind it | ? | behind the grip, handle ? | 11x9 |
| RFB | Bullpup, forward ejection | **rock** (FAL magazines), paddle | yes | behind the grip, handle left front | 21x5 |
| SCAR-H SV | SCAR-H | box, button | yes | handle left | 21x5 |
| SVD-12 | Kalashnikov-type | rock, paddle | yes (follower) | handle right | 16x6 |

### Sniper rifle (5)

| Gun | Real action | Feed / release | Hold-open | Layout | Game |
| --- | --- | --- | --- | --- | --- |
| SRS | **Bolt action**, bullpup | box behind the grip, button | n/a | bolt behind the grip | 6x8 |
| FY-JS | Bolt action | box, button | n/a | bolt right | 11x6 |
| M82A3 | Recoil-operated semi-auto .50 | box, button/paddle | yes | handle right, bipod | 11x5 |
| Scout Elite | Bolt action (Steyr Scout) | box, button | n/a | bolt right, bipod in the fore-end | 11x6 |
| SV-98 | Bolt action | box, button | n/a | bolt right | 11x5 |

## Handling families (what the player does)

| Family | Guns | Load | Charge / cycle | Mod today | Needed |
| --- | --- | --- | --- | --- | --- |
| **A. Box, button, hold-open** | M4, M16A4, M416, MK11, SCAR-H(+SV), CZ-3A1, CZ-805, SG553, AUG, SAR-21, PDW-R, M82A3, QBU-88, (AK 5C, G36C, CBJ-MS, JS2, MX4, PP-2000, USAS-12 as found) | grab from the chest, seat at the magwell, button drops the old one, palming | last shot locks the bolt; button = bolt catch; rack only after a dry seat | AR variant | handle and release points per gun; paddle release (G36C, UMP) pressed by the off-hand thumb at the magwell |
| **B. Box, no hold-open** | UMP-45, FAMAS?, QBZ-95-1?, Ultimax (open bolt) | as A | every empty reload needs a rack; the button never chambers | (AR variant chambers wrongly) | `hold_open = false` in the state machine; open bolt: the rack cocks, the chamber stays empty until the trigger |
| **C. Rock-in** | AK-12, AEK-971, A-91, Saiga, (ACE 52), M39, RFB, SVD-12 | hook the front, rock back; old one rocked out by hand or knocked out with the new one | AK: rack. M14/FAL/SVD: hold-open, op-rod or handle releases | AK variant (rock + flick) | `hold_open` flag for M39/RFB/SVD; rock geometry (pivot at the front lip) |
| **D. Bullpup** | P90, PDW-R, A-91, AUG, FAMAS, QBZ-95-1, SAR-21, QBB-95-1, QBU-88, RFB, SRS, UTS-15 | magwell behind the grip, near the cheek | handles left/top/front | zones assume the magwell ahead | magwell zone from `Wep_Mag`, reload zone behind the gun hand; the support hand stays on the handguard (not a "near the grip" rule) |
| **E. Top magazine** | P90 | lay the magazine flat on top, slide it back until it clicks; release at the back | handle on either side | none | own insert path (direction along the barrel) |
| **F. Tube, shell by shell** | 870, Hawk?, M1014, QBS-09, SPAS-12, UTS-15 | take one shell (or two) from a belt pouch, push into the loading port; each push +1 | pump: back and forward after every shot; semi: bolt handle/release when empty | none (`ReloadType = single bullet` in the game) | shell pouch zone, a loading-port zone, per-shell count; pump gesture on the fore-end along the barrel |
| **G. Bolt action** | SRS, FY-JS, Scout Elite, SV-98 | box magazine as A (no hold-open) | after every shot: lift the handle, pull back (case flies), push forward, turn down | none; the game cycles the bolt on its own (`BoltActionTime`) | hold the next shot until the hand cycles; drive the bolt bone with the hand; the game's own cycle animation kept from the drawn gun (research) |
| **H. Belt-fed** | M240B, M249, MG4, PKP, Type 88 | open the cover, box off, new box on, lay the belt on the feed tray, close the cover | pull the handle back once (open bolt) | none | lid, box, belt steps (#64), `Wep_Belt` bone; feed side per gun (PKP from the right) |
| **I. Drum** | QBB-95-1 (USAS-12 option) | as D, bigger and heavier | as A/B | | weight in the hand, slower insert |
| **J. Revolver** | M412 REX | push the latch, break the frame open, the extractor throws the cases, speedloader in, close | double action: a long, heavy trigger | none | break-open gesture, speedloader zone, cylinder count |

Families cover reloads. Each gun also needs these, whichever family it is in:

- **Fire selector:** the game's `CycleFireModeInputAction`, cycling through `FireLogicTypeArray`. A thumb
  flick at the selector (or one button) changes it, with a click and haptics. Bolt and pump guns have none.
- **Two hands:** the support spot from `IK_Joint_LeftHand`. Pistol-grip-magazine guns (MX4, PP-2000) and
  the P90 hold differently from rifles. One-handed fire of a long gun kicks harder (#60).
- **Recoil class:** pistol / PDW / rifle / battle rifle (7.62) / shotgun / LMG / .50 (#60, per-gun data in
  `GunSwayData`).
- **Weight and inertia:** heavy guns (LMGs, M82A3) turn slower in one hand and settle with two.
- **Sights:** every gun's default optic and iron sights (#39).
- **Holsters:** the crate lets a long gun go into the **secondary** slot. A rifle can't sit in the hip
  holster: by its size the secondary goes to the hip (pistol, PDW) or across the back.
- **Sounds:** each gun's own reload sound events (#30), found by `tools/weapon_sound_probe.py`.
- **Bipod / rest:** LMGs, M82A3 and the Scout have a `Wep_Bipod`. The game's deploy is a later step.

## Method: one pass per gun (the catalog sweep)

For each gun, with the monitor checks from `docs/tasks/testing.md` (fakehead, fakehand):

1. Take it from the crate (pipe weapon picker, below), primary slot.
2. `weapon_catalog_probe.py --held`: firing data into `docs/weapons.md` (reload type, modes, rof, times,
   bolt time, zoom levels).
3. Bones in `Wep_Root` axes: `Wep_Mag`, `Wep_Slide`, `Wep_Belt`, `Wep_Bipod`, both IK joints. If the handle
   isn't the slide bone, measure it from the game's empty reload, as for the AK-12.
4. Screenshots of the gun from the left, right and top (fakehand turns): magwell, handle, release and
   selector, checked against the real-gun table above. Resolve every `?`.
5. Profile row: the five properties and the points.
6. Monitor check of its family's script (fire to empty, reload, chamber, every fire mode, holster,
   two hands). Raw output goes to the gun's line in `docs/weapons.md`.
7. Rows for the family go to `docs/HEADSET-TESTS.md`: a few broad checks per family, not per gun.

## Work order

Order: first what unblocks everything (unlocks, picking a gun, the catalog), then the families by how many
guns they cover and how much is already built. The box-magazine families (A 21, B 4, C 8, E 1, I 1)
cover 35 of the 51 guns on the existing state machine; then F 6, G 4, H 5, J 1.

| # | Step | Guns | Depends on | Issue |
| --- | --- | --- | --- | --- |
| 0a | ~~**Unlock all weapons** setting~~ **done 2026-11-15** (`unlock_all_weapons`, default on; `AllUnlocksUnlocked` alone) | all | | |
| 0b | ~~**Weapon picker**~~ **done 2026-11-16** (#80): pipe `givegun <id> <slot>` and the VR panel's Own crate rows, through the soldier's customization call (re/notes/weapon.md "Own crate"); attachments still missing | all | | #80 |
| 0c | ~~**Catalog**~~ **data done 2026-11-17**: `tools/catalog_sweep.py` gives each gun and reads it; `docs/weapons.md` (firing data, family, the mod's kind today) | all | | #81 |
| 0d | Fix the AK list (`U_SAIGA_20K`; add A-91 and the rock-in guns once the flags exist) with a unit test | Saiga | | with 1 |
| 1 | **Profile model**: **first part done 2026-11-17**: `weapon/gun_traits` holds feed, release, hold-open, bullpup and cycle for all 51; the AR/AK split and the hold-open come from it (`reload_profile::hold`); tube, belt and revolver guns use the game's reload (`physical_reload_all` = 1 for the old way). Per-gun handle points: 23 guns measured from the game's animations (18 from the empty reload, then CZ-805, M39 and the bolt knobs of SRS, FY-JS and Scout from a shot; `tools/handle_probe.py`, re/notes/weapon.md); magazine grab points for 24 guns (`magazine_grab_for`), all behind the setting `gun_points`; the magwell is each gun's live `Wep_Mag`. Still to do: release points, the magazine's way out (measured, not used), handles for the guns whose reload uses a bolt release (from the mesh) | | 0c | #64 |
| 2 | **Catalog sweep** (method above) for all 51: firing data **done** (docs/weapons.md); bones, screenshots, every `?` still to do | all | 0b, 0c | #81 |
| 3 | **Families A + B** (box, with and without hold-open; paddle release) | 25 | 1, 2 | #64 |
| 4 | **Family C** (rock-in, with/without hold-open) | 8 | 1, 2 | #64 |
| 5 | **Family D/E/I** (bullpup zones, P90 top magazine, drum) | 12 bullpups (in A-C, F, G too) | 3 | #64 |
| 6 | **Fire selector** and **holster by size** (long secondary on the back) | all | 2 | new |
| 7 | **Family F** (tube shotguns: shell by shell, pump per shot) | 6 | 1 | #64 |
| 8 | **Family G** (bolt action: hold the shot until cycled, hand-driven bolt) | 4 | 1, research on the game's bolt cycle | #64 |
| 9 | **Family H** (belt-fed: lid, box, belt, charge) | 5 | 1 | #64 |
| 10 | **Family J** (revolver: break, extract, speedloader) | 1 | 1 | #64 |
| 11 | Recoil and weight classes per gun (#60), sights per gun (#39), reload sounds per gun (#30) | all | 2 | existing |
| 12 | Headset batches: one short list per family as each lands (5-10 broad checks) | | each | |

Each step: its own branch, a live setting where it changes behaviour, a panel row, unit tests for the
engine-free logic, the gate green, the monitor check's raw output on the issue, and the family's rows in
`docs/HEADSET-TESTS.md`.

## Open questions

- ~~How the crate applies a choice (0b)~~ answered 2026-11-16: not needed. A `CustomizeSoldierData` with an
  `UnlockWeaponAndSlot` list applied through the soldier's customization (ServerSoldierEntity slot 100) does it, as the
  level's kit entities do (re/notes/weapon.md "Own crate"). How the crate's own Done applies it is still unknown.
- Bolt and pump guns (8): does the game play its own cycle animation on the drawn gun, and can it be
  held until the hand does it? `FireLogic.BoltAction` (`HoldBoltActionUntilFireRelease`,
  `ForceBoltActionOnFireTrigger`) may already give the hook.
- Shotguns (7): does the game's single-round reload accept rounds written one at a time, the way the
  magazine count is written today?
