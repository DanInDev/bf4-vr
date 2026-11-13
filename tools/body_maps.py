"""Writes the player-facing maps in docs/img: grab zones on the body and the Quest 3 controller layout.

    python -E -P tools\\body_maps.py

The zone offsets are copied from src/weapon/holsters.hpp and src/weapon/body_rig.hpp (body frame,
metres from the point between the eyes: x toward the gun side, y up, z back) and the bindings from
src/xr/controller_actions.cpp, src/input/vr_controls.cpp and the weapon modules. Run it again when
those change. Both SVGs follow the viewer's light or dark theme.
"""

from pathlib import Path

OUT = Path(__file__).resolve().parent.parent / "docs" / "img"

STYLE = """
<style>
  svg { --bg:#f7f8f4; --fg:#1d231f; --muted:#5d665f; --line:#a8b0a6; --body:#d5dacf; --body-line:#6f786f;
        --gun:#c2541b; --off:#2367a3; --both:#7b4aa3; --mod:#2f7d4f; --plate:#2b302c; --plate-line:#596059; }
  @media (prefers-color-scheme: dark) {
    svg { --bg:#1c211d; --fg:#e3e7df; --muted:#9aa39b; --line:#3d453f; --body:#2b322d; --body-line:#8c968d;
          --gun:#f08a4b; --off:#63a6e6; --both:#b48ae0; --mod:#62c08a; --plate:#3a413b; --plate-line:#80897f; }
  }
  .bg { fill:var(--bg); }
  .title { font:700 92px 'Barlow Condensed','Arial Narrow',sans-serif; fill:var(--fg); }
  .sub { font:400 44px 'Segoe UI',system-ui,sans-serif; fill:var(--muted); }
  .head { font:600 56px 'Barlow Condensed','Arial Narrow',sans-serif; fill:var(--fg); letter-spacing:2px; }
  .axis { font:400 36px Consolas,monospace; fill:var(--muted); }
  .note { font:400 42px 'Segoe UI',system-ui,sans-serif; fill:var(--muted); }
  .grid { stroke:var(--line); stroke-width:2; stroke-dasharray:10 14; }
  .floor { stroke:var(--line); stroke-width:6; }
  .part { fill:var(--body); stroke:var(--body-line); stroke-width:6; stroke-linejoin:round; }
  .visor { fill:var(--fg); }
  .lens { fill:var(--muted); opacity:.55; }
  .strap { fill:none; stroke:var(--fg); stroke-width:14; stroke-linecap:round; }
  .side { font:600 50px 'Barlow Condensed','Arial Narrow',sans-serif; fill:var(--muted); letter-spacing:3px; }
  .gun { stroke:var(--gun); fill:var(--gun); }
  .off { stroke:var(--off); fill:var(--off); }
  .both { stroke:var(--both); fill:var(--both); }
  .mod { stroke:var(--mod); fill:var(--mod); }
  .game { stroke:var(--muted); fill:var(--muted); }
  .reach { fill-opacity:.16; stroke-width:7; }
  .lead { fill:none; stroke-width:3; }
  .zl { font:600 64px 'Barlow Condensed','Arial Narrow',sans-serif; stroke:none; }
  .zs { font:400 36px Consolas,monospace; fill:var(--muted); stroke:none; }
  .plate { fill:var(--plate); stroke:var(--plate-line); stroke-width:6; }
  .shell { fill:var(--body); stroke:var(--body-line); stroke-width:6; stroke-linejoin:round; }
  .key { fill:var(--bg); stroke:var(--fg); stroke-width:6; }
  .keyt { font:700 40px 'Segoe UI',system-ui,sans-serif; fill:var(--fg); text-anchor:middle; }
  .bl { font:600 62px 'Barlow Condensed','Arial Narrow',sans-serif; fill:var(--fg); }
  .bd { font:400 46px 'Segoe UI',system-ui,sans-serif; fill:var(--muted); }
  .dot { stroke:none; }
</style>
"""

