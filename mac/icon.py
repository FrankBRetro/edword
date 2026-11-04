#!/usr/bin/env python3
# Copyright (c) 2026 Francis C Bullen. SPDX-License-Identifier: MIT (see LICENSE)
"""icon.py - draw EdWord's application icon as an .iconset (plain Python, no
libraries): a sheet of paper with lines of text and a pen nib on a blue
rounded square. usage: icon.py OUT.iconset"""
import os, sys, struct, zlib

def png(path, w, h, px):
    raw = b''.join(b'\0' + bytes(px[y * w * 4:(y + 1) * w * 4]) for y in range(h))
    def chunk(t, d):
        return struct.pack('>I', len(d)) + t + d + struct.pack('>I', zlib.crc32(t + d) & 0xffffffff)
    with open(path, 'wb') as f:
        f.write(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 6, 0, 0, 0)) +
                chunk(b'IDAT', zlib.compress(raw, 9)) + chunk(b'IEND', b''))

def draw(n):
    ss = 4                                        # supersampling
    N = n * ss
    def inside(x, y):
        u, v = x / N, y / N                       # 0..1, y down
        # background: rounded square
        r = 0.18
        cx = min(max(u, 0.06 + r), 0.94 - r); cy = min(max(v, 0.06 + r), 0.94 - r)
        if (u - cx) ** 2 + (v - cy) ** 2 > r * r or not (0.06 <= u <= 0.94 and 0.06 <= v <= 0.94):
            return None
        col = (43 + int(40 * v), 74 + int(50 * v), 140 + int(60 * (1 - v)))
        # paper with a folded corner
        if 0.24 <= u <= 0.72 and 0.17 <= v <= 0.83 and not (u - 0.62 > v - 0.17 and u > 0.62):
            col = (250, 250, 247)
            if u - 0.62 > v - 0.27 - 0.0 and u > 0.62 and v < 0.27:
                col = (210, 214, 220)
            for k in range(7):                    # lines of text
                ly = 0.32 + k * 0.07
                lw = 0.36 if k % 3 != 2 else 0.22
                if ly <= v <= ly + 0.025 and 0.30 <= u <= 0.30 + lw:
                    col = (90, 96, 108) if k else (40, 60, 120)
        # a pen across the corner
        d = (u - 0.44) * 0.707 + (v - 0.80) * -0.707
        t = (u - 0.44) * 0.707 + (v - 0.80) * 0.707
        if abs(t) < 0.045 and 0.0 < d < 0.40:
            col = (220, 170, 60) if d > 0.08 else (60, 60, 64)
        return col
    px = []
    for y in range(n):
        for x in range(n):
            acc = [0, 0, 0, 0]
            for sy in range(ss):
                for sx in range(ss):
                    c = inside(x * ss + sx + 0.5, y * ss + sy + 0.5)
                    if c:
                        acc[0] += c[0]; acc[1] += c[1]; acc[2] += c[2]; acc[3] += 255
            k = ss * ss
            a = acc[3] // k
            px += [acc[0] // max(1, acc[3] // 255), acc[1] // max(1, acc[3] // 255), acc[2] // max(1, acc[3] // 255), a] if a else [0, 0, 0, 0]
    return px

out = sys.argv[1]
os.makedirs(out, exist_ok=True)
for s in (16, 32, 128, 256, 512):
    for scale in (1, 2):
        n = s * scale
        name = 'icon_%dx%d%s.png' % (s, s, '@2x' if scale == 2 else '')
        png(os.path.join(out, name), n, n, draw(n))
