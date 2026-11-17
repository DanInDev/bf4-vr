# Sounds — BF4 build 179547

Status: **cause of the wrong reload sounds found and fixed (2026-10-26, #30)**; **the game's own reload
sounds now play at the physical reload's steps (2026-10-27, AK-12 and SCAR-H verified by recording)**.

## Method

`tools/sound_probe.py` records the default playback device (`bfvr_audio_loopback`, WASAPI loopback,
placed by timestamp so silence keeps the timeline) while the pipe drives one step at a time with
pauses, then reports per step the sound onset and its rise over the level just before. Campaign
checkpoint "Get Grenade Launcher", AK-12, no headset session. Recordings go to `re/dumps/sound/`
(git-ignored).

## Results

| Step (pipe) | Sound after the step |
| --- | --- |
| Fire (RT 0.2 s) | 0.09 s, +42 dB |
| Reload button X (button reload, magazine not full) | 0.65 s, +25 dB (the game's reload sequence) |
| X with a full magazine | none |
| Accessory toggle (D-pad up) | 0.7–1.3 s, +13–18 dB |
| Weapon switch (Y) | small, +11 dB on the way back |
| **Gun-hand grip = pad RB, tap 0.15 s** | **0.45 s, +22 dB** |
| **RB held 1.2 s** | **1.42 s, +22 dB** (≈0.25 s after the release) |
| Off-hand trigger (LT, aim) tap or hold | none |
| B (crouch / stand) | none / +10 dB |
| **Physical reload steps through the pipe only** (`reload grab/letgo/take/insert/rack/stow/fire`: ammo writes, the drawn magazine) | **none** (rise ≤ 9 dB, below the 10 dB threshold) |

- The physical reload itself is silent: writing the server ammo (magazine and reserve, also raising
  the reserve on a stow) plays nothing, so the ammo-pickup sound (`SoldierSoundData.PickupAmmo`) is
  not involved.
- RB acts on release: a tap and a 1.2 s hold both sound ≈0.25–0.3 s after letting go, which is how
  the controller's spot (tap) / command rose (hold) work. Holding a gun in VR means squeezing the
  gun-hand grip, so every squeeze during a reload played a call-out: the "wrong sounds".
- Fix: RB kept from the game. First as `grip_spot` (verified: RB tap/hold silent when kept, the sound
  returns when passed); after merging feature/headset-feedback the general `grips_to_game = 0` (both
  grips, `input::for_game`) does it, and `grip_spot` was dropped. Holsters, grenades and two-handed
  read the raw controller and are unaffected.

## Where weapon sounds live (typeinfo + live reads, `tools/weapon_sound_probe.py`)

- `FiringFunctionData.Sound` (+0x160, SoundAsset). AK-12: `Sound/Weapons/Handheld/AK12/Weapon_Assault_AK12_SPC`,
  a `SoundPatchConfigurationAsset` whose `Sound` (+0x28) is the shared source patch
  `Sound/Weapons/SourcePatches/Handheld/Weapon_SourcePatch_SMG_Assault_LMG`.
- The patch's `SoundGraphData` (+0x28): 4 input events (one is `Fire`), 39 input parameters (one is
  `IsFirstPerson`). Names are hashed with `h = h * 33 ^ c`, seed 5381 (confirmed by those two). No
  reload events: **reload foley is not in the gun's sound patch.**
- `SoldierSoundData` has Death, BulletImpact, Healing, BreathControl, HitIndicator, PickupKit,
  PickupAmmo; `SoldierSoundComponentData` carries Ant trajectory/bone transforms. Reload foley (mag out,
  mag in, bolt) is played by events in the first-person reload **animation** (Ant), which a physical
  reload never runs. So the game plays no reload sounds during a physical reload.

## Reload sound entities and their stage events (2026-10-27)

- Every gun in the level has a reload `SoundEntity` (live vtable `0x141CE88C0`), e.g. for the AK-12
  `SoundEntityData.Sound` = `Sound/Weapons/Handheld/AK12/Weapon_Assault_AK12_Reload_SPC` (configuration)
  -> patch `Sound/Weapons/SourcePatches/Handheld/Weapon_SourcePatch_Reload_AR`. AI soldiers use
  `Sound/Weapons/AI/...` variants. Found with the research pipe `scan <hex bytes>` (search of committed
  private read/write memory for the vtable) and `tools/reload_sound_probe.py`.
- During the game's reload the entity's +0x80 (sound instance) is created at the start; the stage
  sounds are graph input events. The gun's firing patch (`Weapon_SourcePatch_SMG_Assault_LMG`) has
  `Fire`, `Action` (+ `ActionType`), but not the reload stages.
- **Event handler** `0x1407628D0` (vtable slot 5; prologue `48 89 5C 24 08 48 89 6C 24 10 48 89 74 24 18`),
  `(entity, EntityEvent*)`, id at `[event+8]`: `Enable` (0x8BA423E4) / `Disable` (0xF90216B1) handled
  there; **any other id creates the instance if needed (entity +0x91 enabled, data +0x60
  PlayOnCreation) and is forwarded to the instance (`[instance]->vtable[0](instance, id)`)**, i.e. a
  graph input event by its hash. Called on the game thread (view-params copy) through the research
  pipe `entityevent <entity> <id> [slot]`: the stage sounds play.
- `Weapon_SourcePatch_Reload_AR` input events, mapped by sliding spectrogram matching of each event's
  sound against recordings of the game's own reloads (tactical: bursts 0.68 / 1.77 / 2.2 s after X;
  empty: 3.0 / 4.05 / 4.48 / 5.33 s after the last shot):