# name, where, hand, x, y, z, reach (metres)
# Grab zones v2 (#57): primary at the sling, gadgets over the shoulders, magazines from the low chest
# down to the off-side hip (one capsule, drawn as its two ends).
ZONES = [
    ("Gadget 1", "over the shoulder", "both", 0.20, 0.05, 0.15, 0.155),
    ("Gadget 2", "over the shoulder", "both", -0.20, 0.05, 0.15, 0.14),
    ("Grenades", "high chest", "off", 0.11, -0.27, -0.10, 0.10),
    ("Knife", "high chest", "gun", -0.10, -0.27, -0.10, 0.11),
    ("Magazines", "low chest", "off", -0.04, -0.47, -0.10, 0.13),
    ("Secondary", "hip", "gun", 0.22, -0.75, 0.0, 0.14),
    ("Magazines", "down to the hip", "off", -0.22, -0.75, 0.0, 0.13),
    ("Primary", "at the sling", "gun", 0.10, -0.50, -0.12, 0.14),
]


def mm(v):
    return int(round(v * 1000))


def grab_zones():
    parts = []
    add = parts.append
    # Front plate origin (eyes) at (1450, 820); side plate origin at (3900, 820).
    fx, fy = 1450, 820
    sx, sy = 3900, 820
    width, height = 4700, 2900
    add(f'<rect class="bg" width="{width}" height="{height}" rx="24"/>')
    add('<text class="title" x="80" y="140">Grab zones around the body</text>')
    add('<text class="sub" x="80" y="210">bf4-vr, right-handed, to scale on a 1.78 m player. Circles are the real reach. '
        'Squeeze the grip inside one.</text>')
    lx = 80
    for cls, text in (("gun", "Gun hand"), ("off", "Off hand"), ("both", "Either hand")):
        add(f'<circle class="{cls} reach" cx="{lx + 22}" cy="282" r="22"/>')
        add(f'<text class="bd" x="{lx + 60}" y="296">{text}</text>')
        lx += 330

    # Front view: the player faces the viewer, so their right (gun side) is on the left.
    g = [f'<g transform="translate({fx} {fy})">']
    g.append('<text class="head" x="-1300" y="-380">FRONT</text>')
    g.append('<text class="axis" x="-1060" y="-380">facing you: their right (gun side) is on your left</text>')
    for h in (0.5, 1.0, 1.5):
        y = mm(1.66 - h)
        g.append(f'<line class="grid" x1="-1300" x2="1300" y1="{y}" y2="{y}"/>')
        g.append(f'<text class="axis" x="-1300" y="{y - 12}">{h:.1f} m</text>')
    g.append('<line class="floor" x1="-1300" x2="1300" y1="1660" y2="1660"/>')
    g.append('<path class="part" d="M -60 1640 L -165 1640 L -175 800 L 175 800 L 165 1640 L 60 1640 L 30 950 L -30 950 Z"/>')
    g.append('<path class="part" d="M -205 195 Q -230 500 -150 640 L -180 810 L 180 810 L 150 640 Q 230 500 205 195 Z"/>')
    g.append('<path class="part" d="M -200 180 L -860 180 Q -900 220 -860 260 L -200 270 Z"/>')
    g.append('<path class="part" d="M 200 180 L 860 180 Q 900 220 860 260 L 200 270 Z"/>')
    g.append('<ellipse class="part" cx="-915" cy="220" rx="55" ry="48"/>')
    g.append('<ellipse class="part" cx="915" cy="220" rx="55" ry="48"/>')
    g.append('<rect class="part" x="-38" y="110" width="76" height="95"/>')
    g.append('<ellipse class="part" cx="0" cy="10" rx="82" ry="122"/>')
    g.append('<path class="strap" d="M -86 -10 Q -95 -150 0 -150 Q 95 -150 86 -10"/>')
    g.append('<rect class="visor" x="-108" y="-48" width="216" height="92" rx="30"/>')
    g.append('<ellipse class="lens" cx="-48" cy="-2" rx="30" ry="22"/>')
    g.append('<ellipse class="lens" cx="48" cy="-2" rx="30" ry="22"/>')
    g.append('<text class="side" x="-915" y="130" text-anchor="middle">GUN HAND</text>')
    g.append('<text class="side" x="915" y="130" text-anchor="middle">OFF HAND</text>')
    for name, where, hand, x, y, _z, r in ZONES:
        cx, cy, rr = -mm(x), -mm(y), mm(r)
        out = -1 if cx < 0 else 1
        lx = out * (600 if y > -0.4 else 520)
        ly = cy + 100 if name in ("Grenades", "Knife") else cy + 40 if name == "Magazines" else cy
        anchor = "end" if out < 0 else "start"
        g.append(f'<g class="{hand}">')
        g.append(f'<circle class="reach" cx="{cx}" cy="{cy}" r="{rr}"/>')
        g.append(f'<circle class="dot" cx="{cx}" cy="{cy}" r="12"/>')
        g.append(f'<line class="lead" x1="{cx + out * rr}" y1="{cy}" x2="{lx - out * 14}" y2="{ly - 20}"/>')
        g.append(f'<text class="zl" x="{lx}" y="{ly}" text-anchor="{anchor}">{name}</text>')
        g.append(f'<text class="zs" x="{lx}" y="{ly + 44}" text-anchor="{anchor}">{where}, {round(r * 100)} cm</text>')
        g.append('</g>')
    g.append('</g>')
    parts.extend(g)

    # Side view from the gun side: the player faces left, back is to the right (svg x = z).
    s = [f'<g transform="translate({sx} {sy})">']
    s.append('<text class="head" x="-760" y="-380">SIDE</text>')
    s.append('<text class="axis" x="-600" y="-380">from the gun side, facing left</text>')
    for h in (0.5, 1.0, 1.5):
        y = mm(1.66 - h)
        s.append(f'<line class="grid" x1="-760" x2="740" y1="{y}" y2="{y}"/>')
    s.append('<line class="floor" x1="-760" x2="740" y1="1660" y2="1660"/>')
    s.append('<line class="grid" x1="0" x2="0" y1="-300" y2="1660"/>')
    s.append('<text class="axis" x="12" y="-300">eyes</text>')
    s.append('<text class="axis" x="-760" y="-300">&#8592; forward</text>')
    s.append('<path class="part" d="M 0 800 L 150 800 L 140 1640 L 30 1640 L -110 1650 L -110 1600 L -10 1590 Z"/>')
    s.append('<path class="part" d="M 0 200 Q -70 330 -55 500 Q -40 650 -10 810 L 170 810 Q 200 600 190 420 Q 185 260 170 200 Z"/>')
    s.append('<ellipse class="part" cx="80" cy="230" rx="60" ry="60"/>')
    s.append('<rect class="part" x="50" y="110" width="70" height="100"/>')
    s.append('<ellipse class="part" cx="75" cy="10" rx="108" ry="122"/>')
    s.append('<rect class="visor" x="-55" y="-50" width="90" height="95" rx="26"/>')
    s.append('<path class="strap" d="M 20 -20 L 175 -10"/>')
    spots = [
        (0, ["Gadget 1", "Gadget 2"], "both", 1),
        (2, ["Grenades", "Knife"], "off", -1),
        (4, ["Magazines", "Primary (sling)"], "off", -1),
        (5, ["Secondary", "Magazines"], "gun", 1),
        (7, [], "gun", 1),
    ]
    for index, names, hand, side in spots:
        _n, _w, own, _x, y, z, r = ZONES[index]
        cx, cy, rr = mm(z), -mm(y), mm(r)
        lx = cx + rr + 20 if side > 0 else cx - rr - 20
        anchor = "start" if side > 0 else "end"
        s.append(f'<g class="{own}">')
        s.append(f'<circle class="reach" cx="{cx}" cy="{cy}" r="{rr}"/>')
        s.append(f'<circle class="dot" cx="{cx}" cy="{cy}" r="12"/>')
        for i, label in enumerate(names):
            ty = cy - (len(names) - 1) * 32 + i * 66 + 20
            s.append(f'<text class="zl" x="{lx}" y="{ty}" text-anchor="{anchor}">{label}</text>')
        s.append('</g>')
    s.append('</g>')
    parts.extend(s)

    notes = [
        "Zones are measured from between the eyes and turn with the head only once it is 34° off the body, "
        "so looking down at a pouch does not move it.",
        "The rifle is taken where it hangs on its sling; the magazines reach from the belly down to the off-side hip.",
        "Shoulder zones are 15 cm behind and 5 cm above the eyes: reach back over the shoulder. "
        "grab_debug = 1 draws every zone as a ring in the headset.",
        "Also on the gun: the magazine and charging handle (off hand, physical reload), the handguard "
        "(two-handed hold) and the pin of a held grenade (gun hand).",
    ]
    for i, line in enumerate(notes):
        parts.append(f'<text class="note" x="80" y="{2650 + i * 56}">{line}</text>')

    return svg(width, height, "Grab zones around the body", parts)


