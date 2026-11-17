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

## Open

- Other reload patches (shotguns, LMGs with belts, snipers, pistols) have their own events; only the
  AR patch is mapped. Map them the same way (`sound_probe.py stages --weapon <name>` plus a recording
  of that gun's own reload) when their reload variants exist.
- The sound plays from the entity's own transform (the gun), so it is positioned like the game's.
- LB (off-hand grip) is the quick-throw grenade outside the reload zones (8 cm magwell, 20 cm chest):
  a grab that misses a zone throws a grenade. Not seen as a sound problem; keep in mind.

## Grenade sounds (2026-11-14, owner: "a synthesised pin sound, not the grenade's own")

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
