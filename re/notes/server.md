# Local server, hosting and direct connect: BF4 build 179547

> Copied to `main` on 2026-11-10 as a record. The code it mentions (pipe `startlevel`, the launcher's
> private test switch, `game_telemetry`, the selector hook) and the SAFETY.md rule 1 exception exist
> only on the branch `re/local-server`; `main` stays campaign only. Outcome: the client refuses an MP
> map without an EA Online session, so multiplayer work moved to Battlefield 3 + Venice Unleashed
> (repository `bf3-vr`).

Status: **static analysis plus a read-only live check in the campaign** (2026-11-05, REA/Ghidra 12.1.4 on `re/dumps/bf4-179547-mem.exe`, sha256
`ac3ba0ae…779d`, plus `tools/strxref.py`, `callers.py`, `disasm.py` and the type dump). Nothing here
has been run. Question behind it: can two PCs play BF4 together without EA's online services, so VR
multiplayer can happen on a private server (see the research in the session of 2026-11-05).

Short answer: the retail executable still contains a **hosted mode** (the client runs the server and
accepts others) and a **direct-connect join** (`ip:port`, no matchmaking). Both are ordinary code
paths. What is not known yet is whether the server accepts a joining player without an EA login.

## Settings access

- Settings manager: global `0x14261E930`. `0x140688B90(manager, TypeInfo*)` returns the live settings
  object of that type; callers cache it in a global (`ClientSettings` in `0x142364BF8`, `GameSettings`
  in `0x142364C38`, `NetworkSettings` in `0x142371C48`). Field offsets come from the type dump.
- The manager also answers by dotted name: `0x1406881D0(manager, "Client.ServerIp")` (exists?) and
  `0x140688D30(manager, name, …)` (value).
- **Command-line settings are off in retail.** Startup `0x14000D350` builds a Lua table of the command
  line, then runs `Scripts/Game/Startup.lua` with `allowCommandlineSettings = false` written as a
  constant (Ghidra removed the unreachable `true` branch). So `-Client.ServerIp …` most likely does
  nothing; the mod would write the settings in memory instead (it runs in the process).
- Arguments the retail build still reads directly (`0x14063F5E0(name)`): `listen`, `dedicated`, `host`,
  `name`, `instanceName`, `requestState`, `requestStateParams`, `webmode`, `startpoint`, `saveGame`,
  `serverInstancePath`, plus window options. `server` and `single` are read in `0x140010CD0`, but the
  result is not used.
- **One instance per PC:** `0x140010CD0` creates `Global\{000D5EF3-1452-49F0-8EAE-62C90372441B}_Instance_Mutex`
  and stops when it already exists (`ERROR_ALREADY_EXISTS`). Two copies on one PC need a second PC (or
  a mod change); a two-player test needs two machines.

## Client: Startup decides SP, hosted MP or join

Client state entry `0x14081F6B0` (`this` = client, `+0x30` its `ClientSettings*`). `ClientState`
enum (type dump): 4 `Startup`, 5 `StartServer`, 6 `WaitingForLevel`, 14 `ConnectToServer`, …
On entering `Startup` it reads `ClientSettings.ServerIp` (+0x48):

| `ServerIp` | Level (`GameSettings.Level`) | Message sent | Meaning |
| --- | --- | --- | --- |
| empty | `IsMultiplayer` 0 | `ClientStartSingleplayerMessageBase` (ctor `0x141A8F690`) | campaign: local server |
| empty | `IsMultiplayer` 1 | `ClientStartMultiplayerMessage` (ctor `0x141A8F640`, +0x60 tutorial flag, +0x61 `IsCoop`) | **hosted**: this PC runs the MP server and plays on it |
| set | – | `ClientJoinMultiplayerMessageBase` (ctor `0x14024BA50`, address string at +0x28) | **join** `ip:port` directly |

- Join: when `ServerIp` has no `:`, the client appends `:` + `NetworkSettings.ServerPort` (+0x24).
- The level flags come from `0x14097DF20(levelName)`, a lookup in the `LevelListReport` asset:
  `+0x40` `IsMultiplayer`, `+0x41` `IsCoop` (`LevelDescription` +0x18/+0x19 inside the asset at +0x28).
