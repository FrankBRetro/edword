#!/usr/bin/env python3
# Copyright (c) 2026 Francis C Bullen. SPDX-License-Identifier: MIT (see LICENSE)
"""shot2png.py - turn EdWord's SHOTS\\*.SCR screen dumps into PNG files"""
import sys, os, struct
from PIL import Image

def convert(src, dst):
    b = open(src, 'rb').read()
    magic, w, h, planes = struct.unpack('>4h', b[:8])
    data = b[8:]
    if planes == 1:
        im = Image.frombytes('1', (w, h), data)
        im = Image.eval(im.convert('L'), lambda v: 255 - v)     # 1 bits are black
        im.save(dst)
        return
    # interleaved bit planes (ST medium / low resolution); default palette
    pal = [(255, 255, 255), (255, 0, 0), (0, 255, 0), (0, 0, 0)] if planes == 2 else \
          [(255, 255, 255), (255, 0, 0), (0, 255, 0), (255, 255, 0), (0, 0, 255), (255, 0, 255), (0, 255, 255),
           (170, 170, 170), (85, 85, 85), (255, 85, 85), (85, 255, 85), (255, 255, 85), (85, 85, 255),
           (255, 85, 255), (85, 255, 255), (0, 0, 0)]
    im = Image.new('RGB', (w, h))
    px = im.load()
    words = w // 16
    for y in range(h):
        row = data[y * words * planes * 2:(y + 1) * words * planes * 2]
        for wd in range(words):
            pw = [(row[(wd * planes + p) * 2] << 8) | row[(wd * planes + p) * 2 + 1] for p in range(planes)]
            for b in range(16):
                c = 0
                for p in range(planes):
                    if pw[p] & (0x8000 >> b): c |= 1 << p
                px[wd * 16 + b, y] = pal[c]
    im.save(dst)

if __name__ == '__main__':
    src, out = sys.argv[1], sys.argv[2]
    os.makedirs(out, exist_ok=True)
    for f in sorted(os.listdir(src)):
        if f.upper().endswith('.SCR'):
            convert(os.path.join(src, f), os.path.join(out, f[:-4].lower() + '.png'))