| Event (hash) | Name | Sound | Match |
| --- | --- | --- | --- |
| `9D874880` | ? | magazine out | 0.88 at the first burst |
| `0C87FAA9` | ? | magazine in | 0.94 / 0.91 |
| `95E4238C` | ? | magazine seated (~0.4 s after in) | 0.65 |
| `7C80C030` | `Bolt` | charging handle / bolt release (empty reload only) | 0.95 at 5.33 s |
| `C46CF22F` | ? | closing foley | 0.74 |
| `7C7F4233` | `Grab` | silent alone | |
| `1C998F75`, `ABEA067C` | ? | silent alone | |

- `weapon/game_reload_sounds` (settings `reload_sounds` and `game_sounds`, default on; where a gun's
  entity is not found, or the event has no game cue, the synthesised/WAV cues of `weapon/reload_sounds`
  from feature/headset-feedback play instead): on a gun change a background thread
  finds the gun's reload entity (`/<name without U_>/..._Reload_SPC`, not `/AI/`), retried every 5 s
  while missing; physical reload events queue cues (out; in + seat after 350 ms; bolt on chambered,
  racked empty and ejected rounds; settle on stow; an AR release that drops the magazine out of the
  gun plays "out"); the game thread sends them after checking the entity's vtable and data pointer.
  Recorded through the pipe: AK-12 grab +24 dB, insert +23/+28 dB, rack +34 dB; SCAR-H release +24,
  insert +29, rack +32, catch +23, stow +22; 0 refused.

## Silent bolt and magazine with an empty magazine (#78, 2026-11-16)

- **Rack on an empty AR magazine:** the state machine returned `bolt_locked` (the follower holds the bolt open), the event
  the last shot uses, which has no cue. Now `racked_locked`, with the bolt cue. The bolt catch pressed on an empty
  magazine drops the bolt home (`racked_empty`, bolt cue), as a firm press does on a real AR.
- **Release while the off hand holds the new magazine** (the usual empty reload): the drop event has no cue and the
  fallback (`sound_if_magazine_left`) skipped it while a hand held a magazine. Now the cue is sent for any seated
  magazine leaving the gun unless a pull, flick or palming already played it this frame (`release_needs_out_cue`).
  **Sent, not shown audible:** for the SCAR-H the counter went 11 -> 12, but the one recording of that release heard
  nothing (and two other steps right after a pouch take were silent in that run, audible in others). Open: does the
  SCAR-H graph drop a cue that follows a take within ~2.5 s? Next: `entityevent` the out event by hand after a take.
- Recorded with `tools/sound_probe.py empty_ar` / `empty_ak` / `order_ak` (AK-12: an out after two takes, +17.6 dB).

## The player's own foley (2026-11-03, #33)

