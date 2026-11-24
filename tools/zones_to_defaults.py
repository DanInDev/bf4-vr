"""#152: make the grab zones tuned in the headset the defaults for everyone.

    python -E -P tools\\zones_to_defaults.py [--file "E:\\EA\\Battlefield 4\\bfvr_zones.ini"] [--check]

Copies the tuned lines of a bfvr_zones.ini (panel Weapon > Tune grab zones > Save) into the built-in table in
src/weapon/zone_defaults.cpp, which the mod reads before the player's own bfvr_zones.ini. Lines are
"<gun> <zone> <right> <up> <forward> <size>" (zone handle | magazine | lid); comments and blank lines are left out,
anything else is refused. --check only reports what would change. Then build, gate, and commit the .cpp.
"""
import argparse
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TARGET = os.path.join(ROOT, 'src', 'weapon', 'zone_defaults.cpp')
DEFAULT_FILE = r'E:\EA\Battlefield 4\bfvr_zones.ini'
LINE = re.compile(r'^(U_[A-Za-z0-9_\-]+) (handle|magazine|lid)( -?\d+(\.\d+)?){4}$')
HEADER = '# Built-in grab-zone tunes (tools/zones_to_defaults.py). "<gun> <zone> <right> <up> <forward> <size>".'


def tuned_lines(path):
    lines, bad = [], []
    with open(path, encoding='utf-8') as source:
        for number, raw in enumerate(source, 1):
            line = ' '.join(raw.split())
            if not line or line.startswith('#'):
                continue
            (lines if LINE.match(line) else bad).append(line if LINE.match(line) else f'{number}: {raw.rstrip()}')
    return lines, bad


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--file', default=DEFAULT_FILE, help='the saved bfvr_zones.ini')
    parser.add_argument('--check', action='store_true', help='report only, write nothing')
    args = parser.parse_args()
    if not os.path.isfile(args.file):
        sys.exit(f'no {args.file}: tune the zones in the headset and press Save on the panel first')
    lines, bad = tuned_lines(args.file)
    if bad:
        sys.exit('refused, lines that are not "<gun> <zone> <right> <up> <forward> <size>":\n  ' + '\n  '.join(bad))
    with open(TARGET, encoding='utf-8', newline='') as source:
        text = source.read()
    newline = '\r\n' if '\r\n' in text else '\n'
    start = text.index('R"zones(') + len('R"zones(')
    end = text.index(')zones"')
    old = [l for l in text[start:end].splitlines() if l and not l.startswith('#')]
    block = newline.join([HEADER] + sorted(lines)) + newline
    print(f'{len(lines)} tuned zones in {args.file} ({len(old)} built in now)')
    for line in sorted(set(lines) - set(old)):
        print('  + ' + line)
    for line in sorted(set(old) - set(lines)):
        print('  - ' + line)
    if args.check:
        return
    with open(TARGET, 'w', encoding='utf-8', newline='') as target:
        target.write(text[:start] + block + text[end:])
    print(f'wrote {os.path.relpath(TARGET, ROOT)}: build, run tools\\gate.ps1 and commit it')


if __name__ == '__main__':
    main()
