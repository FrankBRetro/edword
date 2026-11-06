#!/usr/bin/env python3
# Copyright (c) 2026 Francis C Bullen. SPDX-License-Identifier: MIT (see LICENSE)
"""mkrsc.py - EdWord's GEM resource files

usage: mkrsc.py lang/strings.txt OUTDIR

Writes OUTDIR/EDWORD.RSC (English), EDWORDDE.RSC, EDWORDES.RSC, EDWORDIT.RSC
and EDWORDFR.RSC - one resource file per language, each laid out for its own
words - and edrsc.h, the tree and object numbers they all share.

The trees: the menu bar, the Print dialog, the two pages of the Preferences,
the font selector, the toolbar (16 and 8 pixel high icons, for ST high and
ST medium resolution) and the toolbar's popup menus. They are ordinary GEM
objects, so they can be looked at and changed with a resource editor
(ResourceMaster, Interface, ORCS...) as long as the object numbers stay the
same.

Dialog rows are one character apart in the files; EdWord spreads them to
one and a half when it loads them (it marks the objects to move with the
extended object type: 1 an item in a row, 2 a box of radio buttons, 3 an
exit button), so they look the same in every resolution.
"""
import os, sys, struct

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import mklang

LANGS = mklang.LANGS
RSCNAME = {'en': 'EDWORD.RSC', 'de': 'EDWORDDE.RSC', 'es': 'EDWORDES.RSC', 'it': 'EDWORDIT.RSC', 'fr': 'EDWORDFR.RSC'}

# ---- AES constants -------------------------------------------------------------
G_BOX, G_TEXT, G_BOXTEXT, G_IMAGE, G_USERDEF, G_IBOX, G_BUTTON, G_BOXCHAR, G_STRING, \
    G_FTEXT, G_FBOXTEXT, G_ICON, G_TITLE = range(20, 33)
SELECTABLE, DEFAULT, EXIT, EDITABLE, RBUTTON, LASTOB, TOUCHEXIT, HIDETREE = 1, 2, 4, 8, 16, 32, 64, 128
DISABLED, OUTLINED = 8, 16
IBM = 3

# ---- strings -------------------------------------------------------------------
def load_strings(path):
    import re
    text, cur = {}, None
    for line in open(path, encoding='utf-8'):
        line = line.rstrip('\n')
        if not line.strip() or line.lstrip().startswith('#'):
            continue
        if line.startswith('@'):
            cur = line[1:].strip()
            text[cur] = {}
            continue
        m = re.match(r'(\w\w): ?(.*)$', line)
        if m:
            text[cur][m.group(1)] = m.group(2).replace('\\n', '\n')
    return text

class Lang:
    def __init__(self, text, lg):
        self.text, self.lg = text, lg
    def __call__(self, sid):
        t = self.text[sid]
        return mklang.to_st(t.get(self.lg, t['en']), sid)

LANG_NAMES = [b'English', b'Deutsch', b'Espa\xa4ol', b'Italiano', b'Fran\x87ais']
PAPER_NAMES = [b'A4', b'Letter', b'A5', b'Legal']
SIZES = [8, 9, 10, 12, 14, 18, 24, 36]
ZOOMS = [50, 75, 100, 125, 150, 200]
NCOLORS = 8
COLOURS = ['S_COL_BLACK', 'S_COL_RED', 'S_COL_GREEN', 'S_COL_BLUE', 'S_COL_CYAN', 'S_COL_MAGENTA',
           'S_COL_YELLOW', 'S_COL_GREY']

# ---- object trees ----------------------------------------------------------------
class Obj:
    def __init__(self, typ, x, y, w, h, flags=0, state=0, spec=0, ext=0, name=None):
        self.typ, self.flags, self.state, self.spec, self.ext = typ, flags, state, spec, ext
        self.x, self.y, self.w, self.h = x, y, w, h        # (chars, pixels) each
        self.children, self.name = [], name

def cp(chars, px=0):
    """a coordinate: characters and pixels"""
    while px < 0:
        chars -= 1; px += 8
    return (chars, px)

class Tree:
    def __init__(self, name, root):
        self.name, self.root = name, root
    def flatten(self):
        out = []
        def walk(o, parent):
            idx = len(out)
            out.append([o, parent, -1, -1, -1])
            first = last = -1
            for c in o.children:
                ci = walk(c, idx)
                if first < 0: first = ci
                if last >= 0: out[last][2] = ci
                last = ci
            out[idx][3], out[idx][4] = first, last
            if last >= 0: out[last][2] = idx
            return idx
        walk(self.root, -1)
        out[0][2] = -1
        return out

def add(parent, o):
    parent.children.append(o)
    return o

# ---- the dialog builder: the same layout rules as gemdlg.c ------------------------
T_TITLE, T_LABEL, T_TEXT, T_RADIO, T_CHECK, T_EDIT, T_BUTTON = range(7)