- Loaded in the campaign (memory scan for `Sound/Character/` names): waves
  `Foley/ArmMovement/Soldier_Foley_ArmMovement_Loop_Wave`, `Foley/Soldier_Foley_PoseChange_Wave`,
  `Soldier_Foley_Sprint_Loop_Wave`, `Soldier_Foley_Stand_To_Prone_Wave`, `Soldier_Foley_Sprint_To_Prone_Wave`,
  `Soldier_Foley_Land_Wave`, `Soldier_Foley_Crawl_*`, surface footsteps (`Footsteps/<surface>/Walk|Run|Sprint_*`),
  `FX/WindOnSpeed_Wave`, `SpecialMoves/Melee/Melee_Player`. So the game has an arm-movement cloth loop and a
  pose-change sound: what #33 wants, without synthesising anything.
- `SoldierEntityData` (soldier +0x30) +0xC8 `SoldierSoundData` has every SoundAsset null in the campaign
  (Movement, Death, ...; floats 5.0 / 0.1 / 0.5). Five `ClientSoldierSoundComponent` objects (vtable
  `0x141BDFEB0`) exist but name no asset directly.
- **The player's movement patch is `Sound/Character/Soldier_Movement_1p`**, found by typing the objects
  around the `SamplerNodeData` that plays the ArmMovement loop wave (+0x90). One `SoundEntity` (live vtable
  `0x141CE88C0`) uses it through a `SoundEntityData` (+0x18 Sound). Its instance (+0x80) exists only while
  the soldier moves (walking creates it, so does `OnStart`), with the parameter values as floats at
  `[instance + 0x8] + 0x80` by ValueIndex.
- Graph inputs (names by hash h*33^c; the rest brute-forced over 1-3 movement words, unnamed):

| Kind | Named | Unnamed hashes |
| --- | --- | --- |
| Events (19) | `OnStart`, `OnStop`, `Stop`, `OnLand`, `OnJump`, `OnEnterAir`, `OnEnterWater`, `OnDamage`, `Vault`, `DiveStart`, `DiveEnd` | `14CDD291 184BEDD0 2A2A53AB 89210E72 89680ECF A83B87A0 AF14D0C9 D9FEB84B` |
| Parameters (9) | `Speed` (index 8; 3.05 walking), `Pose` (index 4; 0 stand, 1 crouch: set by the game when the physical stance crouches), `WaterDepth` (10) | `A4FCD045` (1), `D565F120` (3), `9DCCF178` (5), `B0922CC1` (6), `8C4BF2EB` (7), `9002E2CC` (9) |

- Tried on the monitor, all without result: no unnamed parameter changes while the fake gun hand sweeps
  100 deg, the stick turns the view, or the soldier walks, sprints or crouches. Writing 1.0 into each one,
  and firing every event at the entity (`entityevent`, as for the reload sounds), gave nothing above the
  checkpoint's ambience and music (-20 dB median; one shot is only +8 dB over it). So this entity does not
  sound on events the way the reload entities do, and what drives the ArmMovement loop is not found.
- Next: decode the graph's node connections (which input feeds the ArmMovement sampler's gain), find the
  instance that actually sounds (the `ClientSoldierSoundComponent` may own another), and record in a quiet
  spot. Until then a rustle on quick hand movement can only be a mod cue (a `bfvr_sounds` WAV), not the
  game's own.

## Census of the crate guns' reload sounds (2026-11-17, tools/sound_census.py)

Each crate gun given into the slot in hand, the mod's lookup allowed 14 s, then the reload sound sets in memory read
(a gun's own set exists only while it is held; with the gun in hand it appears beside the level's AK-12, SCAR-H, M4A1,
M320, GP-30 and M26 sets).

- **The mod's lookup found 33 of 51.** Missed: UMP-45, 870, QBS-09, Saiga, USAS-12, AK 5C, G36C, AUG, M16A4, QBZ-95-1,
  QBB-95-1, MK11, M39, RFB, SCAR-H SV, SVD-12, FY-JS, M82A3: every reload sound on them is synthesised.
- **Name mismatches** (the set exists, the folder is not the unlock name without `U_`): U_870 -> `Remington870`,
  U_SAIGA_20K -> `Saiga12`, U_AK5C -> `AK5`, U_M16A4 -> `M16A3`, U_QBZ951 -> `QBZ-951`, U_MK11 -> `Mk11`,
  U_M82A3_SP -> `M82A3`. The other misses showed no new set in the scan (unreliable: pipe `scan` returns at most 64
  matches per region, so "none appeared" is not proof).
