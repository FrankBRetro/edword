#!/usr/bin/env python3
# Copyright (c) 2026 Francis C Bullen. SPDX-License-Identifier: MIT (see LICENSE)
"""mkimg.py - draw the sample picture (a lighthouse)

    mkimg.py LIGHT.IMG     a 1-bit GEM IMG file
    mkimg.py LIGHT.PNG     in colour, as a PNG file
"""
import sys
from PIL import Image, ImageDraw

W, H = 240, 180
SKY, SUN, BEAM, ROCK, TOWER, STRIPE, LAMP, ROOF = \
    (150, 200, 240), (255, 220, 60), (255, 245, 170), (110, 90, 70), (250, 250, 250), (200, 30, 30), (255, 240, 120), (40, 40, 40)
SEA = [(30, 110, 170), (25, 95, 150), (20, 80, 130), (15, 65, 110), (10, 50, 90), (5, 40, 75)]


def draw(colour):
    im = Image.new('RGB', (W, H), (255, 255, 255))
    d = ImageDraw.Draw(im)
    g = (lambda c, l: c) if colour else (lambda c, l: (l, l, l))
    d.rectangle([0, 0, W - 1, 120], fill=g(SKY, 225))                       # sky
    for i in range(6):                                                      # sea
        y = 120 + i * 10
        d.rectangle([0, y, W, y + 10], fill=g(SEA[i], 150 - i * 15))
        for x in range(0, W, 24):
            d.arc([x, y - 4, x + 24, y + 6], 200, 340, fill=(255, 255, 255), width=2)
    d.polygon([(30, 125), (210, 125), (190, 105), (60, 100)], fill=g(ROCK, 90))  # rocks
    d.polygon([(105, 104), (135, 104), (128, 40), (112, 40)], fill=g(TOWER, 255), outline=(0, 0, 0))
    for y in (55, 75, 92):                                                  # stripes
        d.rectangle([108, y, 132, y + 8], fill=g(STRIPE, 20))
    d.rectangle([108, 26, 132, 40], fill=g(LAMP, 255), outline=(0, 0, 0))   # lamp room
    d.polygon([(104, 26), (136, 26), (120, 12)], fill=g(ROOF, 0))
    d.polygon([(132, 30), (238, 10), (238, 50)], fill=g(BEAM, 245))         # beam
    d.ellipse([20, 15, 50, 45], fill=g(SUN, 255), outline=(0, 0, 0))        # sun
    return im


out = sys.argv[1]
if out.lower().endswith('.png'):
    draw(True).save(out)
    sys.exit(0)

bw = draw(False).convert('L').convert('1')                                  # Floyd-Steinberg
rb = (W + 7) // 8
data = bytearray()
for v in (1, 8, 1, 2, 278, 278, W, H):
    data += v.to_bytes(2, 'big')
px = bw.load()
for y in range(H):
    row = bytearray(rb)
    for x in range(W):
        if px[x, y] == 0:
            row[x >> 3] |= 0x80 >> (x & 7)
    data += bytes([0x80, rb]) + row
open(out, 'wb').write(data)
