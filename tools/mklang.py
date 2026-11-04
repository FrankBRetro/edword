#!/usr/bin/env python3
# Copyright (c) 2026 Francis C Bullen. SPDX-License-Identifier: MIT (see LICENSE)
"""mklang.py - turn lang/strings.txt into lang.h and lang.c

strings.txt holds blocks like

    @S_NEW
    en: New
    de: Neu
    es: Nuevo
    it: Nuovo
    fr: Nouveau

in UTF-8. Text is converted to the Atari ST character set; a missing
translation falls back to English at run time. "\\n" in a string is a line
break. Checks:
  - every id has English;
  - alert texts (ids starting AL_) have at most 5 lines of 32 characters,
    and alert buttons (ids starting AB_) at most 3 buttons of 10 characters;
  - menu titles (MT_) fit the menu bar together in every language.
"""
import sys, re
# usage: mklang.py strings.txt lang.h lang.c [--utf8 | --latin1 | --cp1252]

LANGS = ['en', 'de', 'es', 'it', 'fr']

# Unicode -> Atari ST character set (0x80-0xFF, the characters we need)
ST = {
    'Ç': 0x80, 'ü': 0x81, 'é': 0x82, 'â': 0x83, 'ä': 0x84, 'à': 0x85, 'å': 0x86, 'ç': 0x87,
    'ê': 0x88, 'ë': 0x89, 'è': 0x8A, 'ï': 0x8B, 'î': 0x8C, 'ì': 0x8D, 'Ä': 0x8E, 'Å': 0x8F,
    'É': 0x90, 'æ': 0x91, 'Æ': 0x92, 'ô': 0x93, 'ö': 0x94, 'ò': 0x95, 'û': 0x96, 'ù': 0x97,
    'ÿ': 0x98, 'Ö': 0x99, 'Ü': 0x9A, '¢': 0x9B, '£': 0x9C, '¥': 0x9D, 'ß': 0x9E,
    'á': 0xA0, 'í': 0xA1, 'ó': 0xA2, 'ú': 0xA3, 'ñ': 0xA4, 'Ñ': 0xA5, 'ª': 0xA6, 'º': 0xA7,
    '¿': 0xA8, '¬': 0xAA, '½': 0xAB, '¼': 0xAC, '¡': 0xAD, '«': 0xAE, '»': 0xAF,
    'ã': 0xB0, 'õ': 0xB1, 'Ø': 0xB2, 'ø': 0xB3, 'œ': 0xB4, 'Œ': 0xB5, 'À': 0xB6, 'Ã': 0xB7,
    'Õ': 0xB8, '©': 0xBD, '®': 0xBE, '§': 0xDD, '°': 0xF8, '·': 0xFA, '…': None,
}
# capitals the ST character set lacks
PLAIN = {'È': 'E', 'Ê': 'E', 'Ë': 'E', 'Á': 'A', 'Â': 'A', 'Í': 'I', 'Î': 'I', 'Ï': 'I',
         'Ó': 'O', 'Ô': 'O', 'Ú': 'U', 'Û': 'U', 'Ù': 'U', 'Ì': 'I', 'Ò': 'O', '’': "'",
         '‘': "'", '“': '"', '”': '"', '–': '-', '—': '-', '…': '...', '½': '1/2'}

def to_st(s, where):
    out = []
    for ch in s:
        if ord(ch) < 128:
            out.append(ord(ch))
        elif ch in PLAIN and (ch not in ST or ST[ch] is None):
            out.extend(ord(c) for c in PLAIN[ch])
        elif ch in ST and ST[ch] is not None:
            out.append(ST[ch])
        else:
            sys.exit('%s: no ST character for %r' % (where, ch))
    return bytes(out)

# characters Latin-1 lacks, for the Amiga version (--latin1)
LAT = {'œ': 'oe', 'Œ': 'OE', '’': "'", '‘': "'", '“': '"', '”': '"', '–': '-', '—': '-', '…': '...'}

def to_latin1(s, where):
    out = bytearray()
    for ch in s:
        if ord(ch) < 256:
            out.append(ord(ch))
        elif ch in LAT:
            out.extend(LAT[ch].encode('ascii'))
        else:
            sys.exit('%s: no Latin-1 character for %r' % (where, ch))
    return bytes(out)

def to_cp1252(s, where):
    try:
        return s.encode('cp1252')
    except UnicodeEncodeError:
        sys.exit('%s: no Windows-1252 character in %r' % (where, s))

def c_string(b):
    r = '"'
    for x in b:
        if x == 0x22: r += '\\"'
        elif x == 0x5C: r += '\\\\'
        elif x == 0x0A: r += '\\n'
        elif 32 <= x < 127: r += chr(x)
        else: r += '\\%03o' % x
    return r + '"'

