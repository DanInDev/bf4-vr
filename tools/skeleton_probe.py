"""First-person skeleton research through the bf-vr control pipe (game must run with the DLL).

    python -E -P tools\\skeleton_probe.py                 every bone: index, parent, name
    python -E -P tools\\skeleton_probe.py --match Hand    bones whose name contains the text, with the
                                                          bind pose (SkeletonAsset LocalPose) and the
                                                          live animated local pose side by side

Layout (re/notes/skeleton.md): local soldier 0x14230F260 -> +0x1E8 first-person component ->
+0x128 Animatable; Animatable +0x48 live local pose (QuatTransform[bones], 0x20 each: translation
xyz + scale, rotation xyzw), +0x230 SkeletonAsset (+0x18 BoneNames, +0x28 Hierarchy, +0x30
LocalPose, +0x38 ModelPose; arrays keep their count at data-4).
"""
import argparse
import math
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)  # `python -P` leaves the script folder off the path

from camera_probe import command, peek, peek_many, pointer  # noqa: E402

SOLDIER_GLOBAL = 0x14230F260


def transforms(blob: bytes):
    out = []
    for i in range(0, len(blob) - 0x1F, 0x20):
        x, y, z, s, qx, qy, qz, qw = struct.unpack_from('<8f', blob, i)
        out.append(((x, y, z), s, (qx, qy, qz, qw)))
    return out


def angle_between(a, b):
    d = abs(sum(p * q for p, q in zip(a, b)))
    return math.degrees(2.0 * math.acos(min(1.0, d)))


def read_rig():
    soldier = pointer(SOLDIER_GLOBAL)
    component = pointer(soldier + 0x1E8)
    animatable = pointer(component + 0x128)
    local_pose = pointer(animatable + 0x48)
    skeleton = pointer(animatable + 0x230)
    names_at = pointer(skeleton + 0x18)
    count = struct.unpack('<I', peek(names_at - 4, 4))[0]
    hierarchy_at = pointer(skeleton + 0x28)
    parents = struct.unpack(f'<{count}i', peek(hierarchy_at, 4 * count))
    name_pointers = struct.unpack(f'<{count}Q', peek(names_at, 8 * count))
    raw = peek_many(name_pointers, 48)
    names = [(raw.get(p) or b'?').split(b'\0')[0].decode(errors='replace') for p in name_pointers]
    bind_at = pointer(skeleton + 0x30)
    bind_count = struct.unpack('<I', peek(bind_at - 4, 4))[0] if bind_at else 0
    bind = transforms(peek(bind_at, 0x20 * count)) if bind_count == count else []
    live = transforms(peek(local_pose, 0x20 * count))
    return names, parents, bind, live


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--match', default='', help='only bones whose name contains this text')
    args = parser.parse_args()
    names, parents, bind, live = read_rig()
    print(f'{len(names)} bones, bind pose {"found" if bind else "missing"}')
    for i, name in enumerate(names):
        if args.match and args.match.lower() not in name.lower():
            continue
        parent = names[parents[i]] if parents[i] >= 0 else '-'
        line = f'{i:3d} {name:<28} parent {parents[i]:3d} {parent}'
        if args.match:
            lp, _, lq = live[i]
            line += f'\n      live  t=({lp[0]:+.4f},{lp[1]:+.4f},{lp[2]:+.4f}) q=({lq[0]:+.3f},{lq[1]:+.3f},{lq[2]:+.3f},{lq[3]:+.3f})'
            if bind:
                bp, _, bq = bind[i]
                line += (f'\n      bind  t=({bp[0]:+.4f},{bp[1]:+.4f},{bp[2]:+.4f}) q=({bq[0]:+.3f},{bq[1]:+.3f},{bq[2]:+.3f},{bq[3]:+.3f})'
                         f'  live vs bind {angle_between(lq, bq):5.1f} deg')
        print(line)


if __name__ == '__main__':
    main()
