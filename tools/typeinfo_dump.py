"""Frostbite type registry from the memory dump (local analysis only, output stays in re/dumps/).

    python -E -P tools\\typeinfo_dump.py re\\dumps\\bf4-179547-mem.exe                 write the JSON
    python -E -P tools\\typeinfo_dump.py re\\dumps\\bf4-179547-mem.exe --class SoldierEntityData
    python -E -P tools\\typeinfo_dump.py re\\dumps\\bf4-179547-mem.exe --find Camera
    python -E -P tools\\typeinfo_dump.py re\\dumps\\bf4-179547-mem.exe --vtable 0x141e600c0

Layouts (BF4 build 179547, verified against the dump; see re/notes/typeinfo.md):
  TypeInfo object   +0x00 info data*  +0x08 next*  +0x10 u16 runtime id  +0x12 u16 flags
  ClassInfo object  +0x18 super ClassInfo*  +0x28 u16 class id  +0x2A u16 last subclass id
  info data         +0x00 name*  +0x08 u16 flags  +0x0A u16 size  +0x10 module*
                    +0x18 u8 alignment  +0x1A u16 field count
                    class: +0x20 super*  +0x28 create function  +0x30 fields*
                    value type: +0x28 fields*   enum: +0x20 fields*   array: +0x20 element type*
  field             +0x00 name*  +0x08 u16 flags  +0x0A u16 offset  +0x10 type*   (stride 0x18)
  enum value        +0x00 name*  +0x10 u32 value                                  (stride 0x18)
The list head is the global written by the TypeInfo constructor
(mov rax,[head]; mov [rcx+8],rax; mov [head],rcx).
"""
import argparse
import json
import pathlib
import re
import struct

BASE = 0x140000000
KINDS = ['Void', 'DbObject', 'ValueType', 'Class', 'Array', 'FixedArray', 'String', 'CString', 'Enum',
         'FileRef', 'Boolean', 'Int8', 'Uint8', 'Int16', 'Uint16', 'Int32', 'Uint32', 'Int64', 'Uint64',
         'Float32', 'Float64', 'Guid', 'SHA1', 'ResourceRef']


class Image:
    def __init__(self, path):
        self.data = open(path, 'rb').read()  # image layout: file offset == RVA
        pe = struct.unpack_from('<I', self.data, 0x3C)[0]
        self.timestamp = struct.unpack_from('<I', self.data, pe + 8)[0]
        count = struct.unpack_from('<H', self.data, pe + 6)[0]
        optional = struct.unpack_from('<H', self.data, pe + 20)[0]
        self.sections = {}
        for i in range(count):
            name, size, rva = struct.unpack_from('<8sII', self.data, pe + 24 + optional + i * 40)
            self.sections[name.rstrip(b'\0').decode()] = (BASE + rva, BASE + rva + size)

    def contains(self, va):
        return BASE <= va < BASE + len(self.data) - 8

    def u64(self, va):
        return struct.unpack_from('<Q', self.data, va - BASE)[0]

    def u32(self, va):
        return struct.unpack_from('<I', self.data, va - BASE)[0]

    def u16(self, va):
        return struct.unpack_from('<H', self.data, va - BASE)[0]

    def u8(self, va):
        return self.data[va - BASE]

    def string(self, va):
        if not self.contains(va):
            return None
        start = va - BASE
        return self.data[start:self.data.find(b'\0', start, start + 512)].decode('latin1')


def find_head(image):
    """The global with the longest TypeInfo chain among all constructor-pattern targets."""
    text_begin, text_end = image.sections['.text']
    text = image.data[text_begin - BASE:text_end - BASE]
    candidates = set()
    for match in re.finditer(rb'\x48\x8b\x05(....)\x48\x89\x41\x08\x48\x89\x0d(....)', text, re.S):
        at = text_begin + match.start()
        load = at + 7 + struct.unpack('<i', match.group(1))[0]
        store = at + 18 + struct.unpack('<i', match.group(2))[0]
        if load == store:
            candidates.add(load)

    def chain_length(head):
        node, seen = image.u64(head), set()
        while node and image.contains(node) and node not in seen and len(seen) < 100000:
            seen.add(node)
            node = image.u64(node + 8)
        return len(seen) if node == 0 else 0

    best = max(candidates, key=chain_length, default=None)
    if best is None or chain_length(best) == 0:
        raise SystemExit('TypeInfo list head not found (different build or encrypted dump?)')
    return best


def read_fields(image, address, count, enum):
    fields = []
    for i in range(count):
        entry = address + i * 0x18
        name = image.string(image.u64(entry))
        if enum:
            fields.append({'name': name, 'value': image.u32(entry + 0x10)})
        else:
            fields.append({'name': name, 'offset': image.u16(entry + 0x0A), 'flags': image.u16(entry + 0x08),
                           'type': image.u64(entry + 0x10)})
    return fields


