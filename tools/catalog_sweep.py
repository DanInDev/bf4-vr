"""Catalog sweep (docs/tasks/all-weapons.md step 2): every crate gun into the primary slot, its firing data read.

    python -E -P tools\\catalog_sweep.py [--json re\\dumps\\catalog_sweep.json] [--only U_M416,U_870] [--restore U_SCAR-H]

For each of the 51 crate guns (weapon/crate_guns.hpp order): pipe `givegun <id> 0` (the own crate, #80, a research
command), wait until slot 0 holds it, then read the spawned weapon's firing data (weapon_catalog_probe.firing_data)
and the reload kind the mod chose from bfvr.log (`reload: <id> slot 0 <kind>, capacity ...`). Needs a campaign level
with the own-crate build deployed; it changes the player's primary gun (--restore gives one back at the end).
"""
import argparse
import json
import os
import re
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

from camera_probe import command  # noqa: E402
from slot_probe import q, slots  # noqa: E402
from slot_probe import SOLDIER_GLOBAL  # noqa: E402
from weapon_catalog_probe import describe_stored, firing_data  # noqa: E402

LOG = r'E:\EA\Battlefield 4\bfvr.log'


def crate_guns():
    header = os.path.join(HERE, '..', 'src', 'weapon', 'crate_guns.hpp')
    text = open(header, encoding='utf-8').read()
    values = re.search(r'values = "([^"]+)"', text).group(1).split('|')
    labels = dict(part.split('=', 1) for part in re.search(r'choices = "([^"]+)"', text).group(1).split('|'))
    return [(v, labels[v]) for v in values if v != 'keep']


def log_tail(start):
    with open(LOG, 'rb') as fh:
        fh.seek(start)
        return fh.read().decode('utf-8', 'replace')


def slot0_weapon():
    weapons = q(q(SOLDIER_GLOBAL) + 0x570)
    begin = q(weapons + 0x890)
    return q(begin)


def sweep_one(gun):
    start = os.path.getsize(LOG)
    reply = command(f'givegun {gun} 0').strip()
    row = {'unlock': gun, 'give': reply}
    if not reply.startswith('ok'):
        return row
    deadline = time.time() + 20
    while time.time() < deadline and slots().get(0) != gun:
        time.sleep(0.5)
    row['in_slot'] = slots().get(0) == gun
    if not row['in_slot']:
        return row
    time.sleep(2.0)  # the mod's reload module reads the new gun within a second or two
    weapon = slot0_weapon()
    try:
        row.update(describe_stored(q(weapon + 0x4850)))
        row.update(firing_data(q(weapon + 0x30)))
    except RuntimeError as error:
        row['error'] = str(error)
    # The mod's reload line can come late (the MP412 Rex's after 2 s): wait up to 10 s for it.
    pattern = r'reload: ' + re.escape(gun) + r' slot 0 (\w+), capacity (\d+)'
    match = re.search(pattern, log_tail(start))
    waited = time.time() + 10
    while not match and time.time() < waited:
        time.sleep(0.5)
        match = re.search(pattern, log_tail(start))
    row['mod_kind'] = match.group(1) if match else None
    row['mod_capacity'] = int(match.group(2)) if match else None
    return row


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--json')
    parser.add_argument('--only', help='comma-separated debug ids')
    parser.add_argument('--restore', help='debug id to give back into slot 0 at the end')
    args = parser.parse_args()
    guns = crate_guns()
    if args.only:
        wanted = set(args.only.split(','))
        guns = [g for g in guns if g[0] in wanted]
    if args.json:
        os.makedirs(os.path.dirname(os.path.abspath(args.json)), exist_ok=True)
    rows = []
    for gun, label in guns:
        row = sweep_one(gun)
        row['label'] = label
        rows.append(row)
        print(f"{label:<14} {gun:<14} kind={row.get('mod_kind')} mag={row.get('mag_capacity')}x{row.get('mag_count')} "
              f"{row.get('reload_type')} {row.get('fire_logic')} {row.get('fire_modes')} rof={row.get('rate_of_fire')} "
              f"reload={row.get('reload_time_left')}/{row.get('reload_time')} bolt={row.get('bolt_time')} zoom={row.get('zoom_levels')} "
              f"{'' if row.get('in_slot') else 'NOT GIVEN: ' + row.get('give', '')} {row.get('error', '')}", flush=True)
        if args.json:
            with open(args.json, 'w') as fh:
                json.dump(rows, fh, indent=1)
    if args.restore:
        print('restore:', command(f'givegun {args.restore} 0').strip())
    print(len(rows), 'guns')


if __name__ == '__main__':
    main()
