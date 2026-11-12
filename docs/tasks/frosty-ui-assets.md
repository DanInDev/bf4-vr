# Battlefield 4 UI art for the mod's screens (Frosty survey)

Goal: find out which of BF4's own UI textures (HUD frames, panel backgrounds, icons, loading art) are
worth using in the mod's screens, starting with the body calibration step (#50). This is the survey
only: nothing is extracted into the repository, and the mod does not load anything yet.

Binding: [SAFETY.md](../SAFETY.md) rule 7. Game assets may be read from your own install and used by
the mod; exports go to `re/assets/` (git-ignored) and are never committed, posted or shipped. Frosty
runs only with Battlefield 4 closed, and never while Battlefield 6 or an EasyAntiCheat game runs.

## What the owner does (about 30 minutes)

1. **Install Frosty Tool Suite** from its GitHub releases (github.com/CadeEvs/FrostyToolsuite). It
   needs .NET Framework 4.8. [FINDINGS.md](../FINDINGS.md) notes that it lists BF4 as fully supported.
2. **Start Frosty Editor**, add the game: `E:\EA\Battlefield 4\bf4.exe`. The first start builds a cache
   of the game's archives (several minutes); let it finish.
3. **Browse the Data Explorer** and look in the UI folders (names to try: `UI/`, `UI/Art/`,
   `UI/Static/`, `UI/Flow/`, anything with `HUD`, `Loading`, `Frontend`, `Icons`). Open textures
   to preview them.
4. **Export candidates** (right click, Export, PNG or DDS) to
   `C:\Users\dnlni\Desktop\hobby\bf-vr\re\assets\bf4-ui\`, one subfolder per kind (`backgrounds`, `frames`,
   `icons`, `fonts`). For each export, keep Frosty's asset path: export into a folder named after it
   or note it in `re/assets/bf4-ui/paths.txt`. The mod will need that path to find the asset later.
5. Tell the agent the folder is filled.

## What to look for

| For | Wanted |
| --- | --- |
| Calibration backdrop | A dark panel or frontend background that reads behind white text |
| Frames | Bracket corners, slanted header bars, the frontend's panel edges |
| Icons | A soldier silhouette, controller or trigger hints, a check mark, a progress or loading glyph |
| Font | Whether the UI font exists as a separate file or only inside the Scaleform (GFx) movies |

## What the agent does with the survey

- Pick the few assets that clearly beat the drawn ones, and note their sizes and formats.
- Decide how the mod gets them at run time from the player's own install: a one-time extractor in
  the launcher, or a reader for the archive files (superbundles: `.cas`/`.cat`). Either way it is our
  own code, and the art stays on the player's PC.
- Keep the drawn screen (ui/calibration_screen) as the fallback for a missing or changed asset.
