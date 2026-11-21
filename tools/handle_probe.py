"""Per-gun handle and magazine points from the game's own animations (docs/tasks/all-weapons.md, re/notes/weapon.md
"Charging handles, every crate gun"): the 11-03 AK-12 method, automated.

    python -E -P tools\\handle_probe.py record [--guns U_M416,U_AK12] [--event empty|shot] [--out folder] [--restore U_SCAR-H]
    python -E -P tools\\handle_probe.py analyse [--out folder] [--json results.json]

record: for each gun (given into the slot in hand with the own crate, pipe `givegun`, the old gun given back at the end;
without --guns, the gun in hand), reload = button
and arm_ik = 0 (the game's animated hands), then the event: `empty` sets the server magazine to 0 (pipe `reload setammo 0`,
research), so the game plays its empty reload; `shot` fires one shot (a bolt action or pump cycles after it). The local pose
is read as fast as the pipe allows; every frame that changed is saved (Wep_Root axes: +z along the barrel, +y up, +x left):
Wep_Slide, Wep_Mag, each hand's pinch (thumb tip and index middle) and its thumb-index spread. One file per gun and event
(default folder re/dumps/gun_points, git-ignored). Settings are restored at the end (reload physical, arm_ik 1).

analyse: from the saved frames, per gun:
  handle   - the hand riding the bolt: its pinch moved forward by the bolt's travel, over the steadiest run of frames
             (the longest run staying within 2 cm), left hand for the empty reload, either hand for a shot;
  magazine - the hand riding the magazine as it comes out: the pinch relative to Wep_Mag (the grab offset from the
             seated Wep_Mag), and the direction the magazine first moves (its way out).
"""
import argparse
import json
import math
import os
import re
import struct
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

from camera_probe import command, peek, pointer  # noqa: E402
from skeleton_probe import SOLDIER_GLOBAL, read_rig  # noqa: E402
from slot_probe import slots  # noqa: E402

ROOT = os.path.dirname(HERE)
DEFAULT_OUT = os.path.join(ROOT, 're', 'dumps', 'gun_points')


def qmul(a, b):
    ax, ay, az, aw = a
    bx, by, bz, bw = b
    return (aw * bx + ax * bw + ay * bz - az * by, aw * by - ax * bz + ay * bw + az * bx,
            aw * bz + ax * by - ay * bx + az * bw, aw * bw - ax * bx - ay * by - az * bz)


def qrot(q, v):
    x, y, z, w = q
    p = qmul(qmul(q, (v[0], v[1], v[2], 0.0)), (-x, -y, -z, w))
    return p[:3]


def local_pose_address(timeout=10.0):
    """The live local pose; right after a gun change the chain can point at nothing for a moment: wait for it."""
    deadline = time.time() + timeout
    while True:
        try:
            soldier = pointer(SOLDIER_GLOBAL)
            component = pointer(soldier + 0x1E8)
            animatable = pointer(component + 0x128)
            address = pointer(animatable + 0x48)
            if 0x10000 <= address < 0x7FF000000000 and address % 8 == 0:
                peek(address, 0x20)
                return address
        except RuntimeError:
            pass
        if time.time() > deadline:
            raise RuntimeError('no live first-person pose')
        time.sleep(0.25)


def model(parents, pose, wanted):
    """Model-space (position, rotation) of the wanted bones and their ancestors."""
    cache = {}

    def of(i):
        if i in cache:
            return cache[i]
        x, y, z, _s, qx, qy, qz, qw = pose[i]
        if parents[i] < 0:
            cache[i] = ((x, y, z), (qx, qy, qz, qw))
        else:
            pp, pq = of(parents[i])
            offset = qrot(pq, (x, y, z))
            cache[i] = ((pp[0] + offset[0], pp[1] + offset[1], pp[2] + offset[2]), qmul(pq, (qx, qy, qz, qw)))
        return cache[i]

    return {i: of(i) for i in wanted}


def in_root(root, point):
    rp, rq = root
    d = (point[0] - rp[0], point[1] - rp[1], point[2] - rp[2])
    return qrot((-rq[0], -rq[1], -rq[2], rq[3]), d)


