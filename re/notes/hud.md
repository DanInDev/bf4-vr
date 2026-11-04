# HUD widgets — BF4 build 179547

Status: **element toggles shipped (#28, 2026-11-04).** The HUD is a set of widget entities; each
one can be hidden at its source through one hook on the shared `isVisible`, the world markers (squad
names and triangles) too, by their owning screen.

## Widgets

The HUD screen is built from `UIWidgetEntity` subclasses (`tools/typeinfo_dump.py --find Widget`,
about 190 classes). A live widget:

```
widget   +0x00 vtable (runtime class, named by the typeinfo dump)
         +0x28 data: UI<Class>WidgetData (UIWidgetEntityData: +0x38 VisibilityDataSource,
               +0x60 Visible, then the class's own fields)
         +0x118 its UIElementWidgetReferenceEntity
reference (UIElementWidgetReferenceEntity, 0xB0 bytes, vtable 0x141D24C40)
         +0x58 visible byte (written by vtable slot 14 when visibility changes, not per frame)
         +0x90 the widget
```

The reference entities of one HUD lie in **one contiguous array, 0xB0 apart**, with a few other
entities in between. Walking it from the crosshair's reference (scratch script, 2026-11-04) listed every
HUD widget of the Baku checkpoint. Clearing a widget's data `Visible` (+0x60) with `poke` hides it
within one frame, and setting it back shows it again.

| Element on screen (2160x2160 HUD capture) | Class |
| --- | --- |
| Crosshair (4 ticks, middle) | `UICrosshairWidget` |
| Minimap panel incl. objective text, bottom left | `UIHudBackgroundWidget` (one of six) with `UIMinimapBaseWidget`, `UIMinimapVisualWidget` (2), `UIMinimapObjectiveWidget` (the text), `UIPlayerFrustumWidget`, `UIMinimapIconsWidget` (2), `UICompassWidget`, `UIAirRadarWidget` |
| Ammo panel, bottom right | `UIHudBackgroundWidget` with two plain `UIWidgetEntity` pieces |
| Weapon and stance icons, bottom middle | `UIHudBackgroundWidget` + `UIHudInventoryWidget` |
| Nothing on screen at the checkpoint (named only) | `UIHitIndicatorWidget`, `UIReceiveDamageIndicatorWidget`, `UIObjectiveWidget`, `UIObjectiveHintWidget`, `UIInteractionWidget`, `UIScoringWidget`, `UIAwardPresentationWidget`, `UISubtitlesWidget`, `UISquadListWidget`, `UISoldierStatusWidget`, … |
| Squad names and triangles over companions | Not in the HUD screen: the world icons screen's root `UIWidgetEntity` (see below) |

Method: for each widget, HUD capture (`hudshot`, simulated head), `Visible` = 0, capture, restore.
Then the pixels whose alpha dropped were compared. Whole-frame shots are useless for this because the
world keeps moving.

## isVisible: the hook point

- Read watch (`watch <data+0x60> 1 1000 rw`, new `rw` mode): the only reader of `Visible` is
  `0x140875D53` inside **`0x140875D30`**, about once per frame (138 reads/s at 138 fps).
- `0x140875D30` is vtable **slot 30** of `UIWidgetEntity` (`isVisible`). It looks the `Visible`
  property up through the widget's bindings (`+0xA8`), returns false when it is 0, and otherwise
  evaluates the data's `VisibilityDataSource` (`+0x38`, `0x1408BC590`).
  Prologue `40 53 48 83 EC 20 48 83 79 20 00 48 8B D9 74 20`.
- The caller `0x14086EEAE` (`call [rax+0xF0]`) walks the screen's widgets each frame and **skips the
  widget when it answers false**, which means no update and no draw.
- 155 of the 186 widget classes inherit slot 30 unchanged (vtable scan of the dump). Overrides include
  `UISubtitlesWidget` (`0x1403E6A10`: its own flags +0x154/+0x160), `UISoldierStatusWidget`,
  `UITooltipWidget`, `UIMinimapVisualWidget` (calls the base first), and menu widgets.

`ui/hud_widgets.cpp` hooks `0x140875D30`. It calls the original, then answers false for widgets
whose vtable belongs to a hidden element (`ui/hud_widget_table.cpp`). Before installing, it checks
the prologue and that slot 30 of every listed vtable is still the base function. Cost: about 50
widgets per frame, one table scan each. Live counters come from pipe `hudwidgets`.

Verified on the monitor (Release, 2026-11-04): with all elements shown, no false answers. Hiding any
one element answers false once per frame per class (twice for objectives and score, two classes
each). The crosshair's 484 opaque HUD pixels in the middle drop to 0. Hit markers, damage arcs,
objective markers, prompts and score pop-ups need gameplay to appear, so they are a headset check
(`docs/HEADSET-TESTS.md` H1–H3).

## World markers (squad names and triangles)

They are not drawn by the HUD screen. They belong to the **world icons screen** (C++ class, not
reflected; vtable **`0x141BFBAD0`**, slot 0 `0x1408C9BD0`; its `+0x10` holds the
`UIWorldIconsScreenData` asset named `UI/CppScreens/WorldIconsScreen`). That screen draws its
icons under one plain `UIWidgetEntity` root widget (data followed by the string
`UI/CppScreens/WorldIconsScreenRoot.Widgets`). The root asks `isVisible` twice per frame.

How they were found (2026-11-04):
- The research mode `hudwidgets classes` lists every vtable that asks `isVisible` in one second (23 classes,
  about 19 plain `UIWidgetEntity` answers per frame). `hudwidgets instances 141d1bbf0` lists the plain
  ones with their data.
- Clearing `Visible` on each instance's data: one removed every squad marker (632 green HUD pixels
  → 0, about 1,800 HUD pixels in all), and one blanked the whole HUD (the HUD root).
- The widget's owner is at **widget `+0xA0`** (base `isVisible` dereferences it unconditionally): the
  marker root's owner has vtable `0x141BFBAD0`, the crosshair's has `0x141BFB5D0` (HUD screen).
- The screen data's margins (`+0x30`) and `DrawDeathFlowIcons` are not read per frame (read watch);
  the screen copies them when it is created.

`hud_show_markers = 0` hides every widget whose owner screen has that vtable. Monitor: markers
636 → 0 → 606 green pixels (off, then on again), the rest of the HUD unchanged. All world icons go with it:
squad markers for sure. Objective markers and grenade warnings, if they are world icons, have not been
seen yet (headset row H4).
## Dead end

`frametrace` with the eye pass running ends at the UI render's first clear, so the HUD draws are not
in it. Skipping draws at the D3D level would also be fragile, since Twinkle batches them.
