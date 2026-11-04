#!/usr/bin/env python3
# Copyright (c) 2026 Francis C Bullen. SPDX-License-Identifier: MIT (see LICENSE)
"""mkhun.py - a word list for mkdic from a hunspell dictionary

    mkhun.py index.aff index.dic [large_xx.msgpack.gz [top]] > words.txt

Expands the dictionary's stems with their prefixes and suffixes (two
levels, cross products, circumfixes) and prints the forms, one a line, in
UTF-8. With a wordfreq frequency list (large_xx.msgpack.gz from
github.com/rspeer/wordfreq), only the forms people actually write are
kept, besides every stem: that keeps the dictionary small enough for an
ST while it still knows the common inflections. Compound words are left
to the spelling checker, which joins two known words (German).
"""
import gzip
import re
import struct
import sys


def read_msgpack(data):
    """the small part of msgpack wordfreq's files use"""
    pos = 0

    def get():
        nonlocal pos
        b = data[pos]
        pos += 1
        if b <= 0x7F:
            return b
        if 0x80 <= b <= 0x8F:
            return {get(): get() for _ in range(b & 15)}
        if 0x90 <= b <= 0x9F:
            return [get() for _ in range(b & 15)]
        if 0xA0 <= b <= 0xBF:
            n = b & 31
            s = data[pos:pos + n].decode('utf-8', 'replace')
            pos += n
            return s
        if b == 0xC0:
            return None
        if b in (0xC2, 0xC3):
            return b == 0xC3
        if b in (0xD9, 0xDA, 0xDB):
            k = {0xD9: 1, 0xDA: 2, 0xDB: 4}[b]
            n = int.from_bytes(data[pos:pos + k], 'big')
            pos += k
            s = data[pos:pos + n].decode('utf-8', 'replace')
            pos += n
            return s
        if b in (0xDC, 0xDD):
            k = 2 if b == 0xDC else 4
            n = int.from_bytes(data[pos:pos + k], 'big')
            pos += k
            return [get() for _ in range(n)]
        if b in (0xDE, 0xDF):
            k = 2 if b == 0xDE else 4
            n = int.from_bytes(data[pos:pos + k], 'big')
            pos += k
            return {get(): get() for _ in range(n)}
        if b in (0xCC, 0xCD, 0xCE, 0xCF):
            k = {0xCC: 1, 0xCD: 2, 0xCE: 4, 0xCF: 8}[b]
            v = int.from_bytes(data[pos:pos + k], 'big')
            pos += k
            return v
        if b in (0xD0, 0xD1, 0xD2, 0xD3):
            k = {0xD0: 1, 0xD1: 2, 0xD2: 4, 0xD3: 8}[b]
            v = int.from_bytes(data[pos:pos + k], 'big', signed=True)
            pos += k
            return v
        if b == 0xCA:
            v = struct.unpack('>f', data[pos:pos + 4])[0]
            pos += 4
            return v
        if b == 0xCB:
            v = struct.unpack('>d', data[pos:pos + 8])[0]
            pos += 8
            return v
        if b >= 0xE0:
            return b - 256
        raise ValueError('msgpack type %02x' % b)
    return get()


def frequent_words(path, top=None):
    """wordfreq's cBpack: [header, bucket 0, bucket 1...] of lower-case
    words, most frequent first; top: only that many"""
    packed = read_msgpack(gzip.open(path).read())
    words = set()
    for bucket in packed[1:]:
        for w in bucket:
            if top is not None and len(words) >= top:
                return words
            words.add(w)
    return words


