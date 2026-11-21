"""Gun lights at the drawn gun (#92), live: the flashlight's SpotLightEntity against the drawn gun's Wep_Root.

The game composes a weapon accessory's light from the first-person model pose (anim+0x58, the same buffer as +0x50,
which the mod's pose edit writes): FUN_140fbb210 -> FUN_1407184f0 (bone read) -> FUN_140fba580 -> FUN_140ccf9f0
(the light's world matrix at +0x50). This probe checks that it stays so:

1. gives the gun with its flashlight, switches the light on (pad D-pad up) and finds the SpotLightEntity nearest
   the drawn Wep_Root (a full heap scan, about 90 s, the pipe answers nothing meanwhile);
2. holds the drawn gun ahead, 30 cm right, turned 30 degrees left and right and pitched 20 degrees up; for each it
   prints the light's place and direction against the drawn gun's (the offset in the gun's frame taken from the
   first pose): the error must stay under 2 cm and 2 degrees;
3. moves the drawn gun 20 times between two places and polls the bone and the light together: the light must take
   its new place every time, and no more than one frame may be presented (pipe status `presents`) while the pose
   is in the new place and the light still in the old one (two or more would be a frame lag in the picture).

4. the laser (a beam and a dot drawn by the game, its entity not traced): gives the gun with the laser, takes
   screenshots with the drawn gun turned 30 degrees left, ahead and 30 degrees right, toggles the laser and takes
   them again; the state with more green pixels is "on", and the laser's pixels are the green ones that the "off"
   shot of the same pose lacks. They must be there in every pose and move with the gun (their mean x left of the
   ahead one when turned left, right of it when turned right, by more than 100 px of 2160).

    python -E -P tools\\light_probe.py [U_MX4] [U_MX4_Flashlight] [U_MX4_GreenLaser]

Needs a level with the mod loaded; no focus. free_hands and use_reach are off while it runs and set back to their
earlier values; fakehead and fakehand are cleared. The screenshots go to a new folder under %TEMP% (printed), never
into the repository. The gun is left with the laser.
"""
import math
import os
import re
import struct
import sys
import tempfile
import time

import numpy as np
from PIL import Image

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from camera_probe import command  # noqa: E402

SPOT_LIGHT_VTABLE = 0x141DEFA70
CONTROLLED = 0x14230F260
WEP_ROOT = 126
LEFT = '0.007 -0.214 -0.600'
POSES = (('ahead', 0, 0, (0.085, -0.222, -0.300)),
         ('30 cm right', 0, 0, (0.385, -0.222, -0.300)),
         ('turned 30 left', 30, 0, (0.085, -0.222, -0.300)),
         ('turned 30 right', -30, 0, (0.085, -0.222, -0.300)),
         ('pitched 20 up', 0, 20, (0.085, -0.222, -0.300)))


def peek(address, length):
    reply = command('peek %x %d' % (address, length)).strip()
    return bytes.fromhex(reply) if re.fullmatch(r'[0-9a-fA-F]+', reply) else None


def pointer(address):
    data = peek(address, 8)
    return struct.unpack('<Q', data)[0] if data else 0


def rotate(q, v):
    x, y, z, w = q
    tx, ty, tz = 2 * (y * v[2] - z * v[1]), 2 * (z * v[0] - x * v[2]), 2 * (x * v[1] - y * v[0])
    return (v[0] + w * tx + y * tz - z * ty, v[1] + w * ty + z * tx - x * tz, v[2] + w * tz + x * ty - y * tx)


def conjugate(q):
    return (-q[0], -q[1], -q[2], q[3])


def setting_value(name):
    m = re.search(re.escape(name) + r' = (\S+)', command('setting get ' + name))
    return m.group(1) if m else None


class Rig:
    def __init__(self):
        animatable = pointer(pointer(pointer(CONTROLLED) + 0x1E8) + 0x128)
        self.model = pointer(animatable + 0x50)
        self.attach = pointer(animatable + 0x58)

    def bone(self, index):
        values = struct.unpack('<8f', peek(self.model + index * 32, 32))
        return values[0:3], values[4:8]

    def gun(self):
        """Wep_Root in the world: the model pose is relative to the Reference bone (bone 0)."""
        (rp, rq), (gp, gq) = self.bone(0), self.bone(WEP_ROOT)
        offset = rotate(rq, gp)
        world_q = quat_multiply(rq, gq)
        return tuple(rp[k] + offset[k] for k in range(3)), world_q


def quat_multiply(a, b):
    ax, ay, az, aw = a
    bx, by, bz, bw = b
    return (aw * bx + ax * bw + ay * bz - az * by, aw * by - ax * bz + ay * bw + az * bx,
            aw * bz + ax * by - ay * bx + az * bw, aw * bw - ax * bx - ay * by - az * bz)


