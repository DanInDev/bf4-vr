"""Reload sound entities (#30, re/notes/sound.md; game must run with the DLL, research = 1).

    python -E -P tools\\reload_sound_probe.py               list SoundEntity objects whose sound is a reload patch
    python -E -P tools\\reload_sound_probe.py --event Request --name AK12
                                                         send that entity event to the matching entity

Finds live SoundEntity objects by their vtable (pipe `scan`), names their SoundEntityData.Sound
(+0x28 -> +0x18), and with --event calls the entity's event handler on the game thread (pipe
`entityevent`). Event ids are the engine's string hash (h = h * 33 ^ c, seed 5381).
"""
import argparse
import json
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)  # `python -P` leaves the script folder off the path

from camera_probe import command, peek_many  # noqa: E402

# Vtables the typeinfo map gives for SoundEntity, SoundEffectEntity and SoundDataEntity; the live
# SoundEntity objects of this build use one of them.
SOUND_VTABLES = (0x141CE85B0, 0x141CE8700, 0x141CE88C0)


def fb_hash(text):
    value = 5381
    for byte in text.encode():
        value = ((value * 33) ^ byte) & 0xFFFFFFFF
    return value


def q(blob, offset=0):
    return struct.unpack_from('<Q', blob, offset)[0]


def name_of(pointer, cache):
    if pointer in cache:
        return cache[pointer]
    blob = peek_many([pointer], 0x40)[pointer]
    name = ''
    if blob:
        text = peek_many([q(blob, 0x10)], 128)[q(blob, 0x10)]
        name = text.split(b'\0')[0].decode('ascii', errors='replace').replace('�', '?') if text else ''
    cache[pointer] = name
    return name


def reload_entities(match='Reload'):
    objects = []
    for vtable in SOUND_VTABLES:
        reply = command(f'scan {struct.pack("<Q", vtable).hex()} 1024')
        objects += [int(x, 16) for x in reply.split() if all(c in '0123456789abcdef' for c in x)]
    heads = peek_many(objects, 0x90)
    datas = {o: q(b, 0x28) for o, b in heads.items() if b}
    data_blobs = peek_many(set(datas.values()), 0x20)
    cache = {}
    found = []
    for entity, data in datas.items():
        blob = data_blobs.get(data)
        if not blob:
            continue
        sound = q(blob, 0x18)
        name = name_of(sound, cache) if sound else ''
        if match.lower() in name.lower():
            playing = q(heads[entity], 0x80)
            found.append((entity, name, playing, q(heads[entity])))
    return found


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--event')
    parser.add_argument('--name', default='')
    parser.add_argument('--slot', type=int, default=5)
    parser.add_argument('--match', default='Reload', help='list entities whose sound name contains this (default Reload)')
    args = parser.parse_args()
    entities = reload_entities(args.match)
    for entity, name, playing, vtable in entities:
        print(f'{entity:#x} vtable={vtable:#x} playing={playing:#x} {name}')
    if args.event:
        targets = [e for e, n, _, _ in entities if args.name in n]
        for entity in targets[:1]:
            print(command(f'entityevent {entity:x} {fb_hash(args.event):x} {args.slot}'))


if __name__ == '__main__':
    main()
