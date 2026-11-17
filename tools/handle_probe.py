"""Per-gun charging handle points from the game's own empty reload (docs/tasks/all-weapons.md, re/notes/weapon.md
"Charging handles from the game's own reload": the method used by hand for the AK-12 on 2026-11-03, automated).

    python -E -P tools\\handle_probe.py [--guns U_M416,U_AK12] [--json out.json] [--restore U_SCAR-H]

For each gun (given into slot 0 with the own crate, pipe `givegun`; without --guns, the gun in hand): reload = button and
arm_ik = 0 (the game's animated hands), the server magazine set to 0 (pipe `reload setammo 0`, research), so the game plays
its empty reload; the first-person local pose is sampled as fast as the pipe allows and forward kinematics give, in
Wep_Root axes (+z along the barrel, +y up, +x left): Wep_Slide (the bolt) and the left hand's pinch (thumb tip and index
middle). The handle: where the hand pinches as the slide starts back (0.8 cm travel), and at the slide's peak minus its
travel. Guns whose empty reload never moves the slide by hand (a bolt release, a hold-open closed by a button) report
"no pull". Settings are restored at the end (reload physical, arm_ik 1).
"""
import argparse
import json
import math
import os
import struct
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

from camera_probe import command, peek, pointer  # noqa: E402
from skeleton_probe import SOLDIER_GLOBAL, read_rig  # noqa: E402
from slot_probe import slots  # noqa: E402


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


def sample(address, count, parents, ids):
    raw = peek(address, 0x20 * count)
    pose = [struct.unpack_from('<8f', raw, i * 0x20) for i in range(count)]
    m = model(parents, pose, ids.values())
    root = m[ids['root']]
    slide = in_root(root, m[ids['slide']][0])
    thumb = in_root(root, m[ids['thumb']][0])
    index = in_root(root, m[ids['index']][0])
    pinch = tuple((a + b) / 2 for a, b in zip(thumb, index))
    return slide, pinch, math.dist(thumb, index)


def measure(gun, seconds=7.0):
    names, parents, _bind, _live = read_rig()
    ids = {'root': names.index('Wep_Root'), 'slide': names.index('Wep_Slide'), 'thumb': names.index('LeftHandThumb4'),
           'index': names.index('LeftHandIndex2')}
    count = len(names)
    address = local_pose_address()
    rest = []
    while len(rest) < 5:
        try:
            rest.append(sample(address, count, parents, ids))
        except RuntimeError:
            address = local_pose_address()
    rest_slide = tuple(sum(s[0][k] for s in rest) / len(rest) for k in range(3))
    reply = command('reload setammo 0').strip()
    t0 = time.time()
    samples = []
    while time.time() - t0 < seconds:
        try:
            slide, pinch, spread = sample(address, count, parents, ids)
        except RuntimeError:
            address = local_pose_address()
            continue
        samples.append((time.time() - t0, slide, pinch, spread))
    travel = [rest_slide[2] - s[1][2] for s in samples]  # back along the barrel = -z
    row = {'gun': gun, 'setammo': reply, 'samples': len(samples), 'rest_slide': [round(v, 4) for v in rest_slide],
           'max_travel': round(max(travel), 4) if travel else None}
    start = next((i for i, t in enumerate(travel) if t > 0.008), None)
    if start is None or max(travel) < 0.02:
        row['result'] = 'no pull'
        return row
    peak = max(range(len(travel)), key=lambda i: travel[i])
    t, slide, pinch, spread = samples[start]
    row['start'] = {'t': round(t, 2), 'pinch': [round(v, 3) for v in pinch], 'thumb_index_cm': round(spread * 100, 1)}
    pt, pslide, ppinch, pspread = samples[peak]
    row['peak'] = {'t': round(pt, 2), 'travel': round(travel[peak], 3),
                   'pinch_minus_travel': [round(ppinch[0], 3), round(ppinch[1], 3), round(ppinch[2] + travel[peak], 3)],
                   'thumb_index_cm': round(pspread * 100, 1)}
    # The hand riding on the handle: its pinch moved forward by the travel stays put. At ~5 fps the start is seen late,
    # so the handle is the median of that over every frame of the pull (travel > 1 cm) with the hand at the slide.
    # Only frames where the hand grips (thumb and index closer than 4.5 cm): an open hand passing by is no pull.
    riding = [(p[0], p[1], p[2] + tr) for (_t, sl, p, sp), tr in zip(samples, travel) if tr > 0.01 and sp < 0.045 and math.dist(p, sl) < 0.12]
    if len(riding) >= 20:
        handle = [sorted(c[k] for c in riding)[len(riding) // 2] for k in range(3)]
        distances = sorted(math.dist(c, handle) for c in riding)
        row['handle'] = [round(v, 3) for v in handle]
        row['handle_frames'] = len(riding)
        row['handle_spread_cm'] = round(distances[3 * len(distances) // 4] * 100, 1)  # 75 % of the frames within this
        row['result'] = 'pull'
    else:
        row['result'] = 'slide moved, hand away (bolt release?)'
    return row


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--guns', help='comma-separated debug ids (given into slot 0); default: the gun in hand')
    parser.add_argument('--json')
    parser.add_argument('--restore', help='debug id to give back into slot 0 at the end')
    args = parser.parse_args()
    rows = []
    try:
        command('setting set reload button')
        command('setting set arm_ik 0')
        guns = args.guns.split(',') if args.guns else [None]
        for gun in guns:
            if gun:
                command(f'givegun {gun} 0')
                deadline = time.time() + 20
                while time.time() < deadline and slots().get(0) != gun:
                    time.sleep(0.5)
                time.sleep(3.0)
            name = gun or slots().get(0, '?')
            try:
                row = measure(name)
            except (RuntimeError, ValueError) as error:
                row = {'gun': name, 'result': f'error: {error}'}
            rows.append(row)
            print(json.dumps(row), flush=True)
            time.sleep(4.0)  # let the reload finish before the next gun
            if args.json:
                with open(args.json, 'w') as fh:
                    json.dump(rows, fh, indent=1)
    finally:
        command('setting set reload physical')
        command('setting set arm_ik 1')
        if args.restore:
            print('restore:', command(f'givegun {args.restore} 0').strip())


if __name__ == '__main__':
    main()