class Dlg:
    def __init__(self, title):
        self.items, self.row = [], 0
        if title is not None:
            self.item(T_TITLE, title, name='TITLE')
        self.row = 1
    def item(self, typ, s, wc=None, name=None, **kw):
        it = dict(type=typ, s=s, wc=len(s) if wc is None else wc, row=self.row, name=name, **kw)
        self.items.append(it)
        return it
    def nl(self): self.row += 1
    def label(self, s): self.item(T_LABEL, s)
    def text(self, s, name=None, wc=None): self.item(T_TEXT, s, wc, name)
    def radio(self, group, s, name, wc=None): self.item(T_RADIO, s, (wc or len(s)) + 2, name, group=group)
    def check(self, s, name): self.item(T_CHECK, s, len(s) + 2, name)
    def edit(self, prompt, n, after, valid, buf, name):
        pre = prompt + b' ' if prompt else b''
        post = (b' ' + after) if after else b''
        t = pre + b'_' * n + post
        self.item(T_EDIT, t, len(t), name, n=n, valid=valid, buf=buf, pre=pre, post=post)
    def button(self, s, name, flags=0): self.item(T_BUTTON, s, len(s), name, flags=flags)

    def build(self, tname, limit=76):
        items = self.items
        labw = 0
        for it in items:
            if it['type'] == T_LABEL and it['wc'] + 2 > labw: labw = it['wc'] + 2
        maxw = limit
        if labw > maxw // 2: labw = maxw // 2
        vrow, lastrow, x, maxx, nbtn, btnw = 0, -1, 0, 0, 0, 0
        for it in items:
            t = it['type']
            if t == T_BUTTON:
                nbtn += 1; btnw = max(btnw, it['wc']); continue
            if t == T_TITLE:
                it['row'], it['col'] = 0, 2
                maxx = max(maxx, it['wc'] + 2); continue
            if it['row'] != lastrow:
                vrow += 1; lastrow = it['row']; x = labw
                if t == T_LABEL:
                    it['row'], it['col'] = vrow, 2; continue
            if t == T_LABEL:
                vrow += 1; it['row'], it['col'] = vrow, 2; x = labw; continue
            if x + it['wc'] > maxw and x > labw:
                vrow += 1; x = labw
            it['row'], it['col'] = vrow, x + 2
            x += it['wc'] + 2
            maxx = max(maxx, x)
        btnw = max(btnw + 4, 8)
        nbrow, brw = 0, 0
        if nbtn:
            uniform = nbtn * (btnw + 2) + 4 <= limit
            rw, nbrow = 0, 1
            for it in items:
                if it['type'] != T_BUTTON: continue
                w = btnw if uniform else max(it['wc'] + 4, 8)
                if rw and rw + w + 2 + 4 > limit:
                    nbrow += 1; rw = 0
                it['bw'], it['brow'] = w, nbrow - 1
                rw += w + 2
                brw = max(brw, rw + 4)
        cols = max(maxx + 2, brw, 20)
        nrows = vrow + 1 + (nbrow + 1 if nbtn else 0)
        for k in range(nbrow):
            bx = cols - 2
            for it in reversed(items):
                if it['type'] != T_BUTTON or it['brow'] != k: continue
                bx -= it['bw']
                it['col'] = bx
                bx -= 2
        root = Obj(G_BOX, cp(0), cp(0), cp(cols), cp(nrows), state=OUTLINED, spec=0x00021100, name='ROOT')
        # radio groups first, in boxes
        done = set()
        for i, it in enumerate(items):
            if it['type'] != T_RADIO or it['group'] in done: continue
            done.add(it['group'])
            grp = [t for t in items if t['type'] == T_RADIO and t['group'] == it['group']]
            gx0 = min(t['col'] for t in grp); gy0 = min(t['row'] for t in grp)
            gx1 = max(t['col'] + t['wc'] for t in grp); gy1 = max(t['row'] + 1 for t in grp)
            box = add(root, Obj(G_IBOX, cp(gx0), cp(gy0), cp(gx1 - gx0), cp(gy1 - gy0), ext=2,
                                name=(grp[0]['name'] + 'GRP') if grp[0]['name'] else None))
            for t in grp:
                add(box, Obj(G_BUTTON, cp(t['col'] - gx0), cp(t['row'] - gy0), cp(t['wc']), cp(1),
                             flags=SELECTABLE | RBUTTON, spec=('str', t['s']), ext=1, name=t['name']))
        for it in items:
            t = it['type']
            if t in (T_TITLE, T_LABEL, T_TEXT):
                add(root, Obj(G_STRING, cp(it['col']), cp(it['row']), cp(it['wc']), cp(1),
                              spec=('str', it['s']), ext=4 if t == T_TITLE else 1, name=it['name']))
            elif t == T_CHECK:
                add(root, Obj(G_BUTTON, cp(it['col']), cp(it['row']), cp(it['wc']), cp(1), flags=SELECTABLE,
                              spec=('str', it['s']), ext=1, name=it['name']))
            elif t == T_EDIT:
                # the prompt and the text after it as strings, the field just the characters:
                # it looks right with and without the 3D look
                c, pre, post = it['col'], it['pre'], it['post']
                if pre:
                    add(root, Obj(G_STRING, cp(c), cp(it['row']), cp(len(pre)), cp(1), spec=('str', pre), ext=1))
                add(root, Obj(G_FTEXT, cp(c + len(pre)), cp(it['row']), cp(it['n']), cp(1), flags=EDITABLE,
                              spec=('ted', dict(text=it['buf'], tmpl=b'_' * it['n'], valid=it['valid'], color=0x1180)),
                              ext=1, name=it['name']))
                if post:
                    add(root, Obj(G_STRING, cp(c + len(pre) + it['n']), cp(it['row']), cp(len(post)), cp(1),
                                  spec=('str', post), ext=1))
            elif t == T_BUTTON:
                add(root, Obj(G_BUTTON, cp(it['col']), cp(nrows - nbrow + it['brow']), cp(it['bw']), cp(1),
                              flags=SELECTABLE | EXIT | it.get('flags', 0), spec=('str', it['s']), ext=3,
                              name=it['name']))
        return Tree(tname, root)

