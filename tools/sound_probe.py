"""Sound research for physical reload (#30, re/notes/sound.md). The game must run with the DLL in a
campaign level (research = 1); nothing else may drive it meanwhile.

    python -E -P tools\\sound_probe.py reference   game actions through the pad (button reload)
    python -E -P tools\\sound_probe.py physical    physical reload steps through the pipe only
    python -E -P tools\\sound_probe.py <scenario> --out <folder>

Records the default playback device (build/bf4/Debug/bfvr_audio_loopback.exe) while the pipe drives
one step at a time with silence between, then prints per step: the onset after the step, the peak
level over the level just before it, and the reference step whose sound is most alike (spectral
profile). Writes <scenario>.wav, .events.csv, one clip per step, and references.json (profiles of
the reference scenario, used by later runs). Recordings stay out of git (no game audio in the repo).
"""
import argparse
import csv
import json
import os
import subprocess
import sys
import time

import numpy as np
from scipy.io import wavfile
from scipy.signal import spectrogram

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)  # `python -P` leaves the script folder off the path

from camera_probe import command  # noqa: E402

ROOT = os.path.dirname(HERE)
RECORDER = os.path.join(ROOT, 'build', 'bf4', 'Debug', 'bfvr_audio_loopback.exe')

# XINPUT button bits
X, Y, DPAD_UP, LB, RB, A, B = 0x4000, 0x8000, 0x0001, 0x0100, 0x0200, 0x1000, 0x2000


def pad(buttons, ms=150, right_trigger=0.0, left_trigger=0.0):
    return f'pad 0 0 0 0 {ms} {buttons:x} {left_trigger} {right_trigger}'


