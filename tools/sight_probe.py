"""Sight line of the drawn gun from the game's own aim-down-sights pose (game must run with the DLL).

    python -E -P tools\\sight_probe.py            the camera joint in Wep_Root axes, as posed now
    python -E -P tools\\sight_probe.py --ads 2    holds the aim button (LT) 2 s first, then samples

In the game's ADS animation the camera looks through the sight, so the camera's position and forward
in Wep_Root axes (+z barrel, +y up, +x left) are the gun's sight line. Run it with `armik 0`,
`setting set aim head` and `setting set gun_pose animated` (restore them afterwards), else the pose
is the mod's, not the game's. Also prints Wep_Scope1/Wep_Scope2 for comparison.
"""
import argparse
import math
import os
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)  # `python -P` leaves the script folder off the path

from camera_probe import command  # noqa: E402
from skeleton_probe import read_rig  # noqa: E402


def qmul(a, b):
    ax, ay, az, aw = a
    bx, by, bz, bw = b
    return (aw * bx + ax * bw + ay * bz - az * by, aw * by - ax * bz + ay * bw + az * bx,
            aw * bz + ax * by - ay * bx + az * bw, aw * bw - ax * bx - ay * by - az * bz)


def qconj(q):
    return (-q[0], -q[1], -q[2], q[3])


def qrot(q, v):
    p = qmul(qmul(q, (v[0], v[1], v[2], 0.0)), qconj(q))
    return p[:3]


def model(names, parents, live, bone):
    position, _, rotation = live[bone]
    p, q = position, rotation
    parent = parents[bone]
    while parent >= 0:
        pp, _, pq = live[parent]
        p = tuple(a + b for a, b in zip(pp, qrot(pq, p)))
        q = qmul(pq, q)
        parent = parents[parent]
    return p, q


def in_frame(frame, point=None, direction=None):
    fp, fq = frame
    if point is not None:
        return qrot(qconj(fq), tuple(a - b for a, b in zip(point, fp)))
    return qrot(qconj(fq), direction)


def sample(label):
    names, parents, _, live = read_rig()
    index = {name: i for i, name in enumerate(names)}
    root = model(names, parents, live, index['Wep_Root'])
    camera = model(names, parents, live, index['CameraJoint'])
    eye = in_frame(root, point=camera[0])
    forward = in_frame(root, direction=qrot(camera[1], (0.0, 0.0, -1.0)))
    up = in_frame(root, direction=qrot(camera[1], (0.0, 1.0, 0.0)))
    print(f'{label}: camera in Wep_Root axes  eye=({eye[0]:+.4f},{eye[1]:+.4f},{eye[2]:+.4f})'
          f'  forward=({forward[0]:+.4f},{forward[1]:+.4f},{forward[2]:+.4f})  up=({up[0]:+.3f},{up[1]:+.3f},{up[2]:+.3f})')
    pitch = math.degrees(math.atan2(forward[1], forward[2]))
    yaw = math.degrees(math.atan2(forward[0], forward[2]))
    print(f'      sight line vs barrel: {pitch:+.3f} deg up, {yaw:+.3f} deg left')
    for name in ('Wep_Scope1', 'Wep_Scope2'):
        if name in index:
            p = in_frame(root, point=model(names, parents, live, index[name])[0])
            print(f'      {name:<11} ({p[0]:+.4f},{p[1]:+.4f},{p[2]:+.4f})')


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--ads', type=float, default=0.0, help='hold the aim button this many seconds, then sample')
    args = parser.parse_args()
    if args.ads > 0:
        command(f'pad 0 0 0 0 {int(args.ads * 1000) + 3000} 0 255 0')
        time.sleep(args.ads)
        sample('aiming')
    else:
        sample('now')


if __name__ == '__main__':
    main()