- **Patches** (the set's SoundPatch and whether the AR event hashes exist in its graph):

| Patch | Guns seen | AR events present |
| --- | --- | --- |
| `Weapon_SourcePatch_Reload_AR` (8 events) | CBJ-MS, Scorpion, P90, PP-2000, Saiga, ACE 52, AK 5C, SG553, FAMAS, M16, M416, QBZ-95-1, SAR-21, MK11, M82A3 (and the AK-12, SCAR-H, M4A1) | all five |
| `Weapon_SourcePatch_Reload_LMG` (12) | M240, M249, MG4, Type 88, Ultimax | in, seat, out, bolt (4 more unmapped: lid, belt?) |
| `Weapon_SourcePatch_Reload_Shotgun_PumpAction` (7) | 870, SPAS-12 | seat, settle, bolt; **no `in`**: the shell-in cue goes nowhere |
| `Weapon_SourcePatch_Reload_5` (10) | UTAS (and the M320, GP-30) | **none**: every cue goes nowhere |

- So two failure kinds: guns whose set is not found play the **synthesised** cues; guns whose set is found but whose
  patch lacks the AR event play **nothing** for that step (the synthesised fallback is skipped once a set is found).
- Always synthesised on every gun: the rear stop of a rack or pump (`bolt_back`), the belt-fed lid and belt, the
  revolver's open and close, the M320, the grenade pin.

### Fixed the same day

- **Lookup by the sound folder:** the gun's firing sound (`FiringFunctionData.Sound`, +0x160) names its folder
  (`sound/Weapons/Handheld/Remington870/...`); the reload set lives in the same folder (checked for all seven
  mismatches). The mod now searches that folder, ignoring case: **50 of 51 found** (census 3). Still missing: SVD-12
  (folder `SVD12`, no reload set seen).
- **Patches by gun:** AR 34 (all the box-magazine guns incl. Hawk, USAS-12, M39, MK11, M82A3), Shotgun_PumpAction 4
  (870, M1014, QBS-09, SPAS-12), Sniper_BoltAction 4 (SRS, FY-JS, Scout, SV-98), LMG 6 (M240, M249, MG4, Pecheneg,
  Type 88, Ultimax), Pistol 1 (REX), Reload_5 1 (UTAS).
- **A cue the patch lacks is not sent** (it played nothing); the synthesised one plays (`patch_has_event`, the patch's
  input events read when the set is found; pipe `reload` shows `patch=`, `events=`, `unplayable=`).
- **The other patches' events**, by `tools/sound_probe.py game_empty` (the game's own empty reload of a gun recorded),
  `stages` (each of its patch's events fired alone) and `match` (each event's spectrum against the sounds of that
  reload; times read against the gun's bone timeline from handle_probe):

| Patch | Event | Sound in the game's reload | Score | Used for |
| --- | --- | --- | --- | --- |
| Shotgun_PumpAction (870) | `2519DEEC` | the repeated tube loads (4.95, 6.85 s) | 0.98/0.99 | a shell in |
| | `71B0C476` | the first sound, the pump back (1.92 s) | 0.98 | the rear stop |
| | `Bolt` (as AR) | | | the pump stroke |
| Sniper_BoltAction (Scout) | out / in / `Bolt` (as AR) | 2.88 / 3.44 / 4.49 s | 0.91/0.99/0.95 | as before |
| | `9FA31A86` | the bolt (4.49 s) | 0.99 | the rear stop |
| LMG (M249) | `3A3332B0` | as the lid opens (3.69 s; bones 3.1-3.7 s) | 0.94 | lid opened |
| | `27EE4912` | as the lid shuts (6.84 s) | 1.00 | lid closed |
| | seat (as AR) | | 0.76 | belt laid (weak) |
| Pistol (REX) | out (as AR) | the break open (0.87 s) | 0.98 | frame opened |
| | `6F002904` | the close (3.38 s) | 0.97 | frame closed |
| Reload_5 (UTAS) | `0002B591` | the repeated shell loads | 0.98 | a shell in |
| | `0002B597` | the first, loudest (1.57 s) | 0.99 | the pump |

  Not matched yet (no sound of their own, or not placed): Shotgun `34B596D0` (a shell variant), LMG `713E602E`, Scout
  `AF4D8F3A`, `526FE9C4`, `703E0E38` (bolt parts), REX `257F2678` (close variant), UTAS `7C52956C`, `7C53194D`,
  `0002B594`, `0002B596`, `0002B590`. The burst finder is coarse (it found 1-5 sounds per reload); the matches above
  are the ones whose time also fits the bone timeline.
- **Verified in the mod** (`sound_probe.py verify`, the physical reload's steps through the pipe, rise over the quiet
  before): 870 pump rear stop +23 dB (the game's, was synthesised), pump +26, shell in +22 (was silent); M249 lid open
  +19, lid shut +27; REX break open +20, speedloader +25, close +32; UTAS pump +29, shell +16 (both were silent).
  Silent though present in the patch, so left to the synthesised cue: the pump's and Reload_5's settle on taking a
  shell; the Scout's `9FA31A86` as a rear stop (it sounds alone, not there).

## Game files (2026-11-17, owner: "find game sounds")

- **The game's waves, decoded from its own data, nothing recorded.** `tools/gamesounds` is the Great Destroyer's BF3
  reader (cas.cat, toc/sb, EBX, EA-XAS), changed for BF4 (credit in its header): BF4 tocs are version `00 D1 CE 03`
  with a signed 0x22C header and a plain body; payloads come in blocks of type 0x0070/0x0071 (plain) and 0x0970 (LZ4);
  audio chunks are block-wrapped too; the EBX header has u16 counts and 4-byte instance repeaters (complex, count), only
  the first `numGUIDRepeater` repeaters' instances carry a GUID, and a 4-aligned instance's field offsets are 8 lower
  than declared, base classes included (a base field adds no offset); a SoundWaveAsset's takes are `RuntimeVariations`
  structs indexing one `Segments` list; the weapon foley is codec 2 (16-bit big-endian PCM, `48 00 00 0c` header block
  `12 00 bb 80` = version 1, codec 2, mono, 48 kHz), other sounds EA-XAS v1 (codec 4).
- Checked: the AK-12's decoded `Reload_1P_AK12_Bolt/Magin/Magout` correlate 0.89 / 0.84 / 0.92 with a recording of the
  game playing those events, at -18 / -25 / -24 dB: the game plays them that far under full scale (the cues use -18).
- **BF4 names one wave per reload step** (`sound/weapons/reload_1p/reload_1p_<gun>_<step>_wave`, 271 of them): `bolt`,
  `magin`, `magout`, `grab` for most guns; `boltback` / `boltforward` for the bolt-action snipers; `openlid` /
  `closelid` (M249, PKP), `closelid` (M240, MG4, Type 88, LSAT); `insertfirstshell` / `insertshell` (870, M1014,
  QBS-09, SPAS-12, UTAS left/right); `m320_opentube` / `grenadein` / `grenadeout`; `mp412_close_chamber`; `ump_boltopen`
  / `boltclose`. Shared handling: `shared_content/handling/generic_deploy_wave` (9 takes, the weapon switch's cloth),
  `pistol_deploy`, `deploy/pickupweapon`; the grenade's `handheld/m67/m67_remove_safety_pin_wave` (#66).
- A gun's `bolt` wave holds the whole rack: the handle reaching the back, then (louder) the bolt slamming home; the AK-12
  0.09 s then 0.26 s, the SCAR-H 0.07 / 0.16, the M249 0.23 / 0.32. The M16 (and M4A1), M416, AUG, SG553, RFB, JS2,
  CBJ-MS and AK 5C waves start at the release: no rear stop of their own.
- `tools/game_cues.py` decodes them into `re/dumps/gamesounds` (git-ignored) and installs the cues into `bfvr_sounds`:
  per gun (`bfvr_sounds\<sound folder>\bolt_back.wav`, the folder the mod reads from the firing sound) the knock before
  the slam, the snipers' own `boltback`, the G36C's knock for the ten without one (the Great Destroyer's owner chose the
  G36C's pull for the BF3 M16/M4/M416 too) and as the default for any other gun; `cloth.wav` = generic_deploy take 0
  (a shell, a belt or a grenade from the pouch on guns whose set has no pouch foley); `pin.wav` = the M67's pin;
  `M320\magazine_out.wav` / `magazine_in.wav` = its tube opening / a grenade in. The pump shotguns keep their own pump
  event at the rear stop. Log: `reload sounds: ... replaces bolt_back for AK12`.
- Replaced the same day's first try, a rear stop cut from a loopback recording of the Scout's set (the level's ambience
  only ~17-20 dB under it; the BF3 mod's owner had rejected recorded sounds for that reason).

## What still is not the gun's own sound (2026-11-17)

| Step | Guns | Plays now | Why |
| --- | --- | --- | --- |
| Rear stop of a rack | M16A4, M4A1, M416, AUG, SG553, RFB, JS2, CBJ-MS, AK 5C | the G36C's knock | their bolt wave starts at the release |
| Shell / belt / grenade from the pouch | 870, M1014, QBS-09, SPAS-12, UTAS; LMG belt | the game's weapon-switch cloth (not the gun's) | the patches' settle is silent |
| Belt laid on the tray | LMGs | the gun's magazine seat (weak match, 0.76) | no belt event placed |
| Everything | SVD-12 | synthesised | no reload set and no `reload_1p` wave of its own |
| M320 closed, round out | M320 | synthesised | no close wave; `grenadeout` not wired |
| Revolver: cases out, speedloader dropped | REX | nothing | not modelled yet |

Not checked in the game yet: that the mod finds the per-gun files (the log line) and how the cues sit in the mix.

## Open

- The sound plays from the entity's own transform (the gun), so it is positioned like the game's.
- LB (off-hand grip) is the quick-throw grenade outside the reload zones (8 cm magwell, 20 cm chest):
  a grab that misses a zone throws a grenade. Not seen as a sound problem; keep in mind.

## Grenade sounds (2026-11-14, owner: "a synthesised pin sound, not the grenade's own")

- **2026-11-17: done through the game files** (section "Game files"): `tools/game_cues.py` decodes
  `M67_Remove_Safety_Pin_Wave` and installs it as `bfvr_sounds\pin.wav`, which replaces the synthesised pin (#66).
  The live-entity routes below are no longer needed.

- Loaded in the Baku level (pipe `scan` for `Weapons/Handheld/M67`): waves
  `Sound/Weapons/Handheld/M67/M67_Remove_Safety_Pin_Wave`, `M67_Deploy_Wave`, `M67_Throw_Wave` and an
  asset `Sound/Weapons/Handheld/M67/M67`.
- None of the 106 live `SoundEntity` objects (43 sounds) is a grenade's while the rifle is out: the pin
  sound comes with the grenade weapon's own animation, which a physical grenade never runs.
- Open: play that wave (trigger the M67 patch while the grenade weapon exists, e.g. during the grenade
  capture, or decode the wave from the game files as the BF3 VR mod does). Until then the synthesised
  `cue::pin` plays.
- **#66 next step (2026-11-16):** the grenade's `FiringFunctionData.Sound` (+0x160) is the asset
  `Sound/Weapons/Handheld/M67/M67` (0x...c4d0 in that session), but not a `SoundPatchConfigurationAsset`: read as one, its
  "patch" at +0x28 has no name and its graph's event list is garbage (`tools/weapon_sound_probe.py --slot 6`).
  `object_probe.py` finds no vtable for it (data assets carry none). Next: type it through the type registry's data layout
  (which class has a SoundGraph where this one does), list its graph events, and look for the pin event; or record the
  game's own grenade (pipe `holster` to the grenade, pull the pin by the game) with `tools/sound_probe.py` to see which
  SoundEntity plays it.
- **#66 second pass (2026-11-16):** `tools/reload_sound_probe.py --match <text>` lists any SoundEntity by its sound's name.
  With the rifle out none mentions `M67` or `Grenade`. Selecting the grenade to list them while it is held failed
  headless: neither the 5 key through the pipe (Raw Input) nor as a real key with BF4 in front changed the slot (current
  stayed 0; no log line), also with `grenade = button`; the level-start capture had selected it earlier in the session.
  So the pin wave is not reachable through a SoundEntity like the reload sounds: it rides on the grenade's first-person
  animation (an Ant sound event). Ways left: (1) find the animation's sound event path (the soldier's sound component
  playing `M67_Remove_Safety_Pin_Wave`) with REA from the wave's name; (2) during the mod's own grenade capture (the
  game's grenade is selected for ~1-3 s), hold fire so the game's pin animation runs, and watch which object's sound
  instance starts (a write watch on a SoundEntity-like +0x80); (3) a user-supplied local WAV (never in git).