def light_matrix(light):
    m = struct.unpack('<16f', peek(light + 0x50, 64))
    return m[8:11], m[12:15]


def find_light(rig):
    gun_position, _ = rig.gun()
    best = None
    for word in command('scan %s 64' % struct.pack('<Q', SPOT_LIGHT_VTABLE).hex()).split():
        try:
            address = int(word, 16)
        except ValueError:
            continue
        data = peek(address + 0x80, 12)
        if pointer(address) != SPOT_LIGHT_VTABLE or not data:
            continue
        distance = math.dist(struct.unpack('<3f', data), gun_position)
        if best is None or distance < best[0]:
            best = (distance, address)
    return best


def angle(a, b):
    dot = sum(a[k] * b[k] for k in range(3)) / (math.hypot(*a) * math.hypot(*b))
    return math.degrees(math.acos(max(-1.0, min(1.0, dot))))


LASER_POSES = (('turned 30 left', 30), ('ahead', 0), ('turned 30 right', -30))


def green(path):
    rgb = np.asarray(Image.open(path).convert('RGB'), dtype=np.int16)
    r, g, b = rgb[..., 0], rgb[..., 1], rgb[..., 2]
    return (g > r + 40) & (g > b + 40)


def laser_shots(folder, tag):
    masks = []
    for name, yaw in LASER_POSES:
        command('fakehand %d 0 %s %.3f %.3f %.3f' % ((yaw, LEFT) + POSES[0][3]))
        time.sleep(1.2)
        for n in range(3):  # pair rendering: judge from the third shot
            path = os.path.join(folder, '%s_%s_%d.png' % (tag, name.replace(' ', '_'), n))
            command('shot ' + path)
            time.sleep(0.3)
        masks.append(green(path))
    return masks


def laser_check(gun, laser):
    """Returns the number of failures."""
    folder = tempfile.mkdtemp(prefix='bfvr_laser_')
    print('> givegun %s 0 %s -> %s' % (gun, laser, command('givegun %s 0 %s' % (gun, laser)).strip()[:40]))
    time.sleep(3)
    first = laser_shots(folder, 'a')
    command('pad 0 0 0 0 300 0001')
    time.sleep(1.0)
    second = laser_shots(folder, 'b')
    on, off = (first, second) if sum(m.sum() for m in first) >= sum(m.sum() for m in second) else (second, first)
    print('  screenshots in %s; laser on in the %s set' % (folder, 'first' if on is first else 'second'))
    failures = 0
    means = []
    for (name, _), lit, dark in zip(LASER_POSES, on, off):
        near_dark = dark.copy()  # the scene's own green flickers by a pixel or two: keep 4 px clear of it
        for dy in range(-4, 5):
            for dx in range(-4, 5):
                near_dark |= np.roll(np.roll(dark, dy, axis=0), dx, axis=1)
        ys, xs = np.nonzero(lit & ~near_dark)
        if len(xs) < 10:  # seen nearly end-on (ahead) the thin beam covers only ~14 pixels
            print('  %-16s laser pixels %d FAIL (none drawn)' % (name, len(xs)))
            failures += 1
            means.append(None)
            continue
        means.append(xs.mean())
        print('  %-16s laser pixels %5d  mean x %6.1f  x %d..%d  y %d..%d  (green with it off: %d)' % (
            name, len(xs), xs.mean(), xs.min(), xs.max(), ys.min(), ys.max(), int(dark.sum())))
    if None not in means:
        moves = means[1] - means[0] > 100 and means[2] - means[1] > 100
        failures += not moves
        print('  the laser moves with the drawn gun: left %.0f < ahead %.0f < right %.0f px %s' % (
            means[0], means[1], means[2], 'ok' if moves else 'FAIL'))
    return failures


