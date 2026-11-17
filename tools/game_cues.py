"""The mod's sound cues from the game's own files (re/notes/sound.md "Game files"): where a gun's reload sound set has no
event for a physical step, the step plays a cut of that gun's own wave, read from BF4's data by tools/gamesounds (the
Great Destroyer's reader, changed for BF4). Nothing is recorded and nothing of the game goes into git: the decoded waves
stay in re/dumps/gamesounds, the cues go to bfvr_sounds beside bfvr.ini, where weapon/reload_sounds plays them
(bfvr_sounds\\<sound folder>\\<cue>.wav for one gun, bfvr_sounds\\<cue>.wav for all).

    python -E -P tools\\game_cues.py            decode what is missing, cut and install every cue, print the table
    python -E -P tools\\game_cues.py --dry-run  the table only
    python -E -P tools\\game_cues.py --remove   take every cue this tool installs out again (the synthesised ones play)

Cues:
  <folder>\\bolt_back.wav  the rack's rear stop: the knock in the gun's own bolt wave just before its loudest one (the
                          bolt slamming home); the bolt-action snipers' own boltback wave; the G36C's where a gun's
                          wave starts at the release (M16, M416, AUG ..., as the Great Destroyer's owner chose for BF3).
  cloth.wav               a shell, a belt or a grenade from the pouch: the weapon switch's cloth (generic_deploy).
  pin.wav                 the grenade pin (#66): M67_Remove_Safety_Pin.
  M320\\magazine_out.wav, magazine_in.wav   the M320's tube opened, a grenade in.
"""
import argparse
import os
import subprocess
import sys

import numpy as np
from scipy.io import wavfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
GAME = r'E:\EA\Battlefield 4'
SOUNDS = os.path.join(GAME, 'bfvr_sounds')
DECODED = os.path.join(ROOT, 're', 'dumps', 'gamesounds')
WAVS = os.path.join(DECODED, 'wav')
EXE = os.path.join(ROOT, 'build', 'gamesounds', 'Release', 'gamesounds.exe')
PATTERNS = ['reload_1p/', 'generic_deploy', 'm67_remove_safety_pin']

# The game plays its reload waves this much under full scale (the AK-12's bolt, magazine in and out decoded and
# correlated with a recording of the game playing them: -18, -25, -24 dB; correlation 0.89 / 0.84 / 0.92).
GAIN_DB = -18.0
FOLEY_GAIN_DB = -20.0

