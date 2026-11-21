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
   its new place in the same frame as the pose (well under one frame, ~7 ms).

    python -E -P tools\\light_probe.py [U_MX4] [U_MX4_Flashlight]

Needs a level with the mod loaded; no focus. free_hands and use_reach are off while it runs and set back to their
earlier values; fakehead and fakehand are cleared. The light is left on.
"""
import math
import os
import re
import struct
import sys
import time

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


def main():
    gun = sys.argv[1] if len(sys.argv) > 1 else 'U_MX4'
    accessory = sys.argv[2] if len(sys.argv) > 2 else 'U_MX4_Flashlight'
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
        worst_ms = 0.0
        for move in range(20):
            command('fakehand ' + spots[(move + 1) % 2])
            start, samples = time.perf_counter(), []
            while time.perf_counter() - start < 0.3:
                words = re.findall(r'\b([0-9a-fA-F]{8})\b', command('peekmany 4 %x %x' % (root_x, light_x)))
                if len(words) >= 2:
                    samples.append((time.perf_counter() - start,) + tuple(
                        struct.unpack('<f', bytes.fromhex(w))[0] for w in words[:2]))
            if not samples:
                continue
            final_root, final_light = samples[-1][1], samples[-1][2]
            t_root = next(t for t, r, _ in samples if abs(r - final_root) < 0.005)
            t_light = next(t for t, _, l in samples if abs(l - final_light) < 0.005)
            worst_ms = max(worst_ms, (t_light - t_root) * 1000)
        timely = worst_ms < 3.5
        failures += not timely
        print('  timing: over 20 moves the light took its new place at most %.2f ms after the pose (a frame ~7 ms) %s' % (
            worst_ms, 'ok, same frame' if timely else 'FAIL, a frame late'))
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
