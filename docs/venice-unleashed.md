# Venice Unleashed (BF3): reference

Community platform for Battlefield 3: own launcher, accounts, server browser, dedicated servers and a
Lua modding API. Read from veniceunleashed.net and docs.veniceunleashed.net on 2026-11-10 (some doc
pages say they were last edited 2020-2024). Not affiliated with EA/DICE.

## Status and requirements

- Available and free: build 20939 (64 MB) on the home page; account at veniceunleashed.net/signup.
- Needs a legitimate, up-to-date BF3 on the EA app / Origin, launched once; the first VU launch links
  the EA account (one time). The home page says Windows 10+, the FAQ still says Windows 7 SP1+.
- DLC only for servers that use DLC content (mods declare `RequiredXPacks` 1-5).
- Separate from vanilla BF3: its own servers, no game files changed, a VU profile without Battlelog
  progression. The FAQ: no EA ban risk.

## Client

- "VU Client" (installed to `%LocalAppData%\VeniceUnleashed\client`), in-game server browser; servers
  incompatible with your client are greyed out. Mods download automatically on join.
- No anti-cheat is mentioned in the pages read, and nothing about third-party DLLs. How an injected
  DLL such as ours behaves under VU is unknown: check before mixing them.

## Hosting

- Anyone can host: Windows 7+ or Wine 5.0+; a full BF3 install on the server; minimum 3 GHz dual core,
  1 GB RAM, 50 GB disk, 5/5 Mbit (more with players, vehicles, mods).
- A per-server key from veniceunleashed.net/keys saved as `server.key` in the instance folder
  (default `Documents\Battlefield 3\Server`); more keys by request (/key-quota).
- Ports: 7948 UDP (VU networking), 25200 UDP (Frostbite), 47200 TCP (RCON).
- `vu.exe -server -dedicated` (`-headless`, or `vu.com` for a console); 30/60/120 Hz variants.
  Activation: `vu.exe -activate -lsx`, or `-activate -ea_token <token>` without the EA app.

## Modding

- `Admin/Mods/<mod>/mod.json` (Name, Version, Authors, HasVeniceEXT, HasWebUI, RequiredXPacks,
  Dependencies e.g. `"veniceext": "^1.0.0"`, Tags); `Admin/ModList.txt` lists the mods to load.
- VeniceEXT: Lua 5.4 in `ext/client`, `ext/server`, `ext/shared`, each with `__init__.lua`; the API has
  events (notifications) and hooks (change or cancel behaviour), split client / server / shared /
  Frostbite (docs.veniceunleashed.net/vext/ref).
- WebUI: HTML/CSS/JS packed into `ui.vuic`, sent to clients; talks to VeniceEXT.
- Rime: a Frostbite editor whose data bundles VeniceEXT can load (replace or add content such as
  textures). Fully custom models and maps are still on the roadmap ("content authoring").
- All mods live on the server and download to joining clients; test with a local dedicated server.
- Known mods: Reality Mod (bf3reality.com), Fun Bots (github.com/Joe91/fun-bots: AI players).

## Not covered by the docs

- Offline, LAN, single player or campaign: not stated (a local server with Fun Bots is the nearest).
- BF4: no equivalent on the VU site (its sibling is Project Rome, for Bad Company 2). The BF4 project
  people name is Warsaw Revamped; its status was not checked.

## For bf4-vr

The BF3 + Venice Unleashed work has its own repository, `bf3-vr` (sibling folder): its
`docs/RESEARCH.md` holds the fuller research of 2026-11-10 (injected DLLs under VU, launch
arguments, the Lua camera and input hooks, BF3's stereo settings, the local server recipe) and the
plan. Never on official servers (`docs/SAFETY.md`).
