"""Step a VR setting through values while the tester watches in the headset (game must run).

    python -E -P tools\\vr_sweep.py viewcrop "1,1 0.9,0.9 0.8,0.8 0.7,0.75 0.6,0.68" --seconds 20
    python -E -P tools\\vr_sweep.py gunscale "1 0.85 0.7 0.55" --seconds 20

Each value is sent through the control pipe (commas become spaces), held for --seconds, and the
setting returns to the first value at the end. Prints a timestamped line per step.
"""
import argparse
import os
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)  # `python -P` leaves the script folder off the path

from camera_probe import command  # noqa: E402


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('setting', choices=('viewcrop', 'gunscale'))
    parser.add_argument('values', help='space-separated steps; use a comma inside one step for two numbers')
    parser.add_argument('--seconds', type=float, default=20.0)
    args = parser.parse_args()

    steps = args.values.split()
    for number, step in enumerate(steps, start=1):
        reply = command(f'{args.setting} {step.replace(",", " ")}')
        print(f'{time.strftime("%H:%M:%S")} step {number}/{len(steps)}: {reply}', flush=True)
        time.sleep(args.seconds)
    reply = command(f'{args.setting} {steps[0].replace(",", " ")}')
    print(f'{time.strftime("%H:%M:%S")} back to step 1: {reply}', flush=True)


if __name__ == '__main__':
    main()
