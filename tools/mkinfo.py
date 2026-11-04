#!/usr/bin/env python3
# Copyright (c) 2026 Francis C Bullen. SPDX-License-Identifier: MIT (see LICENSE)
"""mkinfo.py OUTDIR - EdWord's Amiga icons, in the format every Workbench
reads (1.2 to 3.x): two bitplanes, the selected look by complementing.

    EdWord.info      the program (Workbench 2.04 and newer)
    EdWord13.info    the program for Workbench 1.2/1.3
    EdWordDoc.info   a document icon that opens with EdWord (copy it next
                     to a document, renamed to the document's name + .info)

Colours are the Workbench 2 pens: . grey, k black, w white, b blue.
"""
import struct
import sys

ICON = [
    "................................................",
    "....kkkkkkkkkkkkkkkkkkkkkkkkkk..................",
    "....kwwwwwwwwwwwwwwwwwwwwwwwkwk.................",
    "....kwwwwwwwwwwwwwwwwwwwwwwwkwwk................",
    "....kwwkkkkkkkkkkkkwwwwwwwwwkkkkk...............",
    "....kwwwwwwwwwwwwwwwwwwwwwwwwwwwk.........bb....",
    "....kwwkkkkkkwkkkkkkkkwkkkwwwwwwk........bbbb...",
    "....kwwwwwwwwwwwwwwwwwwwwwwwwwwwk.......bbbbk...",
    "....kwwkkkkwkkkkkkkwkkkkkkkkwwwwk......bbbbk....",
    "....kwwwwwwwwwwwwwwwwwwwwwwwwwwwk.....bbbbk.....",
    "....kwwkkkkkkkkwkkkkkkwkkkkkwwwwk....bbbbk......",
    "....kwwwwwwwwwwwwwwwwwwwwwwwwwwwk...bbbbk.......",
    "....kwwkkkkkwkkkkkkkkkkwwwwwwwwwk..bbbbk........",
    "....kwwwwwwwwwwwwwwwwwwwwwwwwwwwk.bbbbk.........",
    "....kwwkkkkkkkkkwkkkkkkkkkwwwwwwkbbbbk..........",
    "....kwwwwwwwwwwwwwwwwwwwwwwwwwwkwwbk............",
    "....kwwkkkkkkwkkkkkkkkkwwwwwwwwkwwk.............",
    "....kwwwwwwwwwwwwwwwwwwwwwwwwwwkkk..............",
    "....kwwwwwwwwwwwwwwwwwwwwwwwwwwwk...............",
    "....kkkkkkkkkkkkkkkkkkkkkkkkkkkkk...............",
]

DOCICON = [
    "................................",
    "...kkkkkkkkkkkkkkkkkkkkk........",
    "...kwwwwwwwwwwwwwwwwwwwkwk......",
    "...kwwwwwwwwwwwwwwwwwwwkwwk.....",
    "...kwwbbbbbbbbbbwwwwwwwkkkkk....",
    "...kwwwwwwwwwwwwwwwwwwwwwwwk....",
    "...kwwkkkkkwkkkkkkkwkkkkwwwk....",
    "...kwwwwwwwwwwwwwwwwwwwwwwwk....",
    "...kwwkkkkkkkkwkkkkkkkwkkwwk....",
    "...kwwwwwwwwwwwwwwwwwwwwwwwk....",
    "...kwwkkkkwkkkkkkkkwkkkkkwwk....",
    "...kwwwwwwwwwwwwwwwwwwwwwwwk....",
    "...kwwkkkkkkkwkkkkkkwwwwwwwk....",
    "...kwwwwwwwwwwwwwwwwwwwwwwwk....",
    "...kwwkkkkkkkkkwkkkkkkkkwwwk....",
    "...kwwwwwwwwwwwwwwwwwwwwwwwk....",
    "...kwwwwwwwwwwwwwwwwwwwwwwwk....",
    "...kkkkkkkkkkkkkkkkkkkkkkkkk....",
]

PEN = {'.': 0, 'k': 1, 'w': 2, 'b': 3}
WBTOOL, WBPROJECT = 3, 4
NO_POS = -0x80000000


def planes(rows):
    w, h = len(rows[0]), len(rows)
    wb = (w + 15) // 16 * 2
    out = b''
    for p in range(2):
        for r in rows:
            line = bytearray(wb)
            for x, c in enumerate(r):
                if PEN[c] & (1 << p):
                    line[x >> 3] |= 0x80 >> (x & 7)
            out += bytes(line)
    return w, h, out


def string(s):
    b = s.encode('latin-1') + b'\0'
    return struct.pack('>L', len(b)) + b


def diskobject(rows, kind, tool=None, tooltypes=(), stack=0):
    w, h, data = planes(rows)
    gadget = struct.pack('>LhhhhHHHLLLLLHL', 0, 0, 0, w, h + 1,
                         0x0004,          # GFLG_GADGIMAGE, selected by complement
                         0x0003,          # RELVERIFY | GADGIMMEDIATE
                         0x0001,          # BOOLGADGET
                         1, 0, 0, 0, 0, 0,
                         0)               # revision 0: what 1.x writes
    do = struct.pack('>HH', 0xE310, 1) + gadget
    do += struct.pack('>BBLLllLLl', kind, 0, 1 if tool else 0, 1 if tooltypes else 0,
                      NO_POS, NO_POS, 0, 0, stack)
    image = struct.pack('>hhhhhLBBL', 0, 0, w, h, 2, 1, 3, 0, 0) + data
    out = do + image
    if tool:
        out += string(tool)
    if tooltypes:
        out += struct.pack('>L', (len(tooltypes) + 1) * 4)
        for t in tooltypes:
            out += string(t)
    return out


def main(d):
    open(d + '/EdWord.info', 'wb').write(diskobject(ICON, WBTOOL, stack=20000))
    open(d + '/EdWord13.info', 'wb').write(diskobject(ICON, WBTOOL, stack=20000))
    open(d + '/EdWordDoc.info', 'wb').write(diskobject(DOCICON, WBPROJECT, tool='EdWord'))


if __name__ == '__main__':
    for r in ICON:
        assert len(r) == len(ICON[0])
    for r in DOCICON:
        assert len(r) == len(DOCICON[0])
    main(sys.argv[1])
