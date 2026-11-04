#!/usr/bin/env python3
# Copyright (c) 2026 Francis C Bullen. SPDX-License-Identifier: MIT (see LICENSE)
"""mkimg.py - draw the sample picture (a lighthouse) as a 1-bit GEM IMG file"""
import sys
from PIL import Image, ImageDraw

W, H = 240, 180
im = Image.new('L', (W, H), 255)
d = ImageDraw.Draw(im)
d.rectangle([0, 0, W - 1, 120], fill=225)                       # sky
for i in range(6):                                              # sea
    y = 120 + i * 10
    d.rectangle([0, y, W, y + 10], fill=150 - i * 15)
    for x in range(0, W, 24):
        d.arc([x, y - 4, x + 24, y + 6], 200, 340, fill=255, width=2)
d.polygon([(30, 125), (210, 125), (190, 105), (60, 100)], fill=90)  # rocks
d.polygon([(105, 104), (135, 104), (128, 40), (112, 40)], fill=255, outline=0)
for y in (55, 75, 92):                                          # stripes
    d.rectangle([108, y, 132, y + 8], fill=20)
d.rectangle([108, 26, 132, 40], fill=255, outline=0)            # lamp room
d.polygon([(104, 26), (136, 26), (120, 12)], fill=0)
d.polygon([(132, 30), (238, 10), (238, 50)], fill=245)          # beam
d.ellipse([20, 15, 50, 45], fill=255, outline=0)                # sun
bw = im.convert('1')                                            # Floyd-Steinberg

rb = (W + 7) // 8
out = bytearray()
for v in (1, 8, 1, 2, 278, 278, W, H):
    out += v.to_bytes(2, 'big')
px = bw.load()
for y in range(H):
    row = bytearray(rb)
    for x in range(W):
        if px[x, y] == 0:
            row[x >> 3] |= 0x80 >> (x & 7)
    out += bytes([0x80, rb]) + row
open(sys.argv[1], 'wb').write(out)