def read_registry(image):
    head = find_head(image)
    types = {}
    node = image.u64(head)
    while node:
        info = image.u64(node)
        flags = image.u16(info + 0x08)
        kind = KINDS[(flags >> 4) & 0x1F] if (flags >> 4) & 0x1F < len(KINDS) else f'code{(flags >> 4) & 0x1F}'
        entry = {
            'address': node, 'info': info, 'name': image.string(image.u64(info)) or '', 'kind': kind,
            'flags': flags, 'size': image.u16(info + 0x0A), 'alignment': image.u8(info + 0x18),
            'runtime_id': image.u16(node + 0x10),
        }
        field_count = image.u16(info + 0x1A)
        if kind == 'Class':
            entry['super'] = image.u64(node + 0x18) or None
            entry['class_id'] = image.u16(node + 0x28)
            entry['last_subclass_id'] = image.u16(node + 0x2A)
            entry['fields'] = read_fields(image, image.u64(info + 0x30), field_count, False)
        elif kind == 'ValueType':
            entry['fields'] = read_fields(image, image.u64(info + 0x28), field_count, False)
        elif kind == 'Enum':
            entry['values'] = read_fields(image, image.u64(info + 0x20), field_count, True)
        elif kind in ('Array', 'FixedArray'):
            entry['element'] = image.u64(info + 0x20)
        types[node] = entry
        node = image.u64(node + 8)
    return head, types


def type_name(types, address):
    entry = types.get(address)
    if entry is None:
        return f'?{address:#x}'
    if entry['kind'] in ('Array', 'FixedArray') and entry.get('element') in types:
        return f'Array<{type_name(types, entry["element"])}>'
    return entry['name'] or f'<{entry["kind"]}>'


def map_vtables(image, types):
    """getType() is virtual slot 0 of every typed object: `lea rax,[ClassInfo]; ret`.
    Returns {vtable address: class address} for each .rdata slot pointing at such a function."""
    text_begin, text_end = image.sections['.text']
    text = image.data[text_begin - BASE:text_end - BASE]
    get_type = {}
    for match in re.finditer(rb'\x48\x8d\x05(....)\xc3', text, re.S):
        at = text_begin + match.start()
        target = at + 7 + struct.unpack('<i', match.group(1))[0]
        if target in types:
            get_type[at] = target
    rdata_begin, rdata_end = image.sections['.rdata']
    vtables = {}
    for offset in range(rdata_begin - BASE, rdata_end - BASE - 8, 8):
        slot = struct.unpack_from('<Q', image.data, offset)[0]
        if slot in get_type:
            vtables[BASE + offset] = get_type[slot]
    return vtables


def class_layout(types, address):
    """Fields of a class including inherited ones, ordered by offset."""
    chain = []
    while address in types:
        chain.append(types[address])
        address = types[address].get('super')
        if address in (c['address'] for c in chain):
            break
    rows = []
    for entry in reversed(chain):
        for field in entry.get('fields', []):
            rows.append((field['offset'], field['name'], type_name(types, field['type']), entry['name']))
    return [c['name'] for c in chain], sorted(rows)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('dump')
    parser.add_argument('--out', help='JSON path (default: <dump folder>/typeinfo-<timestamp>.json)')
    parser.add_argument('--class', dest='class_name', help='print one type with inherited fields')
    parser.add_argument('--find', help='list types whose name contains this text (case-insensitive)')
    parser.add_argument('--vtable', type=lambda s: int(s, 0), help='class of an object with this vtable')
    args = parser.parse_args()

    image = Image(args.dump)
    head, types = read_registry(image)
    by_name = {t['name']: t for t in types.values() if t['name']}

    if args.find:
        for t in sorted(by_name.values(), key=lambda t: t['name']):
            if args.find.lower() in t['name'].lower():
                print(f'{t["name"]:60} {t["kind"]:10} size {t["size"]:#x}')
        return
    if args.class_name:
        t = by_name.get(args.class_name)
        if t is None:
            raise SystemExit(f'{args.class_name}: not a reflected type')
        print(f'{t["name"]} {t["kind"]} size {t["size"]:#x} align {t["alignment"]} at {t["address"]:#x}')
        if t['kind'] == 'Enum':
            for v in t['values']:
                print(f'  {v["value"]:6} {v["name"]}')
            return
        if t['kind'] == 'Class':
            chain, rows = class_layout(types, t['address'])
            print('  ' + ' : '.join(chain))
        else:
            rows = sorted((f['offset'], f['name'], type_name(types, f['type']), t['name']) for f in t['fields'])
        for offset, name, field_type, owner in rows:
            print(f'  +{offset:#06x} {name:40} {field_type:40} ({owner})')
        return
    vtables = map_vtables(image, types)
    if args.vtable is not None:
        print(type_name(types, vtables[args.vtable]) if args.vtable in vtables else 'unknown vtable')
        return

    out = pathlib.Path(args.out or pathlib.Path(args.dump).parent / f'typeinfo-{image.timestamp:08x}.json')
    for t in types.values():
        for f in t.get('fields', []):
            f['type_name'] = type_name(types, f['type'])
        if t.get('super'):
            t['super_name'] = type_name(types, t['super'])
        if t.get('element'):
            t['element_name'] = type_name(types, t['element'])
    document = {
        'pe_timestamp': image.timestamp, 'list_head': head,
        'types': sorted(types.values(), key=lambda t: (t['name'], t['address'])),
        'vtables': {f'{v:#x}': type_name(types, c) for v, c in sorted(vtables.items())},
    }
    out.write_text(json.dumps(document, indent=1))
    kinds = {}
    for t in types.values():
        kinds[t['kind']] = kinds.get(t['kind'], 0) + 1
    print(f'list head {head:#x}: {len(types)} types {dict(sorted(kinds.items()))}, {len(vtables)} vtables -> {out}')


if __name__ == '__main__':
    main()
