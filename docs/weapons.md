# Weapon catalog: the 51 crate guns (data)

Measured 2026-11-17 by `tools/catalog_sweep.py` (own crate #80: each gun given into slot 0 in the Baku tunnel, its spawned
firing data read, `re/dumps/catalog_sweep.json` kept out of git). "Mod" is the reload kind physical reload chose for it
(`weapon/profiles.cpp`, from bfvr.log); today only AR and AK exist. Families as in `docs/tasks/all-weapons.md`.

| Gun | Unlock | Magazine (game: rounds incl. chamber x spare) | Game reload | Fire logic / modes | RoF | Reload s (tactical/empty) | Bolt s | Family (from the data) | Mod today |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| MP412 Rex | `U_MP412Rex` | 6x4 | magazine | single | 180 | -/3.4 | 1.0 | J revolver | ? |
| CBJ-MS | `U_CBJ-MS` | 51x3 | magazine+short | auto, single | 700 | 2.4/3.3 | 1.0 | box magazine (A-E, by gun) | AR |
| CZ-3A1 | `U_Scorpion` | 31x5 | magazine+short | auto, single, burst | 1000 | 1.9/3.0 | 1.0 | box magazine (A-E, by gun) | AR |
| JS2 | `U_JS2` | 31x5 | magazine+short | auto, single, burst | 900 | 2.45/3.15 | 1.0 | box magazine (A-E, by gun) | AR |
| MX4 | `U_MX4` | 31x5 | magazine+short | auto, single | 830 | 2.6/3.6 | 1.0 | box magazine (A-E, by gun) | AR |
| P90 | `U_P90` | 51x4 | magazine+short | auto, single | 900 | 2.9/3.55 | 1.0 | box magazine (A-E, by gun) | AR |
| PDW-R | `U_MagpulPDR` | 31x5 | magazine+short | auto, single | 750 | 2.5/3.4 | 1.0 | box magazine (A-E, by gun) | AR |
| PP-2000 | `U_PP2000` | 45x4 | magazine+short | auto, single | 650 | 2.45/3.45 | 1.0 | box magazine (A-E, by gun) | AR |
| UMP-45 | `U_UMP45` | 26x5 | magazine+short | auto, single, burst | 600 | 1.85/2.5 | 1.0 | box magazine (A-E, by gun) | AR |
| 870 MCS | `U_870` | 8x5 | single | bolt | 600 | -/0.67 | 0.55 | F tube (shell by shell), pump | AR |
| Hawk 12G | `U_HAWK` | 6x7 | magazine+short | bolt | 600 | 1.95/2.6 | 0.62 | pump/semi box shotgun (Hawk: pump) | AR |
| M1014 | `U_M1014` | 8x5 | single | single | 210 | -/0.55 | 1.0 | F tube (shell by shell), semi | AR |
| QBS-09 | `U_QBS09_SP` | 6x7 | single | single | 230 | -/0.67 | 1.0 | F tube (shell by shell), semi | AR |
| Saiga 12K | `U_SAIGA_20K` | 9x5 | magazine+short | single | 200 | 2.2/3.5 | 1.0 | box magazine (A-E, by gun) | AR |
| SPAS-12 | `U_SPAS12` | 9x6 | single | bolt | 600 | -/0.67 | 0.55 | F tube (shell by shell), pump | AR |
| USAS-12 | `U_USAS-12_SP` | 15x5 | magazine+short | auto, single | 300 | 2.5/3.45 | 1.0 | box magazine (A-E, by gun) | AR |
| UTS-15 | `U_UTAS` | 15x3 | single | bolt | 600 | -/0.83 | 0.6 | F tube (shell by shell), pump | AR |
| A-91 | `U_A91` | 31x5 | magazine+short | auto, single | 800 | 2.7/3.35 | 1.0 | box magazine (A-E, by gun) | AR |
| ACE 52 CQB | `U_GalilACE52` | 26x5 | magazine+short | auto, single | 650 | 2.3/3.4 | 1.0 | box magazine (A-E, by gun) | AR |
| AK 5C | `U_AK5C` | 31x5 | magazine+short | auto, single | 700 | 1.95/2.4 | 1.0 | box magazine (A-E, by gun) | AR |
| G36C | `U_G36C` | 31x5 | magazine+short | auto, single, burst | 650 | 2.3/2.85 | 1.0 | box magazine (A-E, by gun) | AR |
| M4 | `U_M4A1` | 31x5 | magazine+short | burst, single | 800 | 1.85/2.4 | 1.0 | box magazine (A-E, by gun) | AR |
| SG553 | `U_SG553LB` | 31x5 | magazine+short | auto, single, burst | 830 | 2.15/2.65 | 1.0 | box magazine (A-E, by gun) | AR |
| AEK-971 | `U_AEK971` | 31x5 | magazine+short | auto, single, burst | 900 | 2.3/3.6 | 1.0 | box magazine (A-E, by gun) | AK |
| AK-12 | `U_AK12` | 31x5 | magazine+short | auto, single, burst | 650 | 2.3/3.2 | 1.0 | box magazine (A-E, by gun) | AK |
| AUG A3 | `U_SteyrAug` | 31x5 | magazine+short | auto, single | 700 | 2.5/3.1 | 1.0 | box magazine (A-E, by gun) | AR |
| CZ-805 | `U_CZ805` | 31x5 | magazine+short | auto, single, burst | 700 | 2.65/3.4 | 1.0 | box magazine (A-E, by gun) | AR |
| FAMAS | `U_FAMAS` | 26x6 | magazine+short | auto, single, burst | 1000 | 2.8/3.7 | 1.0 | box magazine (A-E, by gun) | AR |
| M16A4 | `U_M16A4` | 31x5 | magazine+short | burst, single | 800 | 1.8/2.37 | 1.0 | box magazine (A-E, by gun) | AR |
| M416 | `U_M416` | 31x5 | magazine+short | auto, single | 750 | 1.85/2.4 | 1.0 | box magazine (A-E, by gun) | AR |
| QBZ-95-1 | `U_QBZ951` | 31x5 | magazine+short | auto, single, burst | 650 | 2.9/3.55 | 1.0 | box magazine (A-E, by gun) | AR |
| SAR-21 | `U_SAR21` | 31x5 | magazine+short | auto, single | 600 | 2.3/3.4 | 1.0 | box magazine (A-E, by gun) | AR |
| SCAR-H | `U_SCAR-H` | 21x6 | magazine+short | auto, single | 620 | 1.9/2.4 | 1.0 | box magazine (A-E, by gun) | AR |
| M240B | `U_M240` | 100x3 | magazine | auto | 650 | -/6.2 | 1.0 | H belt-fed | AR |
| M249 | `U_M249` | 200x2 | magazine | auto | 800 | -/6.2 | 1.0 | H belt-fed | AR |
| MG4 | `U_MG4` | 200x2 | magazine | auto | 800 | -/6.2 | 1.0 | H belt-fed | AR |
| PKP Pecheneg | `U_Pecheneg` | 100x3 | magazine | auto | 600 | -/5.5 | 1.0 | H belt-fed | AR |
| QBB-95-1 | `U_QBB95` | 76x4 | magazine+short | auto, single, burst | 650 | 4.3/4.9 | 1.0 | box magazine (A-E, by gun) | AR |
| Type 88 LMG | `U_Type88` | 200x2 | magazine | auto | 700 | -/7.3 | 1.0 | H belt-fed | AR |
| U-100 MK5 | `U_Ultimax` | 31x8 | magazine+short | auto, single, burst | 590 | 2.75/3.9 | 1.0 | box magazine (A-E, by gun) | AR |
| MK11 MOD 0 | `U_MK11` | 21x5 | magazine+short | single | 260 | 1.85/2.4 | 1.0 | box magazine (A-E, by gun) | AR |
| M39 EMR | `U_M39EBR` | 21x5 | magazine+short | single | 300 | 2.25/3.4 | 1.0 | box magazine (A-E, by gun) | AR |
| QBU-88 | `U_QBU88` | 11x9 | magazine+short | single | 310 | 2.65/3.9 | 1.0 | box magazine (A-E, by gun) | AR |
| RFB | `U_RFB_SP` | 21x5 | magazine+short | single | 260 | 2.5/3.2 | 1.0 | box magazine (A-E, by gun) | AR |
| SCAR-H SV | `U_SCAR-HSV` | 21x5 | magazine+short | single | 260 | 1.85/2.3 | 1.0 | box magazine (A-E, by gun) | AR |
| SVD-12 | `U_SVD12` | 16x6 | magazine+short | single | 260 | 1.95/2.6 | 1.0 | box magazine (A-E, by gun) | AK |
| SRR-61 | `U_SRS` | 6x8 | magazine+short | bolt | 360 | 3.0/4.5 | 1.25 | G bolt action | AR |
| FY-JS | `U_FY-JS` | 11x6 | magazine+short | bolt | 360 | 2.6/4.8 | 1.25 | G bolt action | AR |
| M82A3 | `U_M82A3_SP` | 11x5 | magazine+short | single | 125 | 4.2/5.6 | 1.0 | box magazine (A-E, by gun) | AR |
| Scout Elite | `U_Scout` | 11x6 | magazine+short | bolt | 360 | 2.85/4.3 | 1.25 | G bolt action | AR |
| SV-98 | `U_SV98` | 11x5 | magazine+short | bolt | 360 | 2.9/4.7 | 1.2 | G bolt action | AR |

