# HUD widgets — BF4 build 179547

Status: **element toggles shipped (#28, 2026-11-04).** The HUD is a set of widget entities; each
one can be hidden at its source through one hook on the shared `isVisible`. World markers (squad
names and triangles) are not widgets and are still open.

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
| Squad names and triangles over companions | **not a widget**: none of the 46 HUD widgets removes them (see below) |

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

## Open: world markers

Squad names and triangles (and probably the objective's world marker) come from the world-icons
screen: `UIWorldIconsScreenData` (margins, `DrawDeathFlowIcons`), `UIWorldIconCompData`,
`BFUIWorldIconBehavior`. The runtime screen class is not reflected. Next step: find the screen's
per-frame icon loop (read-watch an icon field, or REA cross-references from `UIWorldIconsScreenData`)
and gate it the same way.

## Dead end

`frametrace` with the eye pass running ends at the UI render's first clear, so the HUD draws are not
in it. Skipping draws at the D3D level would also be fragile, since Twinkle batches them.
