#!/usr/bin/env python3
# Copyright (c) 2026 Francis C Bullen. SPDX-License-Identifier: MIT (see LICENSE)
"""mkico.py - EdWord's Windows icon (win/res/edword.ico) from the drawing in
mac/icon.py: 16, 32 and 48 pixels, each in 256 colours (for Windows 95 to
2000) and in 32-bit colour with alpha (XP on). Needs Pillow for the
256-colour palette. usage: mkico.py OUT.ico"""
import os, sys, struct
from PIL import Image

src = open(os.path.join(os.path.dirname(__file__), '..', 'mac', 'icon.py')).read()
ns = {}
exec(src.split('\nout = sys.argv')[0], ns)
draw = ns['draw']

def bmp_entry(n, px, bpp):
    """an icon image: BITMAPINFOHEADER (twice the height), colours, AND mask"""
    rows = []
    mask_rb = ((n + 31) // 32) * 4
    if bpp == 32:
        for y in range(n - 1, -1, -1):
            r = bytearray()
            for x in range(n):
                R, G, B, A = px[(y * n + x) * 4:(y * n + x) * 4 + 4]
                r += bytes((B, G, R, A))
            rows.append(bytes(r))
        pal = b''
    else:
        im = Image.new('RGB', (n, n), (0, 0, 0))
        for y in range(n):
            for x in range(n):
                R, G, B, A = px[(y * n + x) * 4:(y * n + x) * 4 + 4]
                im.putpixel((x, y), (R, G, B) if A >= 128 else (0, 0, 0))
        q = im.quantize(256, dither=Image.Dither.NONE)
        p = q.getpalette()[:768] + [0] * (768 - len(q.getpalette()[:768]))
        pal = b''.join(bytes((p[i * 3 + 2], p[i * 3 + 1], p[i * 3], 0)) for i in range(256))
        rb = (n + 3) & ~3
        for y in range(n - 1, -1, -1):
            r = bytes(q.getpixel((x, y)) for x in range(n)) + b'\0' * (rb - n)
            rows.append(r)
    mask = []
    for y in range(n - 1, -1, -1):
        bits = bytearray(mask_rb)
        for x in range(n):
            if px[(y * n + x) * 4 + 3] < 128:
                bits[x >> 3] |= 0x80 >> (x & 7)
        mask.append(bytes(bits))
    xor = b''.join(rows)
    hdr = struct.pack('<IiiHHIIiiII', 40, n, 2 * n, 1, bpp, 0, len(xor) + len(mask) * mask_rb, 0, 0, 256 if bpp == 8 else 0, 0)
    return hdr + pal + xor + b''.join(mask)

imgs = []
for bpp in (8, 32):
    for n in (16, 32, 48):
        imgs.append((n, bpp, bmp_entry(n, draw(n), bpp)))
out = struct.pack('<HHH', 0, 1, len(imgs))
off = 6 + 16 * len(imgs)
dirs = b''
data = b''
for n, bpp, b in imgs:
    dirs += struct.pack('<BBBBHHII', n, n, 0, 0, 1, bpp, len(b), off + len(data))
    data += b
open(sys.argv[1], 'wb').write(out + dirs + data)
