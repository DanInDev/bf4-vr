# Sounds — BF4 build 179547

Status: **cause of the wrong reload sounds found and fixed (2026-10-26, #30)**; the game's own reload
foley is not played during a physical reload yet (see "Open").

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
- Fix: `grip_spot` (default 0) keeps RB from the game (`weapon/weapon_swap.cpp` pad filter; holsters,
  grenades and two-handed read the raw controller and are unaffected). Verified: RB tap/hold silent
  with `grip_spot = 0`, the sound returns with `grip_spot = 1`.

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

## Open

- **Reload foley at our steps.** Find how an Ant animation sound event reaches the sound system (the
  call that starts a sound patch event on the soldier/weapon emitter), trace it during a normal reload
  (X), and call it with the mag-out / mag-in / bolt events at the physical steps. Start: a write or
  execute trace around the reload begin message (`ClientWeaponPlayerWeaponReloadBeginMessage`), the
  callers of the sound patch event entry, REA on the decrypted dump. Fallback: play recorded clips of
  the game's reload phases ourselves (XAudio2), split at the onsets the probe finds.
- LB (off-hand grip) is the quick-throw grenade outside the reload zones (8 cm magwell, 20 cm chest):
  a grab that misses a zone throws a grenade. Not seen as a sound problem; keep in mind.