- Message types are `djb2-xor` hashes (`h = (h*33) ^ c`, seed 5381) of the class name; the category
  `0xA23A0E7C` is `"Client"`.
- `ClientGameType` enum: 0 `SinglePlayer`, 1 `Hosted`, 2 `MpTutorial`, 3 `Joined`, stored at client
  +0x40C.

## Client: the state machine

`0x140828CE0` (jump table `0x14082946C`, one entry per `ClientState`):

- `StartServer` (5) → `0x1408260D0`: asks the server creator (client +0x420, vtable +0x00) to start a
  server with a parameter block, then connects to it as `"%s:%u"`:
  `127.0.0.1` when client +0x415 is set, `""` when +0x416 is set, otherwise `localhost`. The
  campaign uses the same connect path, but its server peer is "local" (peer +0x4247, from the
  parameter block's +0x25) and opens **no socket** (live check below): the link is in-process.
- `ConnectToServer` (14) at `0x140829107`: when the address string at client +0x3C0 is non-empty it
  calls `0x14083EA80(connection +0x438, address, password +0x3B8)`, which parses `host:port`
  (`0x14065A4D0`) and connects (`0x14083E860`); otherwise it joins by the native data at +0x3C8
  (`0x14081E4A0` → `0x14083EA50`, the matchmaking route). Then it goes to `WaitingForLevel`.
- The join message is handled in `0x1408210A0`: it clears +0x415/+0x416 (not local), sets
  `ClientGameType` 3 `Joined` (+0x40C), copies the matchmaking data to +0x3C8 (length from the
  message) and, when the message's string at +0x28 is non-empty, assigns it to +0x3C0.
- The client object is reachable through the global `0x142668F98` (live, below).

## Server: which address it listens on

Server network start `0x1409D7230` (called from `0x1409CDBA3`, in `0x1409CD480`):

- Listen address = the `-listen` argument. When that is empty: `127.0.0.1` if the parameter block's
  +0x25 byte is set (the client's +0x415 "local only"), otherwise `""` = **all interfaces**.
- Port = `NetworkSettings.ServerPort`; on a failed bind it tries the next port when
  `IncrementServerPortOnFail` (+0x7C) is set. `NetworkSettings.TitleId` (+0x18) is hashed into the
  connection setup (both sides must match: same build).
- Parameter block from `0x1408260D0`: +0x24 = SP or tutorial, +0x25 = client +0x415, +0x28 = client
  +0x416, +0x29 = the level's flag at +0x42. Listeners of the start message may change +0x415/+0x416
  before this (`vtable +0x68` loop at the start of `0x1408260D0`).

## Online layer

- `OnlineSettings.Backend` / `PeerBackend` (+0x20/+0x24), enum `BackendType`: 0 `Lan`, 1 `Blaze`,
  2 `Peer`, 3 `Local`, 4 `Playground`. Runtime classes exist only for some: RTTI/vtables found for
  `LocalServerBackend` (string) and the Blaze/Origin/DirtySock presence backends; `LanServerBackend`
  appears only as `LanServerBackendData`, so the LAN server backend was probably removed.
- `BFOnlineLevelDescriptionComponent.ForceLocalServerBackend` (+0x14): a level can force the local
  backend (presumably the campaign levels).
- `fb::Twinkle::isLanMode` `0x141064CB0` (name from Ghidra): true when `Client.ServerIp` is longer than
  one character and both `Online.ClientIsPresenceEnabled` and `Online.ServerIsPresenceEnabled` are
  false. The UI then turns off videos and the Origin overlay (`0x141064680`). So a "LAN game without
  presence" UI mode still exists.

## Player admission: what the server checks (2026-11-05, static)

Short answer: **the engine-level server does not check an EA login.** A joining client is admitted on
capacity, a ban list of machine ids and the build; the player name is whatever the client sends. The
open part is the online "server backend" a hosted game runs on (next section).

1. **Connect packet** (client `0x14083E860`): a machine id string (`0x140842030`: from a system call,
   `"UNIQUEID"` when it fails), the djb2 hash of the build string (`0x14063F8D0`, big-endian), one 0
   byte. No token, ticket or persona. The server password goes to the transport separately
   (`0x140847A70` → `0x140AE2CA0`, client +0x3B8).
2. **Accept** (server `0x140A0D920`): `SecureReason` 3 `ServerFull` when connections ≥ `+0x4240` or
   players ≥ max; 5 `Banned` when the machine id is in the list at `+0x41F0`; 6 `GenericError` on a
   malformed packet. Otherwise a `ServerConnection` is created (factory `+0x4290`, ctor `0x140A02C80`),
   machine id stored at connection `+0x2428`, `+0x2448` = build hash matches.
3. **Create player** (`0x140A0DF10`, on `NetworkCreatePlayerMessage` `0xD204CF07`): name = `msg+0x40`
   from the client (empty → `"Player"` + id). The client takes it from a provider at its connection
   +0x2818, fallback `"Player"` (`0x1408A1750`). Rejections only for capacity and spectator slots
   (`SecureReason` 3, 35, 36, 37). Then `ServerPlayerCreatedForConnection`.
4. **"Authenticated"** (`0x14013C1D0`, on that message): copies the connection's identity into the
   player and posts `SessionPlayerAuthenticatedMessage` (its only other listener is the script-event
   logger `0x1409D3C50`). It is skipped entirely when there is no server backend (`[0x142788AE0]` null).
5. `TinyEvent_AckAuthentication`/`NackAuthentication` (5/6) are never sent: the client sends events
   1, 2, 4, 7, 12, 13 (`0x1408A4680`), the server 8 and 10.
6. **Disconnect reasons in use** (`0x140A0D010`, 15 callers): PunkBuster (24), Kicked (4: commander
   flag while spawning, wrong team, admin kick by name), InteractivityTimeout (20), RankTooHigh (19,
   opt-in), GameTimeAccountOnUnofficial (42: only with `ServerSettings.TrialAccountOnUnofficialKick`,
   a non-`OFFICIAL` server and a `trial_access` entitlement), TeamKills (22), commander (39/40).
   **No caller uses `NotVerified` (13) or `WrongPassword` (7).** (The many `+0x246C` writes in
   `0x1401F20A3` are a changed-fields list of another object, not reasons.)

## Server backend: which one a hosted game gets

The client executable has factories for only three server backends: **Local** (`0x1401361B0`, from
`LocalServerBackendData`, campaign), **Peer** (`0x1401362D0`, `PeerServerBackendData` with
`CreateParameters.PlayerCapacity`) and **Playground** (`0x140136090`). `Blaze`, `BFBlaze` and `Lan`
server backends exist only as data types: their code is in EA's dedicated-server build, not here.

Selector `0x1402E7B1A` (`r14` = start parameters from `0x1408260D0`; `L` = the level's
`BFOnlineLevelDescriptionComponent`: +0x10 `ForcedPersistenceGameType`, +0x14 `ForceLocalServerBackend`,
+0x15 `ForcePersistenceGameType`):

| Condition (in order) | Backend |
| --- | --- |
| params +0x25 (local only), +0x24 (SP/tutorial) or `L.ForceLocalServerBackend` | 3 `Local` (4 `Playground` if `ForcePersistenceGameType` and type 2) |
| params +0x28 | `OnlineSettings.PeerBackend` (+0x24) |
| params +0x26 and `OnlineSettings.ServerIsPresenceEnabled` (+0xC5) | `OnlineSettings.Backend` (+0x20) |
| otherwise | 0 `Lan` (no factory in the client) |

`PeerServerBackend` (init `0x1402E6FB0`) talks to the client's online layer through
`PeerServerBackendRequestMessage`/`Response` (sender/handler `0x14044A1B0`, `0x1401785C0`,
`0x140441C20`); that is most likely Blaze game registration and was not followed further.

**Consequence:** the campaign's backend, `LocalServerBackend`, needs no EA service. Its choice is
independent of the listen address: `+0x25` (local only) selects both Local and `127.0.0.1`, but
`ForceLocalServerBackend` selects Local alone. So a hosted game with the level's
`ForceLocalServerBackend` set in memory (a data write, like the mod's other settings) should run
on the campaign backend and still listen on all interfaces. Not tested.

## Live check in the campaign (2026-11-05, read-only, pipe `peek`)

Campaign checkpoint, mod loaded, nothing written:

| What | Read | Matches |
| --- | --- | --- |
| Server backend `[0x142788AE0]` | vtable `0x141E0B5B0` = `LocalServerBackend` | yes |
| `OnlineSettings` (`[0x142364BE0]`) | `Backend` Blaze, `PeerBackend` Peer, `Environment` 3 (Production), both presence flags 1 | (so a hosted game without `ForceLocalServerBackend` would pick Peer or Blaze) |
| `NetworkSettings` | `TitleId` "5", `ClientPort` 25100, `ServerPort` 25200 | – |
| `ClientSettings.ServerIp` | empty | yes |
| Client (`[0x142668F98]`, +0x30 = ClientSettings) | +0x40C 0 `SinglePlayer`, +0x415 1 (local), +0x416 0, +0x3C0 empty | yes |
| Server peer (`[server context 0x142714930]+0x68`) | max connections 70, one connection (the own client); peer +0x4244..+0x4247 = 0, 1, 0, 1 (+0x4247 local) | yes |
| That connection | machine id = a GUID string (hardware profile GUID, struct layout of `GetCurrentHwProfileA`), build match 1, +0x2468 1 (local), reason 0, player set | yes |
| Sockets of bf4.exe | **no UDP socket on 25200**; one UDP socket on another port (all interfaces); TCP to EA (`159.153.x.x:10065`, likely Blaze) and to `127.0.0.1:3216` (EA app LSX) | local server is in-process |

**Menu → campaign is a level change, not a new game.** From the main menu, Campaign › Resume loaded
`Levels/SP/SP_Prologue/SP_Prologue` while `GameSettings.Level` stayed `Levels/Frontend/Frontend`
(polled every 5 s through the load). The front end already runs on the local in-process server
(`LocalServerBackend`); a campaign start asks that server to load another level. So the Startup
decision (SP / hosted / join, server backend, local flag) happens once, at game start, for the front
end; a hosted game for other PCs would need the server started non-local at boot, while an MP map on
the *existing* local server only needs the level-change request (not yet found; candidates
`NetworkRequestLoadLevelMessage`, `ServerLoadLevelMessage`, `ClientLoadLevelMessage` `0xB605BF07`
whose only consumer found so far is the script-event logger `0x140823580`).

**The main menu is a hosted game** (live, fresh start with the EA app offline, 2026-11-05): in the
front end the server backend is `PeerServerBackend` (vtable `0x141BCFE20`), the server peer is not
local (+0x4247 0), the client's game type is 1 `Hosted`, +0x415 0, +0x416 1, and bf4.exe has a UDP
socket on `0.0.0.0` (port 49913 that run). Campaign › Resume replaces this with an in-process
`LocalServerBackend` server (peer local 1). So the stock game runs its hosted path at every start.

**Telemetry in offline mode:** with the EA app offline, loading the campaign still opened a TCP
connection to `gostelemetry.blaze3.ea.com` (`159.153.51.30:9988`), closed again within ~20 s. No
other connection outside `127.0.0.1` (EA app LSX on 3216). See SAFETY.md rule 1 exception.

**The game's telemetry switch does not stop that connection.** The profile option `EnableTelemetry`
(read with `0x140841F40(store, "EnableTelemetry", 1)` on `ProfileOptionsSettingsLoadedMessage`)
sets +0x190A of the metrics object `[0x142378C00]`; `0x14015D900` then sends the DirtySock telemetry
client (+0xA80, opened with `'open'`) `'halt'` = !flag, but only in country mode 2 (+0x1904; modes
0/1 force halted/running by locale lists, `0x14015E010`). Live: with the mod holding the flag at 0
from start-up (mode 2, +0x1909 0 = halt sent), BF4 still opened `159.153.51.30:9988` in the main
menu (2026-11-05). Halt likely stops queued events, not the connection or its handshake. Only a
firewall block of bf4.exe keeps it from reaching EA.

**One-PC MP map test (2026-11-05, EA app offline, bf4.exe firewalled from public addresses).**
`pipe startlevel Levels/MP/MP_Prison/MP_Prison TeamDeathMatch0` from the campaign (LocalServerBackend):

1. First try: crash 8 s later, access violation executing address 0 on the main thread. The MP level
   makes the client restart the server; the backend is created again through the virtual
   `0x140137F40` → selector `0x1402E7B10` (function start; `0x1402E7B1A` is its unwind chunk) →
   `0x140DAB490`, which calls the factory table `0x1427891C8[BackendType]`. The selector returned
   0 `Lan` (start parameters +0x24/+0x25/+0x28 all 0), and the client has no Lan factory: slot 0 is
   null. None of the levels read carries a `BFOnlineLevelDescriptionComponent` (MP_Prison: UI, Memory,
   VeniceMatchmaking, VeniceGameplay, OnlineRichPresence, Water, BFPersistenceConfiguration), so
   `ForceLocalServerBackend` cannot be set per level.
2. With a research hook on the selector (Lan → Local after a `startlevel`): no crash; the server
   restarted on `LocalServerBackend`, and 1.5 min later the client went back to the front end with
   **"The connection with EA Online was lost."** (Peer backend again in the menu). The client
   refuses an MP map without an EA Online (Blaze) session. No connection to a public address was
   seen in either try.
3. Unrelated slow loads seen once with the firewall block (front end 7.5 min, campaign > 10 min)
   did not repeat in three later starts (front end 9-15 s, campaign 25-41 s); cause unknown.

Next question: where the client checks for the EA Online session when an MP level loads, and what
it does in that case.

**How a level change is requested** (static + live, 2026-11-05):

- UI "start level": `0x1403D8270(UILevelComp*, const char* level, const char* gameMode)`. It finds
  the level in the `LevelListReport` (comp +0x80, entries +0x18 name, +0x40 `IsMultiplayer`), sets
  the campaign difficulty for SP levels, builds a `NetworkRequestLoadLevelMessage` (`LevelSetup`:
  name, difficulty, inclusion option `GameMode=<gameMode>` when given) and sends it to the server,
  then posts `ClientLoadLevelRequestedMessage`. Its Lua-side caller `0x1403AEF71` gets the component
  from the UI manager (`[0x142672E18]`, vfunc +0x198 with the `UILevelCompData` TypeInfo `0x142BE4EF0`).
- Server: `0x1409D13B0` handles that request: only from player 1 (the host) and when peer +0x4248
  is 0, then `0x1409D54F0` (load level: `ServerLoadLevelMessage` with a copy of the `LevelSetup`).
- `LevelListReport` (cached `[0x14267B0A0]`, live): 42 entries, the MP maps among them
  (`Levels/MP/MP_Prison/MP_Prison`, `MP_Siege`, `XP0/Levels/MP_007/XP0_Caspian`, …) with
  `IsMultiplayer` 1. So `0x1403D8270` accepts MP maps.
- **One-PC test plan:** from the main menu, call `0x1403D8270(comp, "Levels/MP/MP_Prison/MP_Prison",
  "TeamDeathMatch0")` on the game thread. The front end's local server (`LocalServerBackend`,
  in-process, no socket) should load the MP map; no EA game server is involved. Needs a research
  pipe command and the safety-rule change first.

Takeaways: the campaign client stays logged in to EA (Blaze TCP) even in single player, so a hosted
test would also run with that connection open. A non-local server (+0x25 = 0) would open a UDP socket
on 25200; that is what a second PC connects to.

## Open questions (the go/no-go)

1. **Does a hosted MP game run on `LocalServerBackend` with remote players?** (Statically nothing
   checks an EA login; see above.) Also: does the *joining* client try a Blaze login or presence
   before or after `ClientJoinMultiplayer`, and does it give up without one?
2. Does the hosted path still work end to end? It may depend on data or on code removed from retail.
3. ~~Where the join message writes +0x3C0~~: `0x1408210A0` (above).
4. Loadouts and unlocks without EA's stats (`SyncedBFSettings.AllUnlocksUnlocked`).
5. PunkBuster: `0x14000B120` touches `pb/pbns.dat` only when the server flag (`this+0x20`) is set. A
   private hosted game would have to keep PunkBuster off (it is uninstalled here, SAFETY.md rule 5).

## Before any live test

Every live step needs `docs/SAFETY.md` rule 1 ("campaign only") and `src/safety/mp_guard.cpp` changed
first, on purpose: allow a hosted or direct-connect game only on private addresses, never Blaze
matchmaking (`State_ConnectToGameId`, `State_ClaimReservation`, `-webmode MP` stay blocked). That is
the user's decision. The campaign-safe read-only check is done (section above).