# (label, pipe commands, seconds to wait after the step)
SCENARIOS = {
    'reference': [
        ('setup: button reload', ['setting set reload button'], 3.0),
        ('fire one burst (RT 0.2 s)', [pad(0, 200, 1.0)], 3.0),
        ('reload button (X)', [pad(X)], 5.0),
        ('accessory toggle (D-pad up)', [pad(DPAD_UP)], 2.5),
        ('accessory toggle back', [pad(DPAD_UP)], 2.5),
        ('switch weapon (Y)', [pad(Y)], 3.0),
        ('switch back (Y)', [pad(Y)], 3.0),
    ],
    # Buttons a hand can press by accident while reloading. LB (the quick-throw grenade) is left out:
    # it throws a live grenade.
    'buttons': [
        ('setup: button reload', ['setting set reload button'], 3.0),
        ('gun-hand grip tap (RB 0.15 s)', [pad(RB)], 4.0),
        ('gun-hand grip hold (RB 1.2 s)', [pad(RB, 1200)], 5.0),
        ('reload button tap (X), physical off', [pad(X, 80)], 5.0),
        ('off-hand trigger tap (LT 0.15 s)', [pad(0, 150, 0.0, 1.0)], 3.0),
        ('off-hand trigger hold (LT 1.2 s)', [pad(0, 1200, 0.0, 1.0)], 3.0),
        ('B tap (crouch)', [pad(B)], 3.0),
        ('B tap again (stand)', [pad(B)], 3.0),
    ],
    # grips_to_game (#30): the grips kept from the game, then passed through again.
    'grips': [
        ('setup: grips_to_game off', ['setting set grips_to_game 0'], 3.0),
        ('RB tap, grips off', [pad(RB)], 3.5),
        ('RB hold, grips off', [pad(RB, 1200)], 4.0),
        ('grips_to_game on', ['setting set grips_to_game 1'], 2.5),
        ('RB tap, grips on', [pad(RB)], 3.5),
        ('RB hold, grips on', [pad(RB, 1200)], 4.0),
        ('grips_to_game back off', ['setting set grips_to_game 0'], 2.0),
    ],
    # #78 (SCAR-H in hand, physical reload): the bolt and the magazine-out sounds with an EMPTY magazine.
    'empty_ar': [
        ('setup: physical reload', ['setting set reload physical'], 3.0),
        ('take a magazine', ['reload take'], 2.5),
        ('insert it', ['reload insert'], 2.5),
        ('bolt catch: chambers (reference bolt)', ['reload catch'], 2.5),
        ('fire the state empty (no sound expected)', ['reload fire'] * 40, 2.5),
        ('rack on the empty magazine (#78: bolt)', ['reload rack'], 2.5),
        ('take the new one first', ['reload take'], 2.5),
        ('release the empty one, new one in hand (#78: magazine out)', ['reload button'], 2.5),
        ('insert the new one', ['reload insert'], 2.5),
        ('bolt catch: chambers', ['reload catch'], 2.5),
        ('fire the state empty again (no sound expected)', ['reload fire'] * 40, 2.5),
        ('pull the empty one into the hand (magazine out)', ['reload catch'], 2.5),
        ('push the same empty one back in', ['reload insert'], 2.5),
        ('bolt catch on the empty magazine (#78: bolt)', ['reload button'], 2.5),
    ],
    # #78 (AK-12 in hand): the magazine pulled out by hand when it is empty.
    'empty_ak': [
        ('setup: physical reload', ['setting set reload physical'], 3.0),
        ('fire the state empty (no sound expected)', ['reload fire'] * 40, 2.5),
        ('pull the empty magazine out (#78: magazine out)', ['reload grab'], 2.5),
        ('drop it', ['reload letgo'], 2.5),
        ('take a full one', ['reload take'], 2.5),
        ('insert it', ['reload insert'], 2.5),
        ('rack: chambers (bolt)', ['reload rack'], 2.5),
    ],
    # #78: does the game's reload sound play a magazine-out that follows a pouch take (settle)? AK-12 in hand.
    'order_ak': [
        ('setup: physical reload', ['setting set reload physical'], 3.0),
        ('pull the magazine out, nothing before (reference)', ['reload grab'], 3.0),
        ('drop it', ['reload letgo'], 3.0),
        ('take one from the pouch (settle)', ['reload take'], 3.0),
        ('insert it', ['reload insert'], 3.0),
        ('take the next one first (settle)', ['reload take'], 4.0),
        ('stow it back (settle)', ['reload stow'], 4.0),
        ('pull the magazine out after two settles', ['reload grab'], 3.0),
        ('insert it again', ['reload insert'], 3.0),
        ('pull it out once more, nothing just before', ['reload grab'], 3.0),
        ('insert', ['reload insert'], 3.0),
    ],
    # #82: the grenade pin pull at three volumes against a magazine insert (AK-12 in hand, physical reload).
    'pin': [
        ('setup: physical reload', ['setting set reload physical'], 3.0),
        ('reference: pull the magazine out', ['reload grab'], 2.5),
        ('reference: drop it', ['reload letgo'], 2.5),
        ('reference: take one', ['reload take'], 2.5),
        ('reference: magazine insert', ['reload insert'], 2.5),
        ('set pin_volume 1 (a setting applies on the next frame)', ['setting set pin_volume 1'], 1.0),
        ('pin, volume 1', ['grenade pinsound'], 2.5),
        ('set pin_volume 0.35', ['setting set pin_volume 0.35'], 1.0),
        ('pin, volume 0.35 (default)', ['grenade pinsound'], 2.5),
        ('set pin_volume 0', ['setting set pin_volume 0'], 1.0),
        ('pin, volume 0', ['grenade pinsound'], 2.5),
        ('restore pin_volume 0.35', ['setting set pin_volume 0.35'], 1.0),
    ],
    # AR variant (SCAR-H in hand): the release button drops the magazine out of the gun.
    'physical_ar': [
        ('setup: physical reload', ['setting set reload physical'], 3.0),
        ('release button (drop)', ['reload button'], 2.5),
        ('take from chest', ['reload take'], 2.5),
        ('insert', ['reload insert'], 2.5),
        ('rack', ['reload rack'], 2.5),
        ('release with the hand on it', ['reload catch'], 2.5),
        ('stow at chest', ['reload stow'], 2.5),
        ('back to button reload', ['setting set reload button'], 2.0),
    ],
    'physical': [
        ('setup: physical reload', ['setting set reload physical'], 3.0),
        ('grab magazine (pull out)', ['reload grab'], 2.5),
        ('let go (drop)', ['reload letgo'], 2.5),
        ('take from chest', ['reload take'], 2.5),
        ('insert', ['reload insert'], 2.5),
        ('rack', ['reload rack'], 2.5),
        ('grab again', ['reload grab'], 2.5),
        ('stow at chest', ['reload stow'], 2.5),
        ('take again', ['reload take'], 2.5),
        ('insert again', ['reload insert'], 2.5),
        ('fire one shot (pipe)', ['reload fire'], 2.5),
        ('back to button reload', ['setting set reload button'], 2.0),
    ],
}