def digits(n): return b'9' * n
def anyc(n): return b'X' * n

# ---- the dialogs ----------------------------------------------------------------
def dlg_print(L):
    g = Dlg(L('S_PRINT_TITLE'))
    g.label(L('S_PRINTER')); g.text(b' ' * 30, 'PRINTER'); g.nl()
    g.label(L('S_PAGES'))
    g.radio(1, L('S_ALL_PAGES'), 'ALL')
    cur = L('S_CUR_PAGE') + b' (9999)'
    g.radio(1, cur, 'CUR')
    g.nl(); g.label(b'')
    g.radio(1, L('S_PAGES_FROM'), 'RANGE')
    g.edit(None, 4, None, digits(4), b'1', 'FROM')
    g.edit(L('S_PAGES_TO'), 4, None, digits(4), b'1', 'TO')
    g.nl(); g.label(b'')
    g.radio(1, L('S_PAGE_LIST'), 'LIST')
    g.edit(None, 16, None, anyc(40), b'1', 'LISTED')
    g.text(L('S_PAGE_LIST_HINT'))
    g.nl()
    g.label(L('S_COPIES'))
    g.edit(None, 2, None, digits(2), b'1', 'COPIES')
    g.button(L('S_CANCEL'), 'CANCEL')
    g.button(L('S_PRINT_BTN'), 'OK', DEFAULT)
    t = g.build('PRINT')
    return t

def dlg_prefs(L):
    g = Dlg(L('S_PREFS_TITLE'))
    g.label(L('S_LANGUAGE'))
    for i in range(5): g.radio(1, LANG_NAMES[i], 'LANG%d' % i)
    g.nl(); g.label(L('S_DATE_FORMAT'))
    for i, w in enumerate((10, 10, 10, 18, 28)): g.radio(2, b' ' * w, 'DATE%d' % i)   # today's date, at run time
    g.nl(); g.label(L('S_NEW_PAGES'))
    for i in range(4): g.radio(3, PAPER_NAMES[i], 'PAPER%d' % i)
    g.radio(4, L('S_PORTRAIT'), 'PORTRAIT'); g.radio(4, L('S_LANDSCAPE'), 'LAND')
    g.nl(); g.label(L('S_NEW_WINDOWS')); g.check(L('S_SHOW_TOOLBAR'), 'TOOLBAR')
    g.nl(); g.label(L('S_SPELLING')); g.check(L('S_LIVE_SPELL'), 'LIVE')
    g.nl(); g.label(L('S_DICTIONARY'))
    for i in range(5): g.radio(8, LANG_NAMES[i], 'DICT%d' % i)
    g.nl(); g.label(L('S_TEXT_COLOUR')); g.check(L('S_SHOW_COLOURS'), 'COLOURS')
    g.nl(); g.label(L('S_PDF_PICTURES'))
    g.radio(5, L('S_SCREEN'), 'PDF1'); g.radio(5, b'2x', 'PDF2'); g.radio(5, b'3x', 'PDF3')
    g.nl(); g.label(L('S_EDITING'))
    for i, s in enumerate(('S_D_NORMAL', 'S_D_DARK', 'S_D_GREEN', 'S_D_AMBER')): g.radio(6, L(s), 'DISP%d' % i)
    g.nl(); g.label(L('S_AUTOSAVE'))
    g.check(L('S_ON'), 'ASON')
    g.edit(L('S_EVERY'), 2, L('S_MINUTES'), digits(2), b'5', 'ASMIN')
    g.radio(7, L('S_BACKUP_COPY'), 'ASCOPY'); g.radio(7, L('S_SAVE_DOC'), 'ASDOC')
    g.button(L('S_PREFS_MORE'), 'MORE')
    g.button(L('S_CANCEL'), 'CANCEL')
    g.button(L('S_SAVE_BTN'), 'OK', DEFAULT)
    return g.build('PREFS')

def dlg_prefs2(L):
    g = Dlg(L('S_PREFS_TITLE'))
    g.label(L('S_KEEP'))
    for i, s in enumerate(('S_KEEP_ANY', 'S_KEEP_WIDOWS', 'S_KEEP_TOGETHER')): g.radio(1, L(s), 'KEEP%d' % i)
    g.nl(); g.label(L('S_DOCDIR'))
    g.edit(None, 40, None, anyc(40), b'', 'DOCDIR')
    g.button(L('S_CANCEL'), 'CANCEL')
    g.button(L('S_SAVE_BTN'), 'OK', DEFAULT)
    return g.build('PREFS2')