# Sound folder (the gun's firing sound folder, what the mod looks up) -> its reload_1p wave name and how to cut it.
#   'bolt': the knock before the slam in <name>_bolt;  'back': the loudest knock of <name>_boltback / <name>;
#   'borrow': the G36C's (its own wave starts at the release).
REAR_STOP = {
    'CBJ-MS': ('cbj_ms', 'bolt'), 'Scorpion': ('scorpion', 'bolt'), 'JS2': ('js2', 'bolt'), 'MX4': ('mx4', 'bolt'),
    'P90': ('p90', 'bolt'), 'MagpulPDR': ('magpulpdr', 'bolt'), 'PP2000': ('pp2000', 'bolt'), 'UMP': ('ump_boltopen', 'back'),
    'HAWK': ('hawk', 'bolt'), 'Saiga12': ('saiga12', 'bolt'), 'USAS12': ('usas12', 'bolt'), 'UTAS': ('utas', 'bolt'),
    'A91': ('a91', 'bolt'), 'GalilACE52': ('galilace', 'bolt'), 'AK5': ('ak5', 'borrow'), 'G36c': ('g36', 'bolt'),
    'M4A1': ('m16', 'bolt'), 'SG553LB': ('sg553', 'bolt'), 'AEK971': ('aek971', 'bolt'), 'AK12': ('ak12', 'bolt'),
    'SteyrAUG_A3': ('aug', 'bolt'), 'CZ805': ('cz805', 'bolt'), 'FAMAS': ('famas', 'bolt'), 'M16A3': ('m16', 'bolt'),
    'M416': ('m416', 'bolt'), 'QBZ-951': ('qbz951', 'bolt'), 'SAR21': ('sar21', 'bolt'), 'SCAR-H': ('scar', 'bolt'),
    'M240': ('m240', 'bolt'), 'M249': ('m249', 'bolt'), 'MG4': ('mg4', 'bolt'), 'Pecheneg': ('pkp', 'bolt'),
    'QBB-95': ('qbb95', 'bolt'), 'Type88': ('type88', 'bolt'), 'Ultimax': ('ultimax', 'bolt'),
    'Mk11': ('mk11', 'bolt'), 'M39': ('m39', 'bolt'), 'QBU88': ('qbu88', 'bolt'), 'RFB': ('rfb', 'bolt'),
    'SCAR-H_SV': ('scar', 'bolt'), 'M82A3': ('m82a3', 'bolt'),
    'SRS': ('srs308_boltback', 'back'), 'FY_JS': ('fy_js_boltback', 'back'), 'Scout': ('scout_boltback', 'back'),
    'SV98': ('sv98_boltback', 'back'),
}
BORROW = 'g36'
# Not here: the pump shotguns (870, M1014, QBS-09, SPAS-12) play their own pump event at the rear stop; the SVD-12 has no
# reload set and no reload_1p wave of its own (re/notes/sound.md).
OTHER = {
    'cloth.wav': ('sound_weapons_shared_content_handling_generic_deploy_wave_v0.wav', 0.6, FOLEY_GAIN_DB),
    'pin.wav': ('sound_weapons_handheld_m67_m67_remove_safety_pin_wave.wav', 0.5, FOLEY_GAIN_DB),
    os.path.join('M320', 'magazine_out.wav'): ('sound_weapons_reload_1p_reload_1p_m320_opentube_wave.wav', 0.8, GAIN_DB),
    os.path.join('M320', 'magazine_in.wav'): ('sound_weapons_reload_1p_reload_1p_m320_grenadein_wave.wav', 0.8, GAIN_DB),
}


def wave_path(name):
    for suffix in ('_wave.wav', '_wave_v0.wav'):
        path = os.path.join(WAVS, f'sound_weapons_reload_1p_reload_1p_{name}{suffix}')
        if os.path.exists(path):
            return path
    return None


def load(path):
    rate, x = wavfile.read(path)
    x = x.astype(np.float32) / (32768.0 if x.dtype == np.int16 else 1.0)
    return rate, x.mean(axis=1) if x.ndim > 1 else x


def envelope(x, rate):
    hop = rate // 100
    count = len(x) // hop
    return 20 * np.log10(np.sqrt((x[:count * hop].reshape(count, hop) ** 2).mean(axis=1)) + 1e-9)


def onsets_of(path):
    """The wave's sharp rises as the decoder measured them (wav\numbers.txt "sounds at"), in seconds."""
    name = os.path.basename(path)[:-4]
    with open(os.path.join(WAVS, 'numbers.txt'), encoding='utf-8', errors='replace') as handle:
        lines = handle.read().splitlines()
    for i, line in enumerate(lines):
        if line.startswith(name + ' ') and i + 1 < len(lines) and 'sounds at' in lines[i + 1]:
            return [float(t.split('s(')[0]) for t in lines[i + 1].split('sounds at')[1].split()]
    return []


def parts(x, rate, path):
    """Each sound in the wave: (start s, end s, its loudest 10 ms in dB), from one rise to the next."""
    db = envelope(x, rate)
    times = onsets_of(path)
    out = []
    for k, t in enumerate(times):
        end = times[k + 1] if k + 1 < len(times) else len(db) / 100.0
        out.append((t, end, float(db[int(t * 100):max(int(t * 100) + 1, int(end * 100))].max())))
    return out


def rear_stop(x, rate, how, path):
    """(start, end) seconds of the rear stop in a bolt wave, or None when the wave starts at the release: 'bolt', the
    sound before the loudest one (the slam home), at least 80 ms before it; 'back', the loudest one. 10 ms before the
    rise, at most 0.25 s."""
    found = parts(x, rate, path)
    if not found:
        return None
    loudest = max(range(len(found)), key=lambda k: found[k][2])
    if how == 'back':
        start, end, _ = found[loudest]
    else:
        before = [p for p in found[:loudest] if found[loudest][0] - p[0] >= 0.08 and p[2] >= found[loudest][2] - 30.0]
        if not before:
            return None
        start, end = before[-1][0], found[loudest][0] - 0.015
    return max(0.0, start - 0.01), min(end, start + 0.25)


