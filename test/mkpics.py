#!/usr/bin/env python3
# Copyright (c) 2026 Francis C Bullen. SPDX-License-Identifier: MIT (see LICENSE)
"""mkpics.py DIR - test pictures for hosttest: PNG in several kinds, IFF
ILBM and true-colour GEM IMG, all of the same 40 x 30 picture:
pixel (x, y) = (x * 6, y * 8, (x + y) * 3) for colour, and greys"""
import struct
import sys
import zlib

W, H = 40, 30


def px(x, y):
    return (x * 6 % 256, y * 8 % 256, (x + y) * 3 % 256)


def chunk(t, d):
    return struct.pack('>I', len(d)) + t + d + struct.pack('>I', zlib.crc32(t + d) & 0xffffffff)


def png(path, ctype, depth, rows, plte=None, trns=None, interlace=0, w=W, h=H):
    out = b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, depth, ctype, 0, 0, interlace))
    if plte:
        out += chunk(b'PLTE', plte)
    if trns:
        out += chunk(b'tRNS', trns)
    out += chunk(b'IDAT', zlib.compress(rows, 9)) + chunk(b'IEND', b'')
    open(path, 'wb').write(out)


def rgb_row(y, x0=0, dx=1, w=W):
    return b''.join(bytes(px(x, y)) for x in range(x0, w, dx))