# ---- the font selector (gemtool.c) ------------------------------------------------
FROWS = 9
def fontsel(L):
    # positions as in fs_layout(): characters and pixels from the window's work area
    top = cp(1, 6)
    lh = FROWS
    root = Obj(G_BOX, cp(0), cp(0), cp(60), cp(1 + FROWS + 3 + 1, 6 + 8 + 8 + 6 + 4 + 8), spec=0x00001170, name='ROOT')
    def at(c, p=0): return cp(c, p)
    add(root, Obj(G_STRING, at(1), cp(0, 3), cp(20), cp(1), spec=('str', L('S_FONT')), name='LFONT'))
    add(root, Obj(G_STRING, at(31), cp(0, 3), cp(len(L('S_SIZE')) + 4), cp(1), spec=('str', L('S_SIZE') + b' 999'), name='LSIZE'))
    add(root, Obj(G_STRING, at(41), cp(0, 3), cp(len(L('S_STYLE'))), cp(1), spec=('str', L('S_STYLE')), name='LSTYLE'))
    fbox = add(root, Obj(G_BOX, at(1), top, cp(26), cp(lh), spec=0x00FF1170, name='FBOX'))
    for i in range(FROWS):
        add(fbox, Obj(G_STRING, cp(0, 1), cp(i, 1), cp(26, -2), cp(1), spec=('str', b' ' * 26), name='FROW%d' % i))
    fu = add(root, Obj(G_BOXCHAR, cp(27, 2), top, cp(2, 2), cp(1, 2), flags=TOUCHEXIT, spec=0x01FF1170, name='FUP'))
    add(root, Obj(G_BOXCHAR, cp(27, 2), cp(1 + lh - 1, 6 - 2), cp(2, 2), cp(1, 2), flags=TOUCHEXIT, spec=0x02FF1170, name='FDN'))
    trk = add(root, Obj(G_BOX, cp(27, 2), cp(2, 6 + 3), cp(2, 2), cp(lh - 2, -6), flags=TOUCHEXIT, spec=0x00FF1111, name='FTRK'))
    add(trk, Obj(G_BOX, cp(0), cp(0), cp(2, 2), cp(2), flags=TOUCHEXIT, spec=0x00FF1170, name='FTHUMB'))
    sbox = add(root, Obj(G_BOX, at(31), top, cp(6), cp(lh), spec=0x00FF1170, name='SBOX'))
    for i in range(FROWS):
        add(sbox, Obj(G_STRING, cp(0, 1), cp(i, 1), cp(6, -2), cp(1), spec=('str', b' ' * 5), name='SROW%d' % i))
    add(root, Obj(G_BOXCHAR, cp(37, 2), top, cp(2, 2), cp(1, 2), flags=TOUCHEXIT, spec=0x01FF1170, name='SUP'))
    add(root, Obj(G_BOXCHAR, cp(37, 2), cp(1 + lh - 1, 6 - 2), cp(2, 2), cp(1, 2), flags=TOUCHEXIT, spec=0x02FF1170, name='SDN'))
    for i, s in enumerate(('S_BOLD', 'S_ITALIC', 'S_UNDERLINE')):
        add(root, Obj(G_BUTTON, at(41), cp(1 + i, 6 + 6 * i), cp(len(L(s)) + 3), cp(1, 2),
                      flags=SELECTABLE, spec=('str', L(s)), name='ST%d' % i))
    add(root, Obj(G_BOX, at(1), cp(1 + lh, 6 + 8), cp(58), cp(3, 8), spec=0x00FF1170, name='PV'))
    cl, ap = len(L('S_CLOSE')) + 2, len(L('S_APPLY')) + 2
    by = cp(1 + lh + 3, 6 + 8 + 8 + 6)
    add(root, Obj(G_BUTTON, cp(60 - 1 - cl - 1 - ap), by, cp(ap), cp(1, 4), flags=SELECTABLE | EXIT | DEFAULT,
                  spec=('str', L('S_APPLY')), name='APPLY'))
    add(root, Obj(G_BUTTON, cp(60 - 1 - cl), by, cp(cl), cp(1, 4), flags=SELECTABLE | EXIT, spec=('str', L('S_CLOSE')), name='CLOSE'))
    return Tree('FONTSEL', root)

# ---- the toolbar and its icons ---------------------------------------------------
def read_icons(path):
    """the 16 x 16 icons from gemicon.c's table: name -> 16 words"""
    import re
    src = open(path, encoding='latin-1').read()
    icons = {}
    for m in re.finditer(r'/\*\s*(\w+)\s*\*/\s*\{([^}]*)\}', src):
        icons[m.group(1)] = [int(v, 16) for v in re.findall(r'0x([0-9A-Fa-f]{4})', m.group(2))]
    return icons