def write(path, x, rate, gain_db):
    x = x.copy()
    fade_in, fade_out = min(len(x), rate // 200), min(len(x), rate // 50)
    x[:fade_in] *= np.linspace(0.0, 1.0, fade_in)
    x[-fade_out:] *= np.linspace(1.0, 0.0, fade_out)
    x *= 10.0 ** (gain_db / 20.0)
    os.makedirs(os.path.dirname(path), exist_ok=True)
    wavfile.write(path, rate, (np.clip(x, -1.0, 1.0) * 32767.0).astype(np.int16))


def decode():
    if not os.path.exists(EXE):
        subprocess.run(['cmake', '-S', os.path.join(HERE, 'gamesounds'), '-B', os.path.dirname(os.path.dirname(EXE)), '-A', 'x64'], check=True)
        subprocess.run(['cmake', '--build', os.path.dirname(os.path.dirname(EXE)), '--config', 'Release'], check=True)
    if os.path.exists(wave_path('ak12_bolt') or ''):
        return
    args = [EXE, '--game', GAME, '--out', DECODED]
    for pattern in PATTERNS:
        args += ['--wav', pattern]
    subprocess.run(args, check=True)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--dry-run', action='store_true')
    parser.add_argument('--remove', action='store_true')
    parser.add_argument('--sounds', default=SOUNDS)
    args = parser.parse_args()
    targets = [os.path.join(folder, 'bolt_back.wav') for folder in REAR_STOP] + ['bolt_back.wav'] + list(OTHER)
    if args.remove:
        for target in targets:
            path = os.path.join(args.sounds, target)
            if os.path.exists(path):
                os.remove(path)
        print('removed the cues from', args.sounds)
        return
    decode()
    borrowed = None
    path = wave_path(BORROW + '_bolt')
    rate, x = load(path)
    cut = rear_stop(x, rate, 'bolt', path)
    borrowed = (x[int(cut[0] * rate):int(cut[1] * rate)], rate, f'the G36C {cut[0]:.2f}-{cut[1]:.2f} s')
    rows = []
    for folder, (name, how) in REAR_STOP.items():
        source = wave_path(name if how == 'back' else name + '_bolt')
        clip = None
        if how != 'borrow' and source:
            rate, x = load(source)
            cut = rear_stop(x, rate, how, source)
            if cut:
                clip = (x[int(cut[0] * rate):int(cut[1] * rate)], rate, f'{name}{"" if how == "back" else "_bolt"} {cut[0]:.2f}-{cut[1]:.2f} s')
        if clip is None:
            clip = (borrowed[0], borrowed[1], borrowed[2] + (' (its own starts at the release)' if source or how == 'borrow' else ' (no wave of its own)'))
        rows.append((folder, clip[2]))
        if not args.dry_run:
            write(os.path.join(args.sounds, folder, 'bolt_back.wav'), clip[0], clip[1], GAIN_DB)
    if not args.dry_run:
        write(os.path.join(args.sounds, 'bolt_back.wav'), borrowed[0], borrowed[1], GAIN_DB)  # any other gun
    rows.append(('(any other gun)', borrowed[2]))
    for target, (source, seconds, gain) in OTHER.items():
        path = os.path.join(WAVS, source)
        if not os.path.exists(path):
            rows.append((target, 'MISSING ' + source))
            continue
        rate, x = load(path)
        times = onsets_of(path)
        start = max(0.0, times[0] - 0.01) if times else 0.0
        clip = x[int(start * rate):int(start * rate) + int(seconds * rate)]
        rows.append((target, f'{source[:-4]} {start:.2f}-{start + len(clip) / rate:.2f} s'))
        if not args.dry_run:
            write(os.path.join(args.sounds, target), clip, rate, gain)
    for target, what in rows:
        print(f'{target:22s} {what}')
    print(('would write' if args.dry_run else 'wrote') + f' {len(rows)} cues to {args.sounds}')


if __name__ == '__main__':
    main()