def main(d):
    # RGB 8-bit, with every filter type in turn (the bytes are raw: filter 0)
    rows = b''.join(b'\x00' + rgb_row(y) for y in range(H))
    png(d + '/rgb.png', 2, 8, rows)
    # Sub filter on every row
    rows = b''
    for y in range(H):
        r = rgb_row(y)
        f = bytes((r[i] - (r[i - 3] if i >= 3 else 0)) & 255 for i in range(len(r)))
        rows += b'\x01' + f
    png(d + '/sub.png', 2, 8, rows)
    # RGBA: left half transparent (white), right opaque
    rows = b''
    for y in range(H):
        r = b''
        for x in range(W):
            r += bytes(px(x, y)) + (b'\x00' if x < W // 2 else b'\xff')
        rows += b'\x00' + r
    png(d + '/rgba.png', 6, 8, rows)
    # 16-bit grey: value y * 8 in the high byte
    rows = b''.join(b'\x00' + b''.join(struct.pack('>H', (y * 8) * 257) for x in range(W)) for y in range(H))
    png(d + '/grey16.png', 0, 16, rows)
    # 4-bit palette: index x % 16 -> (16 * i, 0, 255 - 16 * i)
    plte = b''.join(bytes((16 * i, 0, 255 - 16 * i)) for i in range(16))
    rows = b''
    for y in range(H):
        r = bytearray()
        for x in range(0, W, 2):
            r.append(((x % 16) << 4) | ((x + 1) % 16))
        rows += b'\x00' + bytes(r)
    png(d + '/pal4.png', 3, 4, rows, plte)
    # Adam7 interlaced RGB
    rows = b''
    passes = [(0, 0, 8, 8), (4, 0, 8, 8), (0, 4, 4, 8), (2, 0, 4, 4), (0, 2, 2, 4), (1, 0, 2, 2), (0, 1, 1, 2)]
    for xs, ys, dx, dy in passes:
        if xs >= W or ys >= H:
            continue
        for y in range(ys, H, dy):
            rows += b'\x00' + rgb_row(y, xs, dx)
    png(d + '/inter.png', 2, 8, rows, interlace=1)

    # IFF ILBM, 24 planes, uncompressed
    rb = (W + 15) // 16 * 2
    body = b''
    for y in range(H):
        for ch in range(3):
            for bit in range(8):
                plane = bytearray(rb)
                for x in range(W):
                    if px(x, y)[ch] & (1 << bit):
                        plane[x >> 3] |= 0x80 >> (x & 7)
                body += bytes(plane)
    bmhd = struct.pack('>HHhhBBBBHBBhh', W, H, 0, 0, 24, 0, 0, 0, 0, 1, 1, W, H)
    form = b'ILBM' + b'BMHD' + struct.pack('>I', len(bmhd)) + bmhd + b'BODY' + struct.pack('>I', len(body)) + body
    open(d + '/rgb.iff', 'wb').write(b'FORM' + struct.pack('>I', len(form)) + form)

    # GEM IMG, 24 planes chunky, literal runs (0x80 n)
    bpb = ((W + 7) // 8 + 1) & ~1
    bpl = bpb * 24
    out = struct.pack('>8H', 1, 8, 24, 2, 85, 85, W, H)
    for y in range(H):
        line = bytearray(rgb_row(y)) + bytearray(b'\xff' * (bpl - W * 3))
        i = 0
        while i < bpl:
            n = min(255, bpl - i)
            out += bytes((0x80, n)) + bytes(line[i:i + n])
            i += n
    open(d + '/rgb24.img', 'wb').write(out)
    # GEM IMG, 16 planes chunky (RGB 5-6-5)
    out = struct.pack('>8H', 1, 8, 16, 2, 85, 85, W, H)
    for y in range(H):
        line = bytearray()
        for x in range((W + 7) // 8 * 8):
            r, g, b = px(x, y) if x < W else (255, 255, 255)
            line += struct.pack('>H', ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3))
        i = 0
        while i < len(line):
            n = min(255, len(line) - i)
            out += bytes((0x80, n)) + bytes(line[i:i + n])
            i += n
    open(d + '/rgb16.img', 'wb').write(out)

    st_iff(d)


def st_iff(d):
    """An Atari ST Deluxe Paint picture: 48 x 6, 4 planes, VDAT compression,
    a colour map in the ST's 3 bits, and a RAST palette from line 3."""
    w, h, np = 48, 6, 4
    rbw = (w + 15) // 16
    def val(x, y): return (x // 3) % 16 if y != 1 else 5
    planes = []
    for k in range(np):
        words = [[0] * rbw for _ in range(h)]
        for y in range(h):
            for x in range(w):
                if val(x, y) & (1 << k):
                    words[y][x // 16] |= 0x8000 >> (x & 15)
        col = [words[y][c] for c in range(rbw) for y in range(h)]      # down each column
        cmds, data, i = bytearray(), bytearray(), 0
        while i < len(col):
            r = 1
            while i + r < len(col) and col[i + r] == col[i] and r < 127: r += 1
            if r >= 3:
                if r % 2: cmds.append(r); data += struct.pack('>H', col[i])                 # > 1: repeat
                else: cmds.append(1); data += struct.pack('>HH', r, col[i])                 # 1: count, repeat
                i += r
            else:
                n = 1
                while i + n < len(col) and n < 3: n += 1
                if n == 2: cmds.append(256 - 2); data += struct.pack('>2H', *col[i:i + 2])   # < 0: literal
                else: cmds.append(0); data += struct.pack('>H', n) + struct.pack('>%dH' % n, *col[i:i + n])
                i += n
        v = struct.pack('>H', len(cmds) + 2) + bytes(cmds) + bytes(data)
        planes.append(b'VDAT' + struct.pack('>I', len(v)) + v + (b'\0' if len(v) & 1 else b''))
    body = b''.join(planes)
    cmap = b''.join(bytes(((i & 7) << 5, ((7 - (i & 7)) << 5), 0xE0 if i >= 8 else 0)) for i in range(16))
    st = [0x777] * 16
    st[5] = 0x700 | 0x8F0     # red 7, green 7 with the STE's extra bit (15)
    rast = struct.pack('>H16H', 3, *st)
    bmhd = struct.pack('>HHhhBBBBHBBhh', w, h, 0, 0, np, 0, 2, 0, 0, 10, 11, 320, 200)
    form = b'ILBM' + b'BMHD' + struct.pack('>I', len(bmhd)) + bmhd + b'CMAP' + struct.pack('>I', len(cmap)) + cmap + \
        b'RAST' + struct.pack('>I', len(rast)) + rast + b'BODY' + struct.pack('>I', len(body)) + body
    open(d + '/st.iff', 'wb').write(b'FORM' + struct.pack('>I', len(form)) + form)


if __name__ == '__main__':
    main(sys.argv[1])
