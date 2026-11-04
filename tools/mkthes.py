#!/usr/bin/env python3
# Copyright (c) 2026 Francis C Bullen. SPDX-License-Identifier: MIT (see LICENSE)
"""mkthes.py - build EdWord's thesaurus, EDWORD.THS, from Open English WordNet

usage: mkthes.py <oewn src/yaml folder> <word list> <out file>

The word list (one word per line, the British list the spelling dictionary
is made from) limits headwords and synonyms to words EdWord knows; an
American spelling is replaced by its British form where the list has one.

Open English WordNet: (c) Princeton University and the Open English
Wordnet team, CC BY 4.0 / WordNet licence. See dict/COPYING.THS.

File layout (big-endian):
  "EDWTHS3\\n" nwords:4 nblocks:4 index_len:4 wtab_len:4 data_len:4
  index:  per block of 32 words: offset:4 into wtab, first word\\0
          (small: EdWord keeps it in memory)
  wtab:   every word, sorted: word\\0 nsenses:1 offset:3 * nsenses (into data)
  woff:   per word number: offset:3 into wtab
  data:   per sense (shared by all its words): pos:1 ('n' 'v' 'a' 'r')
          nsyn:1 word number:2 * nsyn (the headword itself is skipped)
Characters are in the Atari ST character set (plain ASCII in practice).
"""
import sys, os, glob, re, struct
import yaml

try:
    Loader = yaml.CSafeLoader
except AttributeError:
    Loader = yaml.SafeLoader

MAXSENSE, MAXSYN, BLOCK = 8, 9, 32

def load(path):
    with open(path, encoding='utf-8') as f:
        return yaml.load(f, Loader=Loader) or {}

def main():
    src, wl, out = sys.argv[1:4]
    words = set()
    for w in open(wl, encoding='latin-1'):
        w = w.strip()
        if w and "'" not in w:
            words.add(w)
    lower = {w.lower() for w in words}

    def british(w):
        """the British form of a (lower case) word, or None if unknown"""
        if w in lower:
            return w
        for a, b in (('or', 'our'), ('er', 're'), ('ize', 'ise'), ('yze', 'yse'),
                     ('og', 'ogue'), ('ense', 'ence'), ('ization', 'isation')):
            if w.endswith(a) and (w[:-len(a)] + b) in lower:
                return w[:-len(a)] + b
        return None

    def ok_phrase(p):
        """a synonym: every word known (British spelling applied)"""
        parts = p.replace('-', ' - ').split()
        outp = []
        for x in parts:
            if x == '-':
                outp.append('-'); continue
            b = british(x.lower())
            if b is None:
                return None
            outp.append(x if x.lower() == b else b)
        s = ' '.join(outp).replace(' - ', '-')
        return s if len(s) < 31 else None

    synsets = {}
    for f in sorted(glob.glob(os.path.join(src, '*.yaml'))):
        base = os.path.basename(f)
        if base.startswith(('noun.', 'verb.', 'adj.', 'adv.')):
            synsets.update(load(f))
    print('synsets', len(synsets), file=sys.stderr)

    entries = {}
    for f in sorted(glob.glob(os.path.join(src, 'entries-*.yaml'))):
        for lemma, poses in load(f).items():
            lemma = str(lemma)
            if ' ' in lemma or not lemma.isalpha():
                continue
            b = british(lemma.lower())
            if b is None or lemma != lemma.lower():
                continue
            for pos, e in poses.items():
                for s in e.get('sense', []):
                    entries.setdefault(b, []).append(s['synset'])
    print('headwords', len(entries), file=sys.stderr)

    def members(sid):
        ss = synsets.get(sid)
        return [str(m) for m in ss.get('members', [])] if ss else []

    sense_rec = {}           # synset id -> (pos, synonyms incl. its own words)
    def sense(sid):
        if sid in sense_rec:
            return sense_rec[sid]
        ss = synsets.get(sid)
        if not ss:
            sense_rec[sid] = None
            return None
        pos = ss.get('partOfSpeech', 'n')
        pos = {'s': 'a'}.get(pos, pos)
        syn = []
        def add(lst):
            for m in lst:
                p = ok_phrase(m)
                if p and p not in syn:
                    syn.append(p)
        add(members(sid))
        own = len(syn)
        if own < 4:
            for rel in ss.get('similar', []) + ss.get('hypernym', []):
                add(members(rel))
                if len(syn) >= MAXSYN:
                    break
        r = (pos, syn[:MAXSYN]) if syn else None
        sense_rec[sid] = r
        return r

    recs = {}
    for head, sids in entries.items():
        lst, seen = [], set()
        for sid in sids:
            if sid in seen:
                continue
            seen.add(sid)
            r = sense(sid)
            if r and any(x.lower() != head for x in r[1]):
                lst.append(sid)
            if len(lst) >= MAXSENSE:
                break
        if lst:
            recs[head] = lst

    used = sorted({sid for l in recs.values() for sid in l})
    allw = set(recs)
    for sid in used:
        allw.update(sense_rec[sid][1])
    wl = sorted(allw, key=lambda w: w.encode('latin-1', 'replace'))
    if len(wl) >= 65536:
        sys.exit('too many words')
    wid = {w: i for i, w in enumerate(wl)}
    data = bytearray()
    off = {}
    for sid in used:
        pos, syn = sense_rec[sid]
        off[sid] = len(data)
        data += pos.encode() + bytes([len(syn)])
        for x in syn:
            data += struct.pack('>H', wid[x])
    wtab = bytearray()
    index = bytearray()
    woff = bytearray()
    for i, w in enumerate(wl):
        if i % BLOCK == 0:
            index += struct.pack('>I', len(wtab)) + w.encode('latin-1', 'replace') + b'\0'
        woff += struct.pack('>I', len(wtab))[1:]
        senses = recs.get(w, [])
        wtab += w.encode('latin-1', 'replace') + b'\0' + bytes([len(senses)])
        for sid in senses:
            wtab += struct.pack('>I', off[sid])[1:]
    nblk = (len(wl) + BLOCK - 1) // BLOCK
    with open(out, 'wb') as f:
        f.write(b'EDWTHS3\n' + struct.pack('>IIIII', len(wl), nblk, len(index), len(wtab), len(data)))
        f.write(index); f.write(wtab); f.write(woff); f.write(data)
    print('wrote', out, len(recs), 'headwords,', len(wl), 'words,', len(used), 'senses,',
          28 + len(index) + len(wtab) + len(woff) + len(data), 'bytes', file=sys.stderr)

if __name__ == '__main__':
    main()