def controller(cx, cy, left, scale=1.6):
    """A Quest 3 Touch Plus controller seen from above, schematic (no tracking ring on Quest 3).

    Drawn in local units around the face's centre, then scaled. Returns the parts and, per control,
    its centre and radius on the canvas so callouts can stop at its edge.
    """
    m = -1 if left else 1  # mirrored: the stick sits toward the other hand, the face buttons outward
    p = [f'<g transform="translate({cx} {cy}) scale({scale})">']
    # Handle: drops from the face, leaning toward the other hand.
    p.append(f'<path class="shell" d="M {-m * 150} 60 Q {-m * 170} 300 {-m * 95} 520 Q {-m * 20} 590 {m * 60} 540 '
             f'Q {m * 120} 320 {m * 150} 80 Z"/>')
    p.append('<ellipse class="shell" cx="0" cy="0" rx="230" ry="205"/>')
    p.append('<ellipse class="plate" cx="0" cy="0" rx="190" ry="165"/>')
    controls = {}
    stick = (-m * 70, -30)
    p.append(f'<circle class="key" cx="{stick[0]}" cy="{stick[1]}" r="72"/>')
    p.append(f'<circle class="key" cx="{stick[0]}" cy="{stick[1]}" r="34"/>')
    controls["stick"] = (stick, 72)
    upper = (m * 92, -78)
    lower = (m * 112, 32)
    for (kx, ky), text in ((upper, "Y" if left else "B"), (lower, "X" if left else "A")):
        p.append(f'<circle class="key" cx="{kx}" cy="{ky}" r="40"/>')
        p.append(f'<text class="keyt" x="{kx}" y="{ky + 14}">{text}</text>')
    controls["upper"] = (upper, 40)
    controls["lower"] = (lower, 40)
    system = (-m * 20, 105)
    p.append(f'<rect class="key" x="{system[0] - 30}" y="{system[1] - 16}" width="60" height="32" rx="16"/>')
    controls["system"] = (system, 32)
    # Trigger under the index finger on the front edge; grip under the middle finger on the handle.
    p.append('<path class="key" d="M -95 -196 Q 0 -262 95 -196" style="fill:none;stroke-width:16;stroke-linecap:round"/>')
    controls["trigger"] = ((0, -228), 12)
    p.append(f'<path class="key" d="M {-m * 163} 150 Q {-m * 175} 300 {-m * 118} 440" '
             'style="fill:none;stroke-width:16;stroke-linecap:round"/>')
    controls["grip"] = ((-m * 168, 300), 12)
    p.append('</g>')
    placed = {name: ((cx + x * scale, cy + y * scale), r * scale) for name, ((x, y), r) in controls.items()}
    return p, placed


