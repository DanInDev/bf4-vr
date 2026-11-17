"""Makes one of the mod's sound cues (weapon/reload_sounds: bfvr_sounds\\<cue>.wav beside bfvr.ini) from a recording
of the game's own sound, so a step the gun's own sound set has no sound for borrows another one of the game's
(re/notes/sound.md "Borrowed cues"). The recordings come from tools/sound_probe.py and stay on this machine: game audio
never goes into git, and the WAVs live in the game folder.

    python -E -P tools\\cue_wav.py list
    python -E -P tools\\cue_wav.py make <cue> [--source <clip.wav>] [--gain-db 0] [--folder <bfvr_sounds>]
    python -E -P tools\\cue_wav.py remove <cue>        (the synthesised cue plays again)

`make` cuts the sound out of the clip (from 10 ms before it rises 30 dB over the clip's quietest part to where it has
fallen back, with a 20 ms fade), writes 16-bit PCM at the clip's rate (PlaySound plays it), and prints what it took.
"""
import argparse
import os
import sys

import numpy as np
from scipy.io import wavfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
SOUND = os.path.join(ROOT, 're', 'dumps', 'sound')
FOLDER = r'E:\EA\Battlefield 4\bfvr_sounds'

# cue: (default clip, what it is, how to record it again)
SOURCES = {
    # A rifle's charging handle has no rear-stop sound in Weapon_SourcePatch_Reload_AR; the Scout's bolt-action set has
    # four bolt parts. 526FE9C4 is the one short single knock (one peak, 0.25 s), the closest to a handle hitting its stop.
    'bolt_back': (os.path.join(SOUND, 'patch_Scout', 'stages-04.wav'), "the Scout's bolt part 526FE9C4",
                  'sound_probe.py stages --weapon Scout --out re\\dumps\\sound\\patch_Scout (step 04)'),
    # The cloth of a weapon switch (the game's own, heard on every switch), for a shell or a belt taken from the pouch.
    'cloth': (os.path.join(SOUND, 'switch', 'switch-01.wav'), 'the weapon switch (Y)',
              'sound_probe.py switch --out re\\dumps\\sound\\switch (BF4 in front: switches need focus) (step 01)'),
}
CUES = ('cloth', 'magazine_in', 'magazine_out', 'chamber', 'pin', 'bolt_back', 'bolt_home')  # reload_sounds name_of


def envelope(samples, hop):
    count = len(samples) // hop
    return 20 * np.log10(np.sqrt((samples[:count * hop].reshape(count, hop) ** 2).mean(axis=1)) + 1e-9)


def denoise(samples, rate, noise, strength=2.0):
    """Spectral gate: the level's ambience and music (in the recordings only ~17-20 dB under a reload sound) taken out
    per frequency, with the clip's quiet part (`noise`, a boolean mask of 10 ms frames) as the noise profile."""
    from scipy.signal import istft, stft
    size = 1024
    _, _, spec = stft(samples, rate, nperseg=size)
    times = np.arange(spec.shape[1]) * (size // 2) / rate
    quiet = noise[np.minimum((times * 100).astype(int), len(noise) - 1)]
    profile = np.abs(spec[:, quiet]).mean(axis=1, keepdims=True) if quiet.any() else np.zeros((spec.shape[0], 1))
    magnitude = np.abs(spec)
    gain = np.clip((magnitude - strength * profile) / (magnitude + 1e-12), 0.05, 1.0)
    _, out = istft(spec * gain, rate, nperseg=size)
    return out[:len(samples)].astype(np.float32)


def cut(samples, rate):
    """The sound in a clip: the frames more than 8 dB over the clip's ambience (its median level), from 10 ms before
    the first to 30 ms after the last, the ambience gated out, 5 ms fade-in and 20 ms fade-out."""
    if samples.ndim > 1:
        samples = samples.mean(axis=1)
    scale = 32768.0 if samples.dtype == np.int16 else 1.0
    samples = samples.astype(np.float32) / scale
    hop = rate // 100
    db = envelope(samples, hop)
    floor = float(np.median(db))
    peak = float(db.max())
    loud = np.nonzero(db > floor + 8.0)[0]
    if not len(loud) or peak < floor + 12.0:
        raise SystemExit(f'no sound in the clip (peak {peak:.1f}, ambience {floor:.1f} dBFS)')
    # The sound's frames: the loud ones around its peak, gaps of at most 80 ms, at most 0.6 s (an ambience bump or a
    # later knock apart from it is not part of it).
    start = end = int(np.argmax(db))
    for frame in loud[::-1]:
        if start - 8 <= frame < start and end - frame <= 60:
            start = int(frame)
    for frame in loud:
        if end < frame <= end + 8 and frame - start <= 60:
            end = int(frame)
    clean = denoise(samples, rate, db <= floor + 3.0)
    first = max(0, start - 1) * hop
    last = min(len(samples), (end + 3) * hop)
    sound = clean[first:last].copy()
    sound[:rate // 200] *= np.linspace(0.0, 1.0, rate // 200)
    fade = min(len(sound), rate // 50)
    sound[-fade:] *= np.linspace(1.0, 0.0, fade)
    return sound, first / rate, last / rate, peak, peak - floor


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('action', choices=('list', 'make', 'remove'))
    parser.add_argument('cue', nargs='?')
    parser.add_argument('--source')
    parser.add_argument('--gain-db', type=float, default=0.0)
    parser.add_argument('--folder', default=FOLDER)
    args = parser.parse_args()
    if args.action == 'list':
        for name in CUES:
            path = os.path.join(args.folder, name + '.wav')
            source = SOURCES.get(name)
            state = 'WAV' if os.path.exists(path) else 'synthesised' + (' (silent: bolt_back plays only a WAV)' if name == 'bolt_back' else '')
            print(f'{name:13s} {state:50s} {"from " + source[1] if source else ""}')
        return
    if args.cue not in CUES:
        raise SystemExit(f'cue must be one of {", ".join(CUES)}')
    target = os.path.join(args.folder, args.cue + '.wav')
    if args.action == 'remove':
        if os.path.exists(target):
            os.remove(target)
        print('removed', target)
        return
    source = args.source or (SOURCES[args.cue][0] if args.cue in SOURCES else None)
    if not source or not os.path.exists(source):
        hint = SOURCES.get(args.cue, (None, None, 'record it with tools/sound_probe.py'))[2]
        raise SystemExit(f'no recording at {source}: {hint}')
    rate, samples = wavfile.read(source)
    sound, start, end, peak, rise = cut(samples, rate)
    sound *= 10.0 ** (args.gain_db / 20.0)
    os.makedirs(args.folder, exist_ok=True)
    wavfile.write(target, rate, (np.clip(sound, -1.0, 1.0) * 32767.0).astype(np.int16))
    print(f'{args.cue}: {start:.2f}-{end:.2f} s of {source} ({len(sound) / rate:.2f} s, peak {peak:.1f} dBFS, '
          f'{rise:.0f} dB over the ambience, gated out'
          f'{f", {args.gain_db:+.1f} dB" if args.gain_db else ""}) -> {target}')


if __name__ == '__main__':
    main()
