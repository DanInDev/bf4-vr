"""Which crate guns get the game's own reload sounds (weapon/game_reload_sounds) and which fall back to the synthesised
cues: each gun is given into the slot in hand (own crate), the mod's lookup gets time to run, and the pipe `reload`
report says whether it found the gun's reload SoundEntity. Every reload SoundEntity in memory is listed too (asset
names), so a miss shows whether the gun's sound exists under another folder name or not at all.

    python -E -P tools\\sound_census.py [--guns U_A,U_B] [--wait 14] [--out re\\dumps\\sound_census.json]
"""
import argparse
import json
import re
import struct
import sys
import time
import os

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from camera_probe import command, peek  # noqa: E402
from slot_probe import slots  # noqa: E402

SOUND_ENTITY_VTABLE = 0x141CE88C0  # re/notes/sound.md
CRATE = ('U_MP412Rex,U_CBJ-MS,U_Scorpion,U_JS2,U_MX4,U_P90,U_MagpulPDR,U_PP2000,U_UMP45,U_870,U_HAWK,U_M1014,U_QBS09_SP,'
         'U_SAIGA_20K,U_SPAS12,U_USAS-12_SP,U_UTAS,U_A91,U_GalilACE52,U_AK5C,U_G36C,U_M4A1,U_SG553LB,U_AEK971,U_AK12,'
         'U_SteyrAug,U_CZ805,U_FAMAS,U_M16A4,U_M416,U_QBZ951,U_SAR21,U_SCAR-H,U_M240,U_M249,U_MG4,U_Pecheneg,U_QBB95,'
         'U_Type88,U_Ultimax,U_MK11,U_M39EBR,U_QBU88,U_RFB_SP,U_SCAR-HSV,U_SVD12,U_SRS,U_FY-JS,U_M82A3_SP,U_Scout,U_SV98')


def q(at):
    return struct.unpack('<Q', peek(at, 8))[0]


def text(at):
    raw = peek(at, 128)
    end = raw.find(b'\0')
    return raw[:end if end >= 0 else 128].decode('ascii', 'replace')


AR_EVENTS = {0x9D874880: 'out', 0x0C87FAA9: 'in', 0x95E4238C: 'seat', 0x7C80C030: 'bolt', 0xC46CF22F: 'settle'}  # re/notes/sound.md


def u32(at):
    return struct.unpack('<I', peek(at, 4))[0]


def input_events(patch):
    """The patch's graph input event hashes (SoundGraphData +0x70, AudioGraphParameter NameHash +0x14)."""
    graph = q(patch + 0x28)
    pointer = q(graph + 0x70)
    if not pointer:
        return []
    return [u32(q(pointer + 8 * i) + 0x14) for i in range(min(u32(pointer - 4), 128))]


def reload_entities():
    """Every SoundEntity whose sound is a reload configuration (not AI): its name, its patch, its input events."""
    reply = command('scan ' + struct.pack('<Q', SOUND_ENTITY_VTABLE).hex() + ' 20000')
    found = {}
    for tok in reply.split():
        if not all(c in '0123456789abcdefABCDEF' for c in tok):
            continue
        try:
            sound = q(q(int(tok, 16) + 0x28) + 0x18)
            name = text(q(sound + 0x10))
            if '_Reload' not in name or '/AI/' in name or name in found:
                continue
            patch = q(sound + 0x28)
            patch_name = text(q(patch + 0x10)) if patch else ''
            events = input_events(patch) if patch_name else []
            found[name] = {'patch': patch_name.split('/')[-1], 'events': len(events),
                           'ar_events_present': [AR_EVENTS[e] for e in events if e in AR_EVENTS],
                           'event_hashes': [f'{e:08x}' for e in events]}
        except (RuntimeError, ValueError, struct.error):
            continue
    return found


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--guns', default=CRATE)
    parser.add_argument('--wait', type=float, default=14.0)
    parser.add_argument('--out', default=os.path.join(os.path.dirname(HERE), 're', 'dumps', 'sound_census.json'))
    parser.add_argument('--patches', action='store_true', help='only list the reload sound sets in memory, their patch and events')
    args = parser.parse_args()
    if args.patches:
        names = reload_entities()
        with open(args.out.replace('.json', '_patches.json'), 'w') as fh:
            json.dump(names, fh, indent=1)
        for n, info in sorted(names.items()):
            print(f"{n.split('/')[-2]:16} {info['patch']:42} events {info['events']:2} AR events present {','.join(info['ar_events_present'])}")
        return
    found = re.search(r'\bslot=(\d)', command('reload'))
    slot = int(found.group(1)) if found else 1
    before = slots().get(slot)
    rows = []
    try:
        baseline = set(reload_entities())
        for gun in args.guns.split(','):
            command(f'givegun {gun} {slot}')
            time.sleep(args.wait)
            report = command('reload')
            entity = re.search(r'game_sounds=\d weapon=(\S+) entity=(\w+)', report)
            row = {'gun': gun, 'in_hand': entity.group(1) if entity else None, 'entity': entity.group(2) if entity else None}
            for key in ('folder', 'patch', 'events'):
                found_value = re.search(rf'\b{key}=(\S+)', report)
                row[key] = found_value.group(1) if found_value else None
            row['game_sound'] = bool(entity) and entity.group(2) not in ('0', '0000000000000000')
            # The reload sound sets that exist only while this gun is held: its own (whatever its folder is called).
            present = reload_entities()
            row['own_sets'] = {n: {k: v for k, v in info.items() if k != 'event_hashes'} for n, info in present.items() if n not in baseline}
            rows.append(row)
            print(json.dumps(row), flush=True)
        names = reload_entities()
    finally:
        if before:
            command(f'givegun {before} {slot}')
    with open(args.out, 'w') as fh:
        json.dump({'guns': rows, 'reload_entities_in_memory': names}, fh, indent=1)
    print('reload SoundEntities in memory:', len(names))
    for n, info in sorted(names.items()):
        print(f"   {n.split('/')[-1]:45} patch {info['patch']:40} events {info['events']:2} AR events present {info['ar_events_present']}")


if __name__ == '__main__':
    main()