def callout(parts, control, tx, ty, anchor, label, detail, cls):
    (kx, ky), radius = control
    ex, ey = tx + (-18 if anchor == "end" else 18), ty - 18
    dx, dy = ex - kx, ey - ky
    length = (dx * dx + dy * dy) ** 0.5 or 1.0
    sx, sy = kx + dx / length * (radius + 8), ky + dy / length * (radius + 8)
    parts.append(f'<g class="{cls}"><line class="lead" x1="{sx:.0f}" y1="{sy:.0f}" x2="{ex}" y2="{ey}" style="stroke-width:5"/>'
                 f'<circle class="dot" cx="{sx:.0f}" cy="{sy:.0f}" r="10"/><circle class="dot" cx="{ex}" cy="{ey}" r="7"/></g>')
    parts.append(f'<text class="bl" x="{tx}" y="{ty}" text-anchor="{anchor}">{label}</text>')
    for i, line in enumerate(detail):
        parts.append(f'<text class="bd" x="{tx}" y="{ty + 54 + i * 50}" text-anchor="{anchor}">{line}</text>')


def quest_controls():
    width, height = 4000, 2760
    parts = []
    add = parts.append
    add(f'<rect class="bg" width="{width}" height="{height}" rx="24"/>')
    add('<text class="title" x="80" y="140">Quest 3 controllers in bf4-vr</text>')
    add('<text class="sub" x="80" y="210">In a level, right-handed, default settings. Seen from above, schematic. '
        'In menus they act as an Xbox pad.</text>')
    lx = 80
    for cls, text in (("game", "The game's button"), ("gun", "Changed by the mod"), ("mod", "The mod's own")):
        add(f'<circle class="{cls} dot" cx="{lx + 18}" cy="286" r="18"/>')
        add(f'<text class="bd" x="{lx + 52}" y="300">{text}</text>')
        lx += 560

    left_parts, lk = controller(1330, 1030, True)
    right_parts, rk = controller(2670, 1030, False)
    parts.extend(left_parts)
    parts.extend(right_parts)

    L = 880
    callout(parts, lk["trigger"], L, 470, "end", "Trigger", ["off: raise the gun to", "your eye to aim · C4: detonate"], "gun")
    callout(parts, lk["upper"], L, 720, "end", "Y", ["fire mode"], "game")
    callout(parts, lk["stick"], L, 900, "end", "Stick · click", ["walk where you look", "click: sprint"], "game")
    callout(parts, lk["lower"], L, 1130, "end", "X", ["the game's menu"], "gun")
    callout(parts, lk["system"], L, 1310, "end", "Menu ≡", ["tap: pause", "hold: bf-vr panel"], "mod")
    callout(parts, lk["grip"], L, 1540, "end", "Grip", ["grab: magazines, grenades,", "handguard, gadgets, use"], "mod")

    R = 3120
    callout(parts, rk["trigger"], R, 470, "start", "Trigger", ["fire"], "game")
    callout(parts, rk["upper"], R, 680, "start", "B", ["magazine release, bolt catch", "(reload = button: reload)"], "gun")
    callout(parts, rk["stick"], R, 920, "start", "Stick · click", ["← → snap turn 30°", "↑ ↓ stand, crouch, prone", "click: melee"], "gun")
    callout(parts, rk["lower"], R, 1200, "start", "A", ["jump"], "game")
    callout(parts, rk["system"], R, 1360, "start", "Meta", ["Quest system menu"], "game")
    callout(parts, rk["grip"], R, 1560, "start", "Grip", ["draw from a holster,", "pull the grenade pin"], "mod")

    add('<text class="head" x="1330" y="1900" text-anchor="middle">LEFT · OFF HAND</text>')
    add('<text class="head" x="2670" y="1900" text-anchor="middle">RIGHT · GUN HAND</text>')

    add('<text class="head" x="80" y="2080">BOTH STICKS AND GESTURES</text>')
    rows = [
        ("Both stick clicks", "tap: mark a moment · hold 0.5 s: bf-vr panel"),
        ("Point the gun hand", "aims the gun; bullets leave the barrel"),
        ("Thrust the gun hand", "melee (knife stab)"),
        ("Gun up to the eye", "aim down the sights; scopes magnify"),
        ("Off hand ahead + grip", "use: pick up, doors, prompts"),
        ("Off hand on the handguard + grip", "two-handed hold"),
    ]
    for i, (what, does) in enumerate(rows):
        col, row = i % 2, i // 2
        x = 80 + col * 1960
        y = 2170 + row * 124
        add(f'<circle class="mod dot" cx="{x + 16}" cy="{y - 18}" r="16"/>')
        add(f'<text class="bl" x="{x + 50}" y="{y}">{what}</text>')
        add(f'<text class="bd" x="{x + 50}" y="{y + 50}">{does}</text>')
    add('<text class="note" x="80" y="2700">Left-handed (dominant_hand = left) swaps the controllers. right_stick = look brings '
        'back stick aiming; grips_to_game and ads_trigger pass the grips and left trigger to the game.</text>')
    return svg(width, height, "Quest 3 controllers in bf4-vr", parts)


def svg(width, height, title, parts):
    body = "\n".join(parts)
    return (f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {width} {height}" width="{width // 4}" '
            f'height="{height // 4}" role="img" aria-label="{title}">\n<title>{title}</title>{STYLE}\n{body}\n</svg>\n')


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    for name, text in (("grab-zones.svg", grab_zones()), ("quest3-controls.svg", quest_controls())):
        (OUT / name).write_text(text, encoding="utf-8", newline="\n")
        print("wrote", OUT / name)


if __name__ == "__main__":
    main()