def frame(address, count, parents, ids, extra=None):
    raw = peek(address, 0x20 * count)
    pose = [struct.unpack_from('<8f', raw, i * 0x20) for i in range(count)]
    m = model(parents, pose, list(ids.values()) + list((extra or {}).values()))
    root = m[ids['root']]
    p = {k: in_root(root, m[ids[k]][0]) for k in ('slide', 'mag', 'lthumb', 'lindex', 'rthumb', 'rindex')}
    out = {'slide': p['slide'], 'mag': p['mag']}
    for name, bone in (extra or {}).items():  # --all-bones: every Wep_ bone, and its raw local transform (x y z s qx qy qz qw)
        out[name] = in_root(root, m[bone][0])
        out[name + '.local'] = tuple(round(v, 5) for v in pose[bone])
    for hand, t, i in (('left', 'lthumb', 'lindex'), ('right', 'rthumb', 'rindex')):
        out[hand] = tuple((a + b) / 2 for a, b in zip(p[t], p[i]))
        out[hand + '_spread'] = math.dist(p[t], p[i])
    return out


def saved(t, f):
    return dict(t=t, **{k: ([round(c, 4) for c in v] if isinstance(v, tuple) else round(v, 4)) for k, v in f.items()})


def record_one(gun, event, seconds, out, all_bones=False):
    names, parents, _bind, _live = read_rig()
    ids = {'root': names.index('Wep_Root'), 'slide': names.index('Wep_Slide'), 'mag': names.index('Wep_Mag'),
           'lthumb': names.index('LeftHandThumb4'), 'lindex': names.index('LeftHandIndex2'),
           'rthumb': names.index('RightHandThumb4'), 'rindex': names.index('RightHandIndex2')}
    extra = {n: i for i, n in enumerate(names) if n.startswith('Wep_') and n not in ('Wep_Root', 'Wep_Mag')} if all_bones else None  # Wep_Slide too: a turn bolt's lift is its rotation (#96)
    count = len(names)
    address = local_pose_address()
    rest = None
    while rest is None:
        try:
            rest = frame(address, count, parents, ids, extra)
        except RuntimeError:
            address = local_pose_address()
    if event == 'empty':
        reply = command('reload setammo 0').strip().splitlines()[0]
    else:
        reply = command('pad 0 0 0 0 150 0 0 1').strip()
    t0 = time.time()
    frames = [saved(0.0, rest)]
    last = None
    while time.time() - t0 < seconds:
        try:
            f = frame(address, count, parents, ids, extra)
        except RuntimeError:
            address = local_pose_address()
            continue
        key = tuple(v for v in f.values() if isinstance(v, tuple))
        if key != last:
            last = key
            frames.append(saved(round(time.time() - t0, 3), f))
    path = os.path.join(out, f'{gun}.{event}.json')
    take = 2
    while os.path.exists(path):  # another take of the same gun: kept beside the first, pooled by analyse
        path = os.path.join(out, f'{gun}.{event}.{take}.json')
        take += 1
    with open(path, 'w') as fh:
        json.dump({'gun': gun, 'event': event, 'reply': reply, 'frames': frames}, fh)
    return len(frames)