class Aff:
    def __init__(self, path):
        self.flagmode = 'char'
        self.pfx = {}
        self.sfx = {}
        self.needaffix = self.onlyincompound = self.forbidden = self.circumfix = None
        lines = open(path, encoding='utf-8', errors='replace').read().splitlines()
        i = 0
        while i < len(lines):
            f = lines[i].split()
            i += 1
            if not f or f[0].startswith('#'):
                continue
            key = f[0]
            if key == 'FLAG' and len(f) > 1:
                self.flagmode = f[1]
            elif key == 'NEEDAFFIX' and len(f) > 1:
                self.needaffix = f[1]
            elif key == 'ONLYINCOMPOUND' and len(f) > 1:
                self.onlyincompound = f[1]
            elif key == 'FORBIDDENWORD' and len(f) > 1:
                self.forbidden = f[1]
            elif key == 'CIRCUMFIX' and len(f) > 1:
                self.circumfix = f[1]
            elif key in ('PFX', 'SFX') and len(f) >= 4 and f[2] in ('Y', 'N'):
                flag, cross, n = f[1], f[2] == 'Y', int(f[3])
                rules = []
                for _ in range(n):
                    r = lines[i].split()
                    i += 1
                    if len(r) < 4:
                        continue
                    strip = '' if r[2] == '0' else r[2]
                    add = r[3]
                    cont = []
                    if '/' in add:
                        add, fl = add.split('/', 1)
                        cont = self.flags(fl)
                    if add == '0':
                        add = ''
                    cond = r[4] if len(r) > 4 else '.'
                    if key == 'PFX':
                        rx = re.compile('^' + cond) if cond != '.' else None
                    else:
                        rx = re.compile(cond + '$') if cond != '.' else None
                    rules.append((strip, add, cont, rx))
                (self.pfx if key == 'PFX' else self.sfx)[flag] = (cross, rules)

    def flags(self, s):
        if self.flagmode == 'long':
            return [s[k:k + 2] for k in range(0, len(s) - 1, 2)]
        if self.flagmode == 'num':
            return [x for x in s.split(',') if x]
        return list(s)


def expand(aff, word, flags):
    """the word's forms (a set)"""
    out = set()
    fl = set(flags)
    if aff.forbidden in fl or aff.onlyincompound in fl:
        return out
    if aff.needaffix not in fl:
        out.add(word)
    cf = aff.circumfix

    def sfx_forms(w, flagset, depth):
        res = []
        for f in flagset:
            if f not in aff.sfx:
                continue
            cross, rules = aff.sfx[f]
            for strip, add, cont, rx in rules:
                if strip and not w.endswith(strip):
                    continue
                if rx and not rx.search(w):
                    continue
                nw = w[:len(w) - len(strip)] + add
                if not nw:
                    continue
                res.append((nw, cross, cont))
                if depth < 2:
                    for nw2, cross2, cont2 in sfx_forms(nw, cont, depth + 1):
                        res.append((nw2, cross and cross2, cont + cont2))
        return res

    def pfx_forms(w, flagset):
        res = []
        for f in flagset:
            if f not in aff.pfx:
                continue
            cross, rules = aff.pfx[f]
            for strip, add, cont, rx in rules:
                if strip and not w.startswith(strip):
                    continue
                if rx and not rx.search(w):
                    continue
                res.append((add + w[len(strip):], cross, cont))
        return res

    suff = sfx_forms(word, fl, 1)
    for w, cross, cont in suff:
        if aff.needaffix in cont and len(cont) == 1:
            pass
        elif cf in cont:
            pass                            # needs its prefix as well
        else:
            out.add(w)
        if cross:
            for pw, pcross, pcont in pfx_forms(w, fl | set(cont)):
                if pcross:
                    out.add(pw)
    for pw, pcross, pcont in pfx_forms(word, fl):
        if cf not in pcont and aff.needaffix not in pcont:
            out.add(pw)
        for sw, scross, scont in sfx_forms(pw, set(pcont), 1):
            out.add(sw)
    return out


def main():
    aff = Aff(sys.argv[1])
    freq = frequent_words(sys.argv[3], int(sys.argv[4]) if len(sys.argv) > 4 else None) if len(sys.argv) > 3 else None
    words = set()
    with open(sys.argv[2], encoding='utf-8', errors='replace') as f:
        f.readline()
        for line in f:
            line = line.strip()
            if not line or line.startswith('#'):
                continue
            line = line.split('\t')[0].split(' ')[0]
            if '/' in line:
                stem, fl = line.split('/', 1)
                fl = aff.flags(fl)
            else:
                stem, fl = line, []
            stem = stem.replace('\\/', '/')
            forms = expand(aff, stem, fl)
            for w in forms:
                if w == stem or freq is None or w.lower() in freq:
                    words.add(w)
    for w in sorted(words):
        if 1 < len(w) < 32 and not any(c.isdigit() for c in w):
            print(w)


if __name__ == '__main__':
    main()