TB = [('UNDO', 'C_UNDO'), ('REDO', 'C_REDO'), None,
      ('BOLD', 'C_BOLD'), ('ITALIC', 'C_ITALIC'), ('UNDER', 'C_UNDER'), None,
      ('LEFT', 'C_JLEFT'), ('CENTRE', 'C_JCENTRE'), ('RIGHT', 'C_JRIGHT'), ('JUSTIFY', 'C_JFULL'), None,
      ('BULLETS', 'C_BULLETS'), ('NUMBERS', 'C_NUMBERS'), ('TABLE', 'C_TBLINS'), ('PGBRK', 'C_PGBRK'), None,
      ('FONT', 'TB_FONT'), ('SIZE', 'TB_SIZE'), ('COLOUR', 'TB_COLOR'), None,
      ('ZOOM', 'TB_ZOOM'), ('SPELL', 'C_SPELL'), ('CODES', 'C_SPLIT'), None,
      ('RULER', 'C_RULERS'), ('GUIDES', 'C_GUIDES')]
BTNW, POPW = 22, 30
ARROW = [0xFE00, 0x7C00, 0x3800]                     # the popup mark, 7 x 3

def toolbar(icons, big):
    tbh = 24 if big else 15
    ih = 16 if big else 8
    bh = tbh - 5
    root = Obj(G_BOX, cp(0), cp(0), cp(80), cp(0, tbh), spec=0x00001170, name='ROOT')    # white, no frame
    x = 4
    for t in TB:
        if t is None:
            x += 6; continue
        icon, cmd = t
        pop = cmd.startswith('TB_')
        w = (POPW if pop else BTNW) - 2
        box = add(root, Obj(G_BOX, cp(x // 8, x % 8), cp(0, 2), cp(w // 8, w % 8), cp(0, bh), flags=TOUCHEXIT,
                            spec=0x00011170, name=icon))
        data = icons[icon]
        if not big:
            data = [data[2 * r] | data[2 * r + 1] for r in range(8)]
        add(box, Obj(G_IMAGE, cp(0, 3), cp(0, (bh + 1 - ih) // 2), cp(2), cp(0, ih),
                     spec=('img', dict(words=data, wb=2, hl=ih, color=1)), name=icon + 'IMG'))
        if pop:
            add(box, Obj(G_IMAGE, cp(0, w - 9), cp(0, bh // 2 - 1), cp(1), cp(0, 3),
                         spec=('img', dict(words=ARROW, wb=2, hl=3, color=1)), name=icon + 'ARR'))
        x += w + 2
    add(root, Obj(G_BOX, cp(0), cp(0, tbh - 1), cp(80), cp(0, 1), spec=0x00001171, name='LINE'))
    return Tree('TOOLBAR' if big else 'TOOLBAR8', root), [t for t in TB if t]

# ---- popups ------------------------------------------------------------------------
def popup(name, texts):
    w = max(len(t) for t in texts) + 4
    root = Obj(G_BOX, cp(0), cp(0), cp(w), cp(len(texts)), spec=0x00FF1100, name='ROOT')
    for i, t in enumerate(texts):
        add(root, Obj(G_STRING, cp(0), cp(i), cp(w), cp(1), flags=SELECTABLE, spec=('str', b'  ' + t + b' ' * (w - 2 - len(t))),
                      name='I%d' % i))
    return Tree(name, root)

# ---- the menu bar -------------------------------------------------------------------
SEP = None
def menus():
    """the menus as in gem.c: (title, [(text, key, cmd) | SEP | ('DYN', name, count, width)])"""
    m_desk = [('M_ABOUT', None, 'C_ABOUT'), SEP] + [('ACC', None, None)] * 6
    m_file = [('M_NEW', '^N', 'C_NEW'), ('M_TEMPLATE', None, 'C_TEMPLATE'), ('M_OPEN', '^O', 'C_OPEN'),
              ('M_IMPORT', None, 'C_IMPORT'), ('M_CLOSE', '^U', 'C_CLOSE'), SEP,
              ('M_SAVEDOC', '^S', 'C_SAVE'), ('M_SAVEAS', '^\x01S', 'C_SAVEAS'), ('M_EXPORT', None, 'C_EXPORT'), SEP,
              ('M_PAGE', None, 'C_PAGE'), ('M_HEADFOOT', None, 'C_HEADFOOT'), ('M_PREVIEW', '^\x01P', 'C_PREVIEW'),
              ('M_PRINT', '^P', 'C_PRINT'), SEP, ('M_QUIT', '^Q', 'C_QUIT')]
    m_edit = [('M_UNDO', 'Undo', 'C_UNDO'), ('M_REDO', '\x01Undo', 'C_REDO'), SEP,
              ('M_CUT', '^X', 'C_CUT'), ('M_COPY', '^C', 'C_COPY'), ('M_PASTE', '^V', 'C_PASTE'), ('M_SELALL', '^A', 'C_SELALL'), SEP,
              ('M_FIND', '^F', 'C_FIND'), ('M_FINDNEXT', '^G', 'C_FINDNEXT'), SEP,
              ('M_PREVPAGE', '^\x01', 'C_PREVPAGE'), ('M_NEXTPAGE', '^\x02', 'C_NEXTPAGE'), ('M_GOTOPAGE', '^J', 'C_GOTOPAGE'), SEP,
              ('M_REPAG', 'F2', 'C_REPAG')]
    m_insert = [('M_DATE', '^D', 'C_INSDATE'), ('M_TIME', None, 'C_INSTIME'), SEP,
                ('M_PGBRK', '^Ret', 'C_PGBRK'), ('M_COLBRK', '^\x01Ret', 'C_COLBRK'), ('M_PAGENUM', None, 'C_PAGENUM'), SEP,
                ('M_TBLINS', None, 'C_TBLINS'), ('M_TBLPROP', None, 'C_TBLPROP'), ('M_ROWINS', None, 'C_ROWINS'),
                ('M_ROWDEL', None, 'C_ROWDEL'), ('M_COLINS', None, 'C_COLINS'), ('M_COLDEL', None, 'C_COLDEL'),
                ('M_TBLDEL', None, 'C_TBLDEL'), ('M_CHART', None, 'C_CHART'), SEP,
                ('M_PICTURE', None, 'C_PICTURE'), ('M_DRAWING', None, 'C_DRAWING'), SEP,
                ('M_TOC', None, 'C_TOC'), SEP, ('M_INSFILE', None, 'C_INSFILE')]
    m_font = [('M_FONTSEL', '^\x01F', 'C_FONTSEL'), ('M_SIZEOTHER', None, 'C_SIZEOTHER'), SEP] + \
             [(('SIZE', i), None, 'C_SIZE0 + %d' % i) for i in range(len(SIZES))] + [SEP] + \
             [('DYN', 'FONT', 24, 24, 'C_FONT0')] + [('M_MOREFONTS', None, 'C_MOREFONTS')]
    m_style = [('M_PLAIN', None, 'C_PLAIN'), SEP, ('M_BOLD', '^B', 'C_BOLD'), ('M_ITALIC', '^I', 'C_ITALIC'),
               ('M_UNDER', '^\x01U', 'C_UNDER'), SEP] + [(('COLOUR', i), None, 'C_COLOR0 + %d' % i) for i in range(NCOLORS)]
    m_para = [('M_JLEFT', '^L', 'C_JLEFT'), ('M_JCENTRE', '^E', 'C_JCENTRE'), ('M_JRIGHT', '^R', 'C_JRIGHT'),
              ('M_JFULL', '^\x01J', 'C_JFULL'), SEP,
              ('M_SP1', None, 'C_SP1'), ('M_SP15', None, 'C_SP15'), ('M_SP2', None, 'C_SP2'), SEP,
              ('M_INDENTS', None, 'C_INDENTS'), ('M_HANGING', None, 'C_HANGING'), ('M_BULLETS', 'F9', 'C_BULLETS'),
              ('M_NUMBERS', 'F10', 'C_NUMBERS'), SEP,
              ('M_HEAD1', None, 'C_HEAD1'), ('M_HEAD2', None, 'C_HEAD2'), ('M_HEAD3', None, 'C_HEAD3'), ('M_BODY', None, 'C_BODY'), SEP,
              ('M_WRAPCOL', None, 'C_WRAPCOL')]
    m_tools = [('M_SPELL', '^K', 'C_SPELL'), ('M_LIVESPELL', None, 'C_LIVESPELL'), ('M_THES', '\x01F7', 'C_THES'),
               ('M_SUMMARY', None, 'C_SUMMARY'), SEP, ('M_UDLOAD', None, 'C_UDLOAD'), ('M_UDNEW', None, 'C_UDNEW'), SEP,
               ('DYN', 'UD', 4, 24, 'C_UD0'), SEP, ('M_PREFS', None, 'C_PREFS')]
    m_window = [('M_VDOC', 'F4', 'C_VDOC'), ('M_SPLIT', '^Y', 'C_SPLIT'), ('M_VCODES', 'F6', 'C_VCODES'), SEP,
                ('M_ZOOMIN', 'F8', 'C_ZOOMIN'), ('M_ZOOMOUT', 'F7', 'C_ZOOMOUT'), SEP,
                ('M_TOOLBAR', '^T', 'C_TOOLBAR'), ('M_RULERS', '^\x01R', 'C_RULERS'), ('M_GUIDES', None, 'C_GUIDES'), SEP,
                ('M_TILEV', None, 'C_TILEV'), ('M_TILEH', None, 'C_TILEH'), ('M_CASCADE', None, 'C_CASCADE'), SEP,
                ('M_ICONIFY', '^Z', 'C_ICONIFY'), ('M_ICONALL', None, 'C_ICONALL'), ('M_BOTTOM', None, 'C_BOTTOM'),
                ('M_CYCLE', '^W', 'C_CYCLE'), SEP, ('DYN', 'WIN', 24, 24, 'C_WIN0'), ('M_WINMORE', None, 'C_WINMORE')]
    return [('MT_DESK', m_desk), ('MT_FILE', m_file), ('MT_EDIT', m_edit), ('MT_INSERT', m_insert), ('MT_FONT', m_font),
            ('MT_STYLE', m_style), ('MT_PARA', m_para), ('MT_TOOLS', m_tools), ('MT_WINDOW', m_window)]

def menubar(L):
    root = Obj(G_IBOX, cp(0), cp(0), cp(80), cp(25), name='ROOT')
    bar = add(root, Obj(G_BOX, cp(0), cp(0), cp(80), cp(1, 2), spec=0x00001100, name='BAR'))
    ms = menus()
    titles = [b' ' + L(t)[:12] + b' ' for t, _ in ms]
    total = sum(len(t) for t in titles)
    active = add(bar, Obj(G_IBOX, cp(2), cp(0), cp(total), cp(1, 3), name='ACTIVE'))
    tx, x = [], 0
    for i, t in enumerate(titles):
        add(active, Obj(G_TITLE, cp(x), cp(0), cp(len(t)), cp(1, 3), spec=('str', t), name='T%d' % i))
        tx.append(x); x += len(t)
    screen = add(root, Obj(G_IBOX, cp(0), cp(1, 3), cp(80), cp(23), name='SCREEN'))
    cmds, dyn, nacc = [], {}, 0
    for mi, (tid, items) in enumerate(ms):
        # the widest entry, as gem.c's build_menu did
        rows = []
        for it in items:
            if it is SEP: rows.append(SEP); continue
            if it[0] == 'DYN':
                for k in range(it[2]): rows.append(('DYN', it[1], k, it[3], it[4]))
                continue
            text, key, cmd = it
            if text == 'ACC': s = b'Desk Accessory'
            elif isinstance(text, tuple) and text[0] == 'SIZE': s = b'%d ' % SIZES[text[1]] + L('S_POINT')
            elif isinstance(text, tuple): s = L(COLOURS[text[1]])
            else: s = L(text)
            rows.append((s, key.encode('latin-1') if key else None, cmd, text))
        maxl = 0
        for r in rows:
            if r is SEP: continue
            if r[0] == 'DYN': l = r[3] + 4
            else:
                l = len(r[0]) + 4 + (len(r[1]) + 2 if r[1] else 0)
            maxl = max(maxl, l)
        maxl = max(12, min(42, maxl))
        dx = 2 + tx[mi]
        if dx + maxl > 78: dx = 78 - maxl
        box = add(screen, Obj(G_BOX, cp(dx), cp(0), cp(maxl), cp(len(rows)), spec=0x00FF1100, name='BOX%d' % mi))
        for k, r in enumerate(rows):
            if r is SEP:
                add(box, Obj(G_STRING, cp(0), cp(k), cp(maxl), cp(1), state=DISABLED, spec=('str', b'-' * maxl)))
                continue
            if r[0] == 'DYN':
                o = add(box, Obj(G_STRING, cp(0), cp(k), cp(maxl), cp(1), spec=('str', b' ' * maxl),
                                 name='%s%d' % (r[1], r[2])))
                cmds.append((o, '%s + %d' % (r[4], r[2])))
                continue
            s, key, cmd, text = r
            line = (b'  ' + s)[:maxl].ljust(maxl)
            if key:
                line = line[:maxl - len(key) - 1] + key + line[maxl - 1:]
            nm = None
            if isinstance(text, str) and text.startswith('M_'): nm = text[2:]
            if text == 'ACC': nm = 'ACC%d' % nacc; nacc += 1
            o = add(box, Obj(G_STRING, cp(0), cp(k), cp(maxl), cp(1), spec=('str', line), name=nm))
            cmds.append((o, cmd))
    return Tree('MENU', root), cmds

# ---- writing a resource file ---------------------------------------------------------
def word(c):
    ch, px = c
    assert 0 <= ch < 256 and 0 <= px < 256, c
    return (px << 8) | ch

def write_rsc(path, trees):
    strings = bytearray()
    def addstr(b):
        o = len(strings); strings.extend(b + b'\0'); return o
    flats = [(t, t.flatten()) for t in trees]
    objs, teds, bits, imgs = [], [], [], bytearray()
    for t, flat in flats:
        for o, parent, nxt, head, tail in flat:
            objs.append((o, nxt, head, tail))
    # pass 1: strings and images, then sizes
    strfix, tedlist, bitlist = {}, [], []
    for o, nxt, head, tail in objs:
        if isinstance(o.spec, tuple):
            kind, v = o.spec
            if kind == 'str':
                strfix[id(o)] = addstr(v)
            elif kind == 'ted':
                tl = len(v['valid']) + 1
                text = v['text'].ljust(tl - 1, b'\0')[:tl - 1]
                tedlist.append((o, addstr(text), addstr(v['tmpl']), addstr(v['valid']), v['color'], tl, len(v['tmpl']) + 1))
            elif kind == 'img':
                off = len(imgs)
                for w in v['words']: imgs.extend(struct.pack('>H', w))
                bitlist.append((o, off, v['wb'], v['hl'], v['color']))
    hdr = 36
    o_string = hdr
    o_imdata = o_string + len(strings) + (len(strings) & 1)
    o_frstr = o_imdata + len(imgs)
    o_frimg = o_frstr
    o_ted = o_frimg
    o_ib = o_ted + 28 * len(tedlist)
    o_bb = o_ib
    o_obj = o_bb + 14 * len(bitlist)
    o_tree = o_obj + 24 * len(objs)
    size = o_tree + 4 * len(trees)
    out = bytearray(size)
    out[o_string:o_string + len(strings)] = strings
    out[o_imdata:o_imdata + len(imgs)] = imgs
    tedoff, bboff = {}, {}
    for i, (o, t, m, v, col, tl, ml) in enumerate(tedlist):
        p = o_ted + 28 * i
        struct.pack_into('>lllhhhhhhhh', out, p, o_string + t, o_string + m, o_string + v, IBM, 0, 0, col, 0, 0, tl, ml)
        tedoff[id(o)] = p
    for i, (o, off, wb, hl, col) in enumerate(bitlist):
        p = o_bb + 14 * i
        struct.pack_into('>lhhhhh', out, p, o_imdata + off, wb, hl, 0, 0, col)
        bboff[id(o)] = p
    k = 0
    trindex = []
    for t, flat in flats:
        trindex.append(o_obj + 24 * k)
        n = len(flat)
        for j, (o, parent, nxt, head, tail) in enumerate(flat):
            spec = o.spec
            if isinstance(spec, tuple):
                spec = {'str': lambda: o_string + strfix[id(o)], 'ted': lambda: tedoff[id(o)], 'img': lambda: bboff[id(o)]}[spec[0]]()
            flags = o.flags | (LASTOB if j == n - 1 else 0)
            struct.pack_into('>hhhhhhlhhhh', out, o_obj + 24 * (k + j), nxt, head, tail, (o.ext << 8) | o.typ, flags, o.state,
                             spec, word(o.x), word(o.y), word(o.w), word(o.h))
        k += n
    for i, p in enumerate(trindex):
        struct.pack_into('>l', out, o_tree + 4 * i, p)
    struct.pack_into('>18H', out, 0, 0, o_obj, o_ted, o_ib, o_bb, o_frstr, o_string, o_imdata, o_frimg, o_tree,
                     len(objs), len(trees), len(tedlist), 0, len(bitlist), 0, 0, size)
    assert size < 65536, 'resource file too big'
    open(path, 'wb').write(out)
    return size

def main():
    src, outdir = sys.argv[1], sys.argv[2]
    text = load_strings(src)
    here = os.path.dirname(os.path.abspath(__file__))
    icons = read_icons(os.path.join(here, '..', 'gemicon.c'))
    names = None
    for lg in LANGS:
        L = Lang(text, lg)
        menu, cmds = menubar(L)
        trees = [menu, dlg_print(L), dlg_prefs(L), dlg_prefs2(L), fontsel(L)]
        tb16, tbl = toolbar(icons, True)
        tb8, _ = toolbar(icons, False)
        trees += [tb16, tb8,
                  popup('POPSIZE', [b'%d ' % s + L('S_POINT') for s in SIZES] + [L('M_SIZEOTHER')]),
                  popup('POPCOLOUR', [L(s) for s in COLOURS]),
                  popup('POPZOOM', [b'%d%%' % z for z in ZOOMS])]
        size = write_rsc(os.path.join(outdir, RSCNAME[lg]), trees)
        # the object numbers: the same in every language
        nm = []
        for t in trees:
            fl = t.flatten()
            nm.append((t.name, [(i, f[0].name) for i, f in enumerate(fl)]))
        if names is None:
            names = nm
            idx = dict((id(f[0]), i) for i, f in enumerate(menu.flatten()))
            first_cmds, first_tb = [(idx[id(o)], c) for o, c in cmds], tbl
        else:
            assert [[n for _, n in objs] for _, objs in nm] == [[n for _, n in objs] for _, objs in names], 'trees differ in ' + lg
            assert [len(t.flatten()) for t in trees] == [len(objs) for _, objs in names]
        print('%s: %d bytes' % (RSCNAME[lg], size))
    with open(os.path.join(outdir, 'edrsc.h'), 'w') as f:
        f.write('/* edrsc.h - generated by tools/mkrsc.py with EDWORD*.RSC; do not edit */\n')
        f.write('/* Copyright (c) 2026 Francis C Bullen. SPDX-License-Identifier: MIT (see LICENSE) */\n')
        f.write('#ifndef EDRSC_H\n#define EDRSC_H\n\n')
        for ti, (tname, objs) in enumerate(names):
            f.write('#define RT_%s %d\n' % (tname, ti))
        f.write('#define RT_COUNT %d\n\n' % len(names))
        pre = {'MENU': 'MN', 'PRINT': 'PR', 'PREFS': 'PF', 'PREFS2': 'PF2', 'FONTSEL': 'FS', 'TOOLBAR': 'TBO',
               'TOOLBAR8': None, 'POPSIZE': 'PS', 'POPCOLOUR': 'PC', 'POPZOOM': 'PZ'}
        for tname, objs in names:
            p = pre[tname]
            if not p: continue
            f.write('/* %s */\n' % tname)
            for i, n in objs:
                if n: f.write('#define %s_%s %d\n' % (p, n, i))
            f.write('#define %s_COUNT %d\n\n' % (p, len(objs)))
        f.write('/* menu entries and their commands */\n#define RSC_MENU_CMDS \\\n')
        f.write(', \\\n'.join('    { %d, %s }' % (o, c) for o, c in first_cmds if c) + '\n\n')
        f.write('/* toolbar buttons and their commands (TB_FONT... open popups) */\n#define RSC_TB_CMDS \\\n')
        f.write(', \\\n'.join('    { TBO_%s, %s }' % (ic, c) for ic, c in first_tb) + '\n')
        f.write('#define RSC_NTB %d\n\n#endif\n' % len(first_tb))

if __name__ == '__main__':
    main()