## What the data says

- **Tube-fed (game reload = single round): 870, M1014, QBS-09, SPAS-12, UTS-15.** The game reloads them round by round
  (0.55-0.83 s per round). Physical reload treats all five as an AR box magazine: wrong (family F).
- **Pump / bolt cycle (fire logic = bolt): 870, Hawk 12G, SPAS-12, UTS-15 (pump, bolt time 0.55-0.62 s) and SRR-61, FY-JS,
  Scout Elite, SV-98 (bolt action, 1.2-1.25 s).** The game cycles them itself after each shot (family G and F).
- **Belt-fed (game reload = whole magazine, no tactical reload, 100-200 rounds, 5.5-7.3 s): M240B, M249, MG4, PKP, Type 88.**
  Physical reload treats them as AR box magazines (family H).
- **Revolver: MP412 Rex**, 6 rounds, game reload 3.4 s, fire logic single: AR kind today (family J).
- **AK kind only for AK-12, AEK-971, SVD-12.** The other rock-in guns (A-91, ACE 52, Saiga 12K: `U_SAIGA_20K`, the
  known list bug) get the AR kind.
- The other 30 guns are box magazines with a tactical/empty reload pair (game `magazine+short`) and fit the existing
  AR/AK state machine once each gun's release, hold-open and handle are set (families A-E).
- Burst-first guns: M4 and M16A4 start in burst. Not read: bones, handle points, sights (step 2 of the method).
