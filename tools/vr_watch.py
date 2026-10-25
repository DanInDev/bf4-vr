"""Watch a headset test: print VR session changes and notable log lines, record timing to CSV.

    python -E -P tools\\vr_watch.py                       until stopped (Ctrl+C)
    python -E -P tools\\vr_watch.py --csv re\\dumps\\vr-test.csv --interval 1

Each line printed is an event (session phase/state, stereo frames starting or stopping, pad
connection, fps drops, new theater/controller/safety/camera/arms log lines). Every sample goes to
the CSV for later analysis (fps, frame timings, submitted frames, eye images). Reads only.
"""
import argparse
import csv
import os
import re
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)  # `python -P` leaves the script folder off the path

from camera_probe import command  # noqa: E402

LOG = r'E:\EA\Battlefield 4\bfvr.log'
LOG_PATTERN = re.compile(r'theater:|controllers: profile|safety:|virtual pad: (offered|IsXInputDevice\(bf-vr)|camera: |arms: |error|failed', re.I)


def parse(status):
    values = {}
    for key, value in re.findall(r'(\w+)=([^\s]+)', status):
        values.setdefault(key, value)
    return values


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--csv', default=os.path.join(HERE, '..', 're', 'dumps', 'vr-test.csv'))
    parser.add_argument('--interval', type=float, default=1.0)
    args = parser.parse_args()

    log_position = os.path.getsize(LOG) if os.path.exists(LOG) else 0
    last = {}
    last_frames = None
    with open(args.csv, 'a', newline='') as out:
        writer = csv.writer(out)
        writer.writerow(['time', 'fps', 'theater', 'session_state', 'presents', 'submitted', 'max_sync_ms', 'max_frame_ms',
                         'eye_images', 'pad_connected', 'copies'])
        print('watching (pipe + bfvr.log)', flush=True)
        while True:
            try:
                status = command('status')
            except RuntimeError:
                if last.get('alive', True):
                    print('game: control pipe not available (BF4 not running or still starting)', flush=True)
                last = {'alive': False}
                time.sleep(max(args.interval, 2.0))
                continue
            values = parse(status)
            now = time.strftime('%H:%M:%S')
            writer.writerow([now, values.get('fps'), values.get('theater'), values.get('session_state'), values.get('presents'),
                             values.get('submitted'), values.get('max_sync_ms'), values.get('max_frame_ms'),
                             values.get('eye_images'), values.get('connected'), values.get('copies')])
            out.flush()

            changes = []
            for key in ('theater', 'session_state', 'connected'):
                if values.get(key) != last.get(key):
                    changes.append(f'{key}={values.get(key)}')
            frames = int(values.get('eye_images', '0') or 0)
            stereo_running = last_frames is not None and frames > last_frames
            if stereo_running != last.get('stereo_running'):
                changes.append('stereo frames ' + ('flowing' if stereo_running else 'stopped'))
            stage_ms = float(values.get('stage_ms', '0') or 0)
            if stage_ms > 1000:
                changes.append(f"STUCK in '{values.get('stage')}' for {stage_ms / 1000:.1f} s")
            fps = float(values.get('fps', '0') or 0)
            if fps and fps < 50 and last.get('fps_ok', True):
                changes.append(f'fps dropped to {fps:.0f}')
            if changes or not last.get('alive', False):
                print(f'{now} ' + ', '.join(changes or ['connected to the game']) + f' (fps {fps:.0f}, frame {values.get("max_frame_ms")} ms)', flush=True)
            last = dict(values, alive=True, stereo_running=stereo_running, fps_ok=fps >= 50 or not fps)
            last_frames = frames

            if os.path.exists(LOG):
                size = os.path.getsize(LOG)
                if size < log_position:  # new game session: log was recreated
                    log_position = 0
                if size > log_position:
                    with open(LOG, 'r', encoding='utf-8', errors='replace') as log:
                        log.seek(log_position)
                        for line in log.read().splitlines():
                            if LOG_PATTERN.search(line):
                                print('log ' + line.strip(), flush=True)
                    log_position = size
            time.sleep(args.interval)


if __name__ == '__main__':
    main()
