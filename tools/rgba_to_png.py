"""Pictures from pipe `reticle grab` textures: srvN_WxH.rgba -> .png, _alpha.png (the art) and _rgb.png; prints alpha range."""
import sys, struct, zlib, os
def png(path, w, h, rows):
    raw = b''.join(b'\x00' + r for r in rows)
    def chunk(t, data): return struct.pack('>I', len(data)) + t + data + struct.pack('>I', zlib.crc32(t + data) & 0xffffffff)
    open(path, 'wb').write(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 6, 0, 0, 0)) + chunk(b'IDAT', zlib.compress(raw, 6)) + chunk(b'IEND', b''))
for f in sys.argv[1:]:
    name = os.path.basename(f)
    w, h = map(int, name.split('_')[1].split('.')[0].split('x'))
    data = open(f, 'rb').read()
    rows = [data[y*w*4:(y+1)*w*4] for y in range(h)]
    png(f[:-5] + '.png', w, h, rows)
    # alpha as grey and colour on black
    a = [bytes(v for px in range(w) for v in (r[px*4+3],)*3 + (255,)) for r in rows]
    png(f[:-5] + '_alpha.png', w, h, a)
    c = [bytes(v for px in range(w) for v in (r[px*4], r[px*4+1], r[px*4+2], 255)) for r in rows]
    png(f[:-5] + '_rgb.png', w, h, c)
    print(name, 'alpha min/max', min(data[3::4]), max(data[3::4]))