def median(values):
    s = sorted(values)
    return s[len(s) // 2]


def steadiest_run(points, tolerance=0.02):
    """The longest run of consecutive points that stays within tolerance of its running median."""
    best = []
    for start in range(len(points)):
        run = [points[start]]
        for p in points[start + 1:]:
            centre = [median([q[k] for q in run]) for k in range(3)]
            if math.dist(p, centre) > tolerance:
                break
            run.append(p)
        if len(run) > len(best):
            best = run
    return best


def riding(frames, part, hand, near=0.15):
    """Frames where the hand moves with the part: both moved > 8 mm since the last frame, the same way within 1.2 cm
    (a hand resting near a held-open bolt, or a bolt released by a slap elsewhere, does not count)."""
    out = []
    for a, b in zip(frames, frames[1:]):
        dp = [b[part][k] - a[part][k] for k in range(3)]
        dh = [b[hand][k] - a[hand][k] for k in range(3)]
        moved = math.sqrt(sum(c * c for c in dp)) > 0.008 and math.sqrt(sum(c * c for c in dh)) > 0.008
        if moved and math.dist(dp, dh) < 0.012 and math.dist(b[hand], b[part]) < near:
            out.append(b)
    return out


def analyse_handle(frames, hands):
    rest = frames[0]['slide']
    travel = [rest[2] - f['slide'][2] for f in frames]
    best = None
    for hand in hands:
        moving = riding(frames, 'slide', hand)
        points = [(f[hand][0], f[hand][1], f[hand][2] + (rest[2] - f['slide'][2])) for f in moving]
        if len(points) >= 2 and (best is None or len(points) > len(best[1])):
            best = (hand, points)
    if not best:
        return {'max_travel': round(max(travel), 3), 'result': 'no pull' if max(travel) < 0.02 else 'bolt moved, no hand riding it'}
    hand, points = best
    point = [round(median([p[k] for p in points]), 3) for k in range(3)]
    spread = max(math.dist(p, point) for p in points)
    return {'max_travel': round(max(travel), 3), 'result': 'pull', 'hand': hand, 'frames': len(points), 'point': point,
            'spread_cm': round(spread * 100, 1)}


def way_out(frames, distance):
    """Unit direction from the seated Wep_Mag to its first position this far out, and that direction's angle in the
    gun's mid-plane (y-z: the sideways part is the animation's hand, a magazine cannot leave its well sideways),
    in degrees from straight down, + forward along the barrel."""
    rest = frames[0]['mag']
    for f in frames:
        d = [f['mag'][k] - rest[k] for k in range(3)]
        n = math.sqrt(sum(c * c for c in d))
        if n > distance:
            return [c / n for c in d], math.degrees(math.atan2(d[2], -d[1]))
    return None, None


def pull_start(recordings, hand='left'):
    """The handle as the hand sits on it just before the pull: the last frame with the bolt home (< 5 mm) before it
    is > 3 cm back, the hand within 7 cm of Wep_Slide there and moved back > 2 cm by the next frame. At ~10 fps the
    pull itself is one or two frames and the hand trails the bolt (P90: 2 cm), which riding() rejects. Pooled over
    the takes: the median and the largest distance from it."""
    found = []
    for frames in recordings:
        rest = frames[0]['slide'][2]
        for i in range(len(frames) - 1):
            a, b = frames[i], frames[i + 1]
            if rest - a['slide'][2] < 0.005 and rest - b['slide'][2] > 0.03 and math.dist(a[hand], a['slide']) < 0.07 \
                    and a[hand][2] - b[hand][2] > 0.02:
                found.append(a[hand])
                break
    if not found:
        return None
    point = [round(median([p[k] for p in found]), 3) for k in range(3)]
    return {'point': point, 'takes': len(found), 'spread_cm': round(max(math.dist(p, point) for p in found) * 100, 1)}


def analyse_magazine(recordings):
    """The magazine over one or more recordings of a gun's empty reload (pooled)."""
    out = {'recordings': len(recordings)}
    offsets, angles3, angles6, moves = [], [], [], []
    for frames in recordings:
        rest = frames[0]['mag']
        moved = [math.dist(f['mag'], rest) for f in frames]
        moves.append(max(moved))
        if max(moved) < 0.03:
            continue
        direction, a3 = way_out(frames, 0.03)
        _d6, a6 = way_out(frames, 0.06)
        out.setdefault('way_out', [round(c, 2) for c in direction])
        angles3.append(a3)
        if a6 is not None:
            angles6.append(a6)
        # The grab: the hand moving with the magazine on its first 25 cm out (before it is thrown or pocketed).
        early = [f for f, m in zip(frames, moved) if m < 0.25]
        offsets += [tuple(f['left'][k] - f['mag'][k] for k in range(3)) for f in riding(early, 'mag', 'left', near=0.20)]
    out['max_move'] = round(max(moves), 3)
    out['seated'] = [round(c, 3) for c in recordings[0][0]['mag']]
    if not angles3:
        out['result'] = 'magazine never moved'
        return out
    out['out_deg_3cm'] = [round(a) for a in angles3]
    out['out_deg_6cm'] = [round(a) for a in angles6]
    if len(offsets) >= 2:
        out['grab_offset'] = [round(median([p[k] for p in offsets]), 3) for k in range(3)]
        out['grab_frames'] = len(offsets)
        out['grab_spread_cm'] = round(max(math.dist(p, out['grab_offset']) for p in offsets) * 100, 1)
        # #141: the steady core. A hand arriving at or leaving the magazine adds a few far frames that made the whole
        # spread fail the 6 cm rule although most frames agree: the frames within 6 cm of the median, and their median.
        core = [p for p in offsets if math.dist(p, out['grab_offset']) <= 0.06]
        if len(core) >= 2:
            centre = [median([p[k] for p in core]) for k in range(3)]
            out['grab_core'] = [round(c, 3) for c in centre]
            out['grab_core_frames'] = len(core)
            out['grab_core_spread_cm'] = round(max(math.dist(p, centre) for p in core) * 100, 1)
        out['result'] = 'held'
    else:
        out['result'] = 'moved, no hand riding it'
    return out


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('mode', choices=['record', 'analyse', 'timeline'])
    parser.add_argument('--guns', help='comma-separated debug ids (given into slot 0); default: the gun in hand')
    parser.add_argument('--event', choices=['empty', 'shot'], default='empty')
    parser.add_argument('--seconds', type=float, default=7.0)
    parser.add_argument('--all-bones', action='store_true', help='record: every Wep_ bone too (tube guns: the shell, the pump)')
    parser.add_argument('--out', default=DEFAULT_OUT)
    parser.add_argument('--json')
    parser.add_argument('--table', nargs='?', const='magazines', choices=['magazines', 'handles'],
                        help='analyse: one line per gun (magazine way out and grab, or the handle by both methods)')
    parser.add_argument('--restore', help='debug id to give back into the slot at the end (default: the gun that was there)')
    parser.add_argument('--slot', type=int, help='slot to give the guns into (default: the slot in hand)')
    parser.add_argument('--file', help='timeline: one recording, e.g. U_SCAR-H.empty')
    args = parser.parse_args()
    os.makedirs(args.out, exist_ok=True)
    if args.mode == 'timeline':
        with open(os.path.join(args.out, args.file + '.json')) as fh:
            frames = json.load(fh)['frames']
        rest = frames[0]
        for f in frames:
            print(f"{f['t']:5.2f} travel={rest['slide'][2] - f['slide'][2]:6.3f} mag={math.dist(f['mag'], rest['mag']):5.3f}"
                  f" L={[round(c, 3) for c in f['left']]} Lsp={f['left_spread']:.3f} L-slide={math.dist(f['left'], f['slide']):.3f}"
                  f" R={[round(c, 3) for c in f['right']]} R-slide={math.dist(f['right'], f['slide']):.3f}")
        return
    if args.mode == 'record':
        # The slot in hand (pipe `reload` names it while the hand reload is on): the guns go there. Giving into another
        # slot records the gun in hand for every gun (2026-11-17: slot 1 in hand after a level load, gives into 0).
        found = re.search(r'\bslot=(\d)', command('reload'))
        slot = args.slot if args.slot is not None else int(found.group(1)) if found else 0
        before = slots().get(slot)
        print(f'slot {slot} in hand: {before}', flush=True)
        try:
            command('setting set reload button')
            command('setting set arm_ik 0')
            for gun in (args.guns.split(',') if args.guns else [None]):
                if gun:
                    command(f'givegun {gun} {slot}')
                    deadline = time.time() + 20
                    while time.time() < deadline and slots().get(slot) != gun:
                        time.sleep(0.5)
                    time.sleep(3.0)
                name = gun or slots().get(slot, '?')
                try:
                    print(name, args.event, record_one(name, args.event, args.seconds, args.out, args.all_bones), 'frames', flush=True)
                except (RuntimeError, ValueError) as error:
                    print(name, 'error:', error, flush=True)
                time.sleep(4.0)
        finally:
            command('setting set reload physical')
            command('setting set arm_ik 1')
            restore = args.restore or (before if args.guns else None)
            if restore:
                print('restore:', command(f'givegun {restore} {slot}').strip())
        return
    pooled = {}
    for name in sorted(os.listdir(args.out)):
        if not name.endswith('.json'):
            continue
        with open(os.path.join(args.out, name)) as fh:
            data = json.load(fh)
        if len(data['frames']) >= 3:
            pooled.setdefault((data['gun'], data['event']), []).append(data['frames'])
    results = {}
    for (gun, event), recordings in sorted(pooled.items()):
        entry = results.setdefault(gun, {})
        if event == 'empty':
            entry['handle_empty'] = analyse_handle(recordings[0], ['left'])
            entry['handle_start'] = pull_start(recordings)
            entry['magazine'] = analyse_magazine(recordings)
        else:
            entry['handle_shot'] = analyse_handle(recordings[0], ['left', 'right'])
            entry['handle_shot_start'] = pull_start(recordings, 'right')  # a bolt action: the trigger hand
    for gun, entry in results.items():
        if args.table == 'handles':
            h = entry.get('handle_start')
            r = entry.get('handle_empty', {})
            print(f"{gun:14} start {h}  riding {r.get('point')} n={r.get('frames')}")
            continue
        if args.table:
            m = entry.get('magazine', {})
            print(f"{gun:14} out 3cm {m.get('out_deg_3cm')} 6cm {m.get('out_deg_6cm')} grab {m.get('grab_offset')}"
                  f" n={m.get('grab_frames')} spread={m.get('grab_spread_cm')} takes={m.get('recordings')}")
        else:
            print(gun, json.dumps(entry))
    if args.json:
        with open(args.json, 'w') as fh:
            json.dump(results, fh, indent=1)


if __name__ == '__main__':
    main()