# Input events of Weapon_SourcePatch_Reload_AR (tools/weapon_sound_probe.py on the reload entity):
# two named by the hash dictionary, six not.
RELOAD_EVENTS = [('Grab', 0x7C7F4233), ('Bolt', 0x7C80C030), ('0c87faa9', 0x0C87FAA9), ('1c998f75', 0x1C998F75),
                 ('9d874880', 0x9D874880), ('c46cf22f', 0xC46CF22F), ('abea067c', 0xABEA067C), ('95e4238c', 0x95E4238C)]


def stage_steps(name):
    """Each graph input event of the reload SoundEntity whose sound name contains `name`, sent to it: the AR list for
    the AR patch, else the patch's own events (sound_census.input_events: shotguns, LMGs, Reload_5 ... have their own)."""
    import struct as _struct
    from camera_probe import peek
    from reload_sound_probe import reload_entities
    from sound_census import input_events
    entity = next(e for e, n, _, _ in reload_entities() if name.lower() in n.lower())
    q = lambda at: _struct.unpack('<Q', peek(at, 8))[0]
    patch = q(q(q(entity + 0x28) + 0x18) + 0x28)
    events = input_events(patch) if patch else []
    known = dict((value, label) for label, value in RELOAD_EVENTS)
    listed = [(known.get(e, f'{e:08x}'), e) for e in events] or RELOAD_EVENTS
    return [(f'reload event {label}', [f'entityevent {entity:x} {value:x}'], 3.0) for label, value in listed]


def verify_steps(gun, steps):
    """The physical reload's steps for `gun` through the pipe (reload = physical): does each now sound (the game's)?"""
    out = [('setup', ['setting set reload physical', 'givegun U_AK12 1'], 4.0), (f'give {gun}', [f'givegun {gun} 1'], 14.0)]
    return out + [(f'reload {step}', [f'reload {step}'], 2.5) for step in steps.split(',')]


VERIFY = {
    'U_870': 'fire,rearstop,rack,take,insert,take,insert',
    'U_M249': 'lidopen,belttake,beltlay,lidclose',
    'U_MP412Rex': 'fire,button,take,insert,lidclose',
    'U_Scout': 'fire,rearstop,rack',
    'U_AK12': 'rearstop,rack,rearstop',  # the rear stop from the game's files (tools/game_cues.py)
    'U_SCAR-H': 'rearstop,rack',
    'U_M416': 'rearstop,rack',  # the G36C's
    'U_UTAS': 'fire,rack,take,insert',
}


def game_empty_steps(gun):
    """The game's own empty reload of `gun` (given fresh into the slot in hand, button reload, the game's hands)."""
    return [('setup', ['setting set reload button', 'setting set arm_ik 0', 'givegun U_AK12 1'], 4.0),
            (f'give {gun}', [f'givegun {gun} 1'], 6.0),
            ('empty reload (setammo 0)', ['reload setammo 0'], 11.0),
            ('restore', ['setting set reload physical', 'setting set arm_ik 1'], 1.0)]


