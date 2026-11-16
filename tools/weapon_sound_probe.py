"""Weapon sound research (#30, re/notes/sound.md; game must run with the DLL, campaign only).

    python -E -P tools\\weapon_sound_probe.py            the gun in hand: its sound asset, graph events and parameters
    python -E -P tools\\weapon_sound_probe.py --slot 0

Reads the client weapon's FiringFunctionData.Sound (+0x160, SoundAsset; a SoundPatchConfigurationAsset
points at its SoundPatchAsset at +0x28), the patch's SoundGraphData (+0x28) and its input/output
events and parameters (AudioGraphParameter: NameHash +0x14, ValueIndex +0x18). Names are matched
against candidate words with the engine's string hash.
"""
import argparse
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)  # `python -P` leaves the script folder off the path

from camera_probe import peek  # noqa: E402

SOLDIER_GLOBAL = 0x14230F260

CANDIDATES = [
    'Fire', 'FireSingle', 'FireAuto', 'FireBurst', 'FireStop', 'FireStart', 'FireEnd', 'Tail', 'Reload', 'ReloadEmpty', 'ReloadStart',
    'ReloadEnd', 'ReloadShotgun', 'ReloadCancel', 'MagOut', 'MagIn', 'MagazineOut', 'MagazineIn', 'BoltBack', 'BoltForward', 'Bolt',
    'Charge', 'ChargingHandle', 'Rack', 'Chamber', 'DryFire', 'Empty', 'Click', 'Equip', 'Unequip', 'Deploy', 'Draw', 'Holster',
    'Start', 'Stop', 'Zoom', 'ZoomIn', 'ZoomOut', 'Ads', 'Mode', 'FireMode', 'SwitchFireMode', 'Accessory', 'Toggle', 'Mechanics',
    'Foley', 'Handling', 'Movement', 'Shell', 'ShellEject', 'Silenced', 'Suppressed', 'Distance', 'Indoor', 'Outdoor', 'Interior',
    'IsFirstPerson', 'FirstPerson', 'ThirdPerson', 'IsLocalPlayer', 'LocalPlayer', 'Rpm', 'RateOfFire', 'Ammo', 'AmmoLeft', 'AmmoCount',
    'LowAmmo', 'Heat', 'Overheat', 'Spin', 'SpinUp', 'SpinDown', 'Cock', 'Pump', 'Trigger', 'TriggerPull', 'TriggerRelease',
]


def fb_hash(text):
    # Frostbite's string hash (DICE "FNV-1"-style: h = h * 33 ^ c, seed 5381), 32-bit.
    value = 5381
    for byte in text.encode():
        value = ((value * 33) ^ byte) & 0xFFFFFFFF
    return value


def fnv1(text):
    value = 0x811C9DC5
    for byte in text.encode():
        value = ((value * 0x01000193) & 0xFFFFFFFF) ^ byte
    return value


NAMES = {}
for word in CANDIDATES:
    for variant in {word, word.lower(), word.upper()}:
        NAMES.setdefault(fb_hash(variant), f'{variant} (h33)')
        NAMES.setdefault(fnv1(variant), f'{variant} (fnv1)')


def q(address):
    return struct.unpack('<Q', peek(address, 8))[0]


def u32(address):
    return struct.unpack('<I', peek(address, 4))[0]


def cstring(pointer):
    try:
        return peek(pointer, 128).split(b'\0')[0].decode('ascii', errors='replace').replace('�', '?') if pointer else ''
    except RuntimeError:
        return '?'


def array(pointer):
    if not pointer:
        return []
    count = u32(pointer - 4)
    return [q(pointer + 8 * i) for i in range(min(count, 512))]


def current_weapon(slot=None):
    soldier = q(SOLDIER_GLOBAL)
    weapons = q(soldier + 0x570)
    if slot is None:
        flags = u32(weapons + 0x28)
        holder = q(weapons + (0x8E0 if flags & 4 else 0x8D8))
        slot = u32(holder + 0x8)
    begin = q(weapons + 0x890)
    return slot, q(begin + 8 * slot)


def show_parameters(title, pointer):
    entries = array(pointer)
    print(f'  {title}: {len(entries)}')
    for entry in entries:
        name_hash = u32(entry + 0x14)
        index = struct.unpack('<H', peek(entry + 0x18, 2))[0]
        default = struct.unpack('<f', peek(entry + 0x10, 4))[0]
        print(f'    hash {name_hash:08x} index {index:3d} default {default:8.3f}  {NAMES.get(name_hash, "")}')


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--slot', type=int)
    args = parser.parse_args()
    slot, weapon = current_weapon(args.slot)
    unlock = q(weapon + 0x4850)
    print(f'slot {slot} weapon {weapon:#x} {cstring(q(unlock + 0x20))}')
    function = q(q(q(weapon + 0x30) + 0x90) + 0x10)
    sound = q(function + 0x160)
    print(f'FiringFunctionData {function:#x} Sound {sound:#x} "{cstring(q(sound + 0x10))}"')
    patch = sound
    nested = q(sound + 0x28)
    if nested and cstring(q(nested + 0x10)):
        print(f'  configuration -> patch {nested:#x} "{cstring(q(nested + 0x10))}"')
        patch = nested
    graph = q(patch + 0x28)
    print(f'graph {graph:#x}')
    show_parameters('input events', q(graph + 0x70))
    show_parameters('output events', q(graph + 0x78))
    show_parameters('input parameters', q(graph + 0x60))


if __name__ == '__main__':
    main()