def main():
    utf8 = '--utf8' in sys.argv          # for the macOS version: strings in UTF-8
    latin1 = '--latin1' in sys.argv      # for the Amiga version: ISO-8859-1
    cp1252 = '--cp1252' in sys.argv      # for the Windows version: Windows-1252
    args = [a for a in sys.argv[1:] if a not in ('--utf8', '--latin1', '--cp1252')]
    src, hout, cout = args[:3]
    enc = (lambda s, where: s.encode('utf-8')) if utf8 else to_latin1 if latin1 else to_cp1252 if cp1252 else to_st
    ids, text, cur = [], {}, None
    for n, line in enumerate(open(src, encoding='utf-8'), 1):
        line = line.rstrip('\n')
        if not line.strip() or line.lstrip().startswith('#'):
            continue
        if line.startswith('@'):
            cur = line[1:].strip()
            if cur in text:
                sys.exit('line %d: %s defined twice' % (n, cur))
            ids.append(cur)
            text[cur] = {}
            continue
        m = re.match(r'(\w\w): ?(.*)$', line)
        if not m or cur is None or m.group(1) not in LANGS:
            sys.exit('line %d: bad line %r' % (n, line))
        text[cur][m.group(1)] = m.group(2).replace('\\n', '\n')
    bad = 0
    for i in ids:
        if 'en' not in text[i]:
            print('missing English:', i); bad += 1
        for lg, s in text[i].items():
            st = to_st(s, '%s/%s' % (i, lg))
            if i.startswith('AL_'):
                lines = st.split(b'|')
                if len(lines) > 5 or max(len(x) for x in lines) > 32:
                    print('alert too big: %s/%s: %r' % (i, lg, s)); bad += 1
            if i.startswith('AB_'):
                btn = st.split(b'|')
                if len(btn) > 3 or max(len(x) for x in btn) > 10:
                    print('alert buttons too big: %s/%s: %r' % (i, lg, s)); bad += 1
    for lg in LANGS:
        w = sum(len(to_st(text[i].get(lg, text[i]['en']), i)) + 2 for i in ids if i.startswith('MT_'))
        if w > 78:
            print('menu bar too wide in %s: %d characters' % (lg, w)); bad += 1
    if bad:
        sys.exit('%d problems' % bad)

    with open(hout, 'w') as f:
        f.write('/* lang.h - generated by tools/mklang.py from lang/strings.txt; do not edit */\n')
        f.write('/* Copyright (c) 2026 Francis C Bullen. SPDX-License-Identifier: MIT (see LICENSE) */\n')
        f.write('#ifndef LANG_H\n#define LANG_H\n\nenum {\n')
        for i in ids:
            f.write('    %s,\n' % i)
        f.write('    S_COUNT\n};\n\n#define NLANG %d\n' % len(LANGS))
        f.write('enum { %s };\n' % ', '.join('LANG_' + l.upper() for l in LANGS))
        f.write('extern int ui_lang;\nextern const char *const lang_name[NLANG];\n')
        f.write('const char *L(int id);\n\n#endif\n')
    with open(cout, 'w') as f:
        f.write('/* lang.c - generated by tools/mklang.py from lang/strings.txt; do not edit */\n')
        f.write('/* Copyright (c) 2026 Francis C Bullen. SPDX-License-Identifier: MIT (see LICENSE) */\n')
        f.write('#include <stddef.h>\n#include "lang.h"\n\nint ui_lang;\n\n')
        names = {'en': 'English', 'de': 'Deutsch', 'es': 'Español', 'it': 'Italiano', 'fr': 'Français'}
        f.write('const char *const lang_name[NLANG] = { %s };\n\n' %
                ', '.join(c_string(enc(names[l], 'names')) for l in LANGS))
        for lg in LANGS:
            f.write('static const char *const s_%s[S_COUNT] = {\n' % lg)
            for i in ids:
                s = text[i].get(lg)
                f.write('    %s,\n' % (c_string(enc(s, i)) if s is not None else 'NULL'))
            f.write('};\n\n')
        f.write('static const char *const *const tab[NLANG] = { %s };\n\n' % ', '.join('s_' + l for l in LANGS))
        f.write('const char *L(int id)\n{\n    const char *s;\n'
                '    if (id < 0 || id >= S_COUNT) return "?";\n'
                '    s = tab[ui_lang >= 0 && ui_lang < NLANG ? ui_lang : 0][id];\n'
                '    return s ? s : s_en[id];\n}\n')
    missing = {lg: sum(1 for i in ids if lg not in text[i]) for lg in LANGS}
    print('%d strings; missing translations: %s' % (len(ids), missing))

if __name__ == '__main__':
    main()