def main():
    gun = sys.argv[1] if len(sys.argv) > 1 else 'U_MX4'
    accessory = sys.argv[2] if len(sys.argv) > 2 else 'U_MX4_Flashlight'
    laser = sys.argv[3] if len(sys.argv) > 3 else 'U_MX4_GreenLaser'
    earlier = {name: setting_value(name) for name in ('free_hands', 'use_reach')}
    failures = 0
    command('fakehead 0.064')
    command('setting set free_hands 0')
    command('setting set use_reach 0')
    try:
        command('fakehand 0 0 %s %.3f %.3f %.3f' % ((LEFT,) + POSES[0][3]))
        print('> givegun %s 0 %s -> %s' % (gun, accessory, command('givegun %s 0 %s' % (gun, accessory)).strip()[:40]))
        time.sleep(3)
        print('  ' + (re.search(r'weapon=\S+', command('reload')) or re.search('', '')).group(0))
        command('pad 0 0 0 0 300 0001')
        time.sleep(1.0)
        rig = Rig()
        print('  model pose %x, attachment pose (anim+0x58) %x: %s' % (
            rig.model, rig.attach, 'the same buffer' if rig.model == rig.attach else 'DIFFERENT buffers'))
        found = find_light(rig)
        if not found or found[0] > 1.5:
            print('FAIL no SpotLightEntity near the drawn gun (nearest %s)' % (found,))
            return 1
        light = found[1]
        print('  light %x, %.3f m from Wep_Root' % (light, found[0]))
        local_offset = local_forward = None
        for name, yaw, pitch, right in POSES:
            command('fakehand %d %d %s %.3f %.3f %.3f' % ((yaw, pitch, LEFT) + right))
            time.sleep(1.2)
            (gp, gq), (forward, position) = rig.gun(), light_matrix(light)
            inverse = conjugate(gq)
            relative = rotate(inverse, tuple(position[k] - gp[k] for k in range(3)))
            relative_forward = rotate(inverse, forward)
            if local_offset is None:
                local_offset, local_forward = relative, relative_forward
            error_cm = math.dist(relative, local_offset) * 100
            error_deg = angle(relative_forward, local_forward)
            ok = error_cm < 2.0 and error_deg < 2.0
            failures += not ok
            print('  %-16s Wep_Root (%.3f %.3f %.3f)  light (%.3f %.3f %.3f) dir (%.3f %.3f %.3f)  in the gun: '
                  '(%.3f %.3f %.3f)  error %.2f cm %.2f deg %s' % (
                      (name,) + gp + position + forward + relative + (error_cm, error_deg, 'ok' if ok else 'FAIL')))
        spots = ['0 0 %s %.3f %.3f %.3f' % ((LEFT,) + POSES[i][3]) for i in (0, 1)]
        command('fakehand ' + spots[0])
        time.sleep(0.5)
        root_x, light_x = rig.model + WEP_ROOT * 32, light + 0x80

        def pair():
            words = re.findall(r'\b([0-9a-fA-F]{8})\b', command('peekmany 4 %x %x' % (root_x, light_x)))
            return tuple(struct.unpack('<f', bytes.fromhex(w))[0] for w in words[:2]) if len(words) >= 2 else None

        def presents():
            m = re.search(r'presents=(\d+)', command('status'))
            return int(m.group(1)) if m else 0

        ends = []  # (root x, light x) at rest in each of the two places
        for spot in spots:
            command('fakehand ' + spot)
            time.sleep(0.5)
            ends.append(pair())
        delays, stale_frames, unmoved = [], [], 0
        for move in range(20):
            root_target, light_target = ends[move % 2]
            command('fakehand ' + spots[move % 2])
            start = time.perf_counter()
            t_root = t_light = None
            frames_at = None
            stale = 0
            while time.perf_counter() - start < 0.3 and t_light is None:
                sample = pair()
                if not sample:
                    continue
                now = time.perf_counter() - start
                if t_root is None and abs(sample[0] - root_target) < 0.005:
                    t_root = now
                if abs(sample[1] - light_target) < 0.005:
                    t_light = now
                    if frames_at is not None:
                        stale = presents() - frames_at
                elif t_root is not None and frames_at is None:
                    frames_at = presents()  # the pose is in the new place, the light not yet
            if t_root is None or t_light is None:
                unmoved += 1
                continue
            delays.append((t_light - t_root) * 1000)
            stale_frames.append(stale)
        # A light that took the new place in the same frame as the pose is never seen old: no frame is
        # presented between the two. One presented frame is a race at the frame's end, two or more a lag.
        timely = not unmoved and len(delays) == 20 and max(stale_frames) <= 1
        failures += not timely
        print('  timing: %d moves of %.2f m; the light %s; after the pose by %.2f ms median, %.2f ms at most; frames '
              'presented with the light behind: %s %s' % (
                  len(delays), abs(ends[0][0] - ends[1][0]),
                  'moved every time' if not unmoved else 'did NOT move %d times' % unmoved,
                  sorted(delays)[len(delays) // 2] if delays else -1, max(delays) if delays else -1,
                  ' '.join(str(s) for s in stale_frames), 'ok, same frame' if timely else 'FAIL'))
        failures += laser_check(gun, laser)
    finally:
        for name, value in earlier.items():
            if value is not None:
                command('setting set %s %s' % (name, value))
        command('fakehand off')
        command('fakehead off')
        print('restored free_hands/use_reach, fakehand and fakehead off')
    print('RESULT %s' % ('ok' if not failures else '%d FAIL' % failures))
    return 1 if failures else 0


if __name__ == '__main__':
    sys.exit(main())