def bursts(wav, step_label):
    """The separate sounds inside one step (onset > 10 dB over the quiet before, after 0.15 s under it): time after the
    step, peak, spectral profile."""
    rate, samples, start, events = load(wav)
    db = envelope_db(samples, rate)
    index = next(i for i, (_, label, _) in enumerate(events) if label == step_label)
    begin = int((events[index][0] - start) / 10.0)
    end = int((events[index + 1][0] - start) / 10.0) if index + 1 < len(events) else len(db)
    # The step's own quiet level (its 20th percentile): the level before it can still hold the gun pickup's sound.
    floor = float(np.percentile(db[begin:end], 20))
    out, quiet = [], 10
    for frame in range(begin, end):
        if db[frame] > floor + 9.0 and quiet >= 10:
            first = frame * rate // 100
            clip = samples[max(0, first - rate // 50): first + rate // 2]
            vector = profile(clip, rate)
            if vector is not None:
                out.append(((frame - begin) / 100.0, float(db[frame:frame + 30].max()), vector))
            quiet = 0
        quiet = quiet + 1 if db[frame] <= floor + 6.0 else 0
    return out


def match(stages_wav, game_wav):
    """Each event's sound against the sounds of the game's own reload: which moment of the reload it is."""
    targets = bursts(game_wav, 'empty reload (setammo 0)')
    print(f'game reload: {len(targets)} sounds at ' + ', '.join(f'{t:.2f}s' for t, _, _ in targets))
    rate, samples, start, events = load(stages_wav)
    db = envelope_db(samples, rate)
    for index, (at, label, _) in enumerate(events):
        if not label.startswith('reload event'):
            continue
        begin = int((at - start) / 10.0)
        end = int((events[index + 1][0] - start) / 10.0) if index + 1 < len(events) else len(db)
        floor = float(np.median(db[max(0, begin - 50):begin]))
        loud = np.nonzero(db[begin:end] > floor + 10.0)[0]
        if not len(loud):
            print(f'{label:28s} silent')
            continue
        first = (begin + int(loud[0])) * rate // 100
        vector = profile(samples[max(0, first - rate // 50): first + rate // 2], rate)
        scores = sorted(((float(np.dot(vector, v)), t) for t, _, v in targets), reverse=True)[:3]
        print(f'{label:28s} +{float(db[begin:end].max()) - floor:5.1f} dB  like the reload at ' + ', '.join(f'{t:.2f}s ({s:.2f})' for s, t in scores))


def record(scenario, out):
    steps = SCENARIOS[scenario]
    seconds = 2.0 + sum(wait for _, _, wait in steps) + 1.0
    wav = os.path.join(out, f'{scenario}.wav')
    recorder = subprocess.Popen([RECORDER, f'{seconds:.1f}', wav], stdout=subprocess.PIPE, text=True)
    time.sleep(2.0)
    events = []
    for label, lines, wait in steps:
        at = time.time() * 1000.0
        replies = [command(line).strip().splitlines()[0][:100] for line in lines]
        events.append((at, label, ' | '.join(replies)))
        print(f'{label:32s} {replies[0] if replies else ""}')
        time.sleep(wait)
    print(recorder.communicate()[0].strip())
    with open(wav + '.events.csv', 'w', newline='') as handle:
        csv.writer(handle).writerows(events)
    return wav


def load(wav):
    rate, samples = wavfile.read(wav)
    with open(wav + '.start') as handle:
        start = float(handle.read())
    with open(wav + '.events.csv', newline='') as handle:
        events = [(float(at), label, reply) for at, label, reply in csv.reader(handle)]
    return rate, samples.astype(np.float32), start, events


def envelope_db(samples, rate, frame=0.01):
    size = int(rate * frame)
    count = len(samples) // size
    blocks = samples[:count * size].reshape(count, size)
    return 20 * np.log10(np.sqrt((blocks ** 2).mean(axis=1)) + 1e-9)


def profile(clip, rate):
    if len(clip) < 2048:
        return None
    freqs, _, power = spectrogram(clip, rate, nperseg=2048)
    bands = np.logspace(np.log10(150), np.log10(16000), 25)  # each band wider than a 23 Hz bin
    values = [power[(freqs >= lo) & (freqs < hi)].mean() for lo, hi in zip(bands[:-1], bands[1:])]
    vector = np.log10(np.asarray(values) + 1e-12)
    vector -= vector.mean()
    return vector / (np.linalg.norm(vector) + 1e-9)


def analyse(wav, references):
    rate, samples, start, events = load(wav)
    db = envelope_db(samples, rate)
    out_dir = os.path.dirname(wav)
    profiles = {}
    print(f'\n{"step":32s} {"onset s":>8s} {"peak dB":>8s} {"rise dB":>8s}  most alike')
    for index, (at, label, _) in enumerate(events):
        begin = int((at - start) / 10.0)
        end = int((events[index + 1][0] - start) / 10.0) if index + 1 < len(events) else len(db)
        before = db[max(0, begin - 50):begin]
        floor = float(np.median(before)) if len(before) else -120.0
        window = db[begin:end]
        if not len(window):
            continue
        loud = np.nonzero(window > floor + 10.0)[0]
        peak = float(window.max())
        if not len(loud):
            print(f'{label:32s} {"-":>8s} {peak:8.1f} {peak - floor:8.1f}  (nothing above the level before)')
            continue
        onset = int(loud[0])
        first = (begin + onset) * rate // 100
        clip = samples[max(0, first - rate // 20): first + rate]
        wavfile.write(os.path.join(out_dir, f'{os.path.basename(wav)[:-4]}-{index:02d}.wav'), rate, clip)
        vector = profile(clip, rate)
        if vector is not None:
            profiles[label] = vector.tolist()
        alike = ''
        if vector is not None and references:
            scores = sorted(((float(np.dot(vector, np.asarray(ref))), name) for name, ref in references.items() if name != label), reverse=True)
            alike = ', '.join(f'{name} {score:.2f}' for score, name in scores[:2])
        print(f'{label:32s} {onset / 100.0:8.2f} {peak:8.1f} {peak - floor:8.1f}  {alike}')
    return profiles


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('scenario', choices=sorted(SCENARIOS) + ['stages', 'game_empty', 'match', 'verify'])
    parser.add_argument('--weapon', default='AK12', help='stages: reload entity name part (its folder)')
    parser.add_argument('--gun', default='U_870', help='game_empty: the unlock to give and reload')
    parser.add_argument('--out', default=os.path.join(ROOT, 're', 'dumps', 'sound'))
    parser.add_argument('--analyse-only', action='store_true')
    args = parser.parse_args()
    os.makedirs(args.out, exist_ok=True)
    if args.scenario == 'match':
        match(os.path.join(args.out, 'stages.wav'), os.path.join(args.out, 'game_empty.wav'))
        return
    wav = os.path.join(args.out, f'{args.scenario}.wav')
    if args.scenario == 'stages':
        SCENARIOS['stages'] = stage_steps(args.weapon)
    if args.scenario == 'game_empty':
        SCENARIOS['game_empty'] = game_empty_steps(args.gun)
    if args.scenario == 'verify':
        SCENARIOS['verify'] = verify_steps(args.gun, VERIFY.get(args.gun, 'fire,rack'))
    if not args.analyse_only:
        wav = record(args.scenario, args.out)
    reference_file = os.path.join(args.out, 'references.json')
    references = {}
    if os.path.exists(reference_file):
        with open(reference_file) as handle:
            references = json.load(handle)
    profiles = analyse(wav, references)
    if args.scenario == 'reference':
        with open(reference_file, 'w') as handle:
            json.dump(profiles, handle)


if __name__ == '__main__':
    main()
