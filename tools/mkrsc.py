#!/usr/bin/env python3
# Copyright (c) 2026 Francis C Bullen. SPDX-License-Identifier: MIT (see LICENSE)
"""mkrsc.py - EdWord's GEM resource files

usage: mkrsc.py lang/strings.txt OUTDIR

Writes OUTDIR/EDWORD.RSC (English), EDWORDDE.RSC, EDWORDES.RSC, EDWORDIT.RSC
and EDWORDFR.RSC - one resource file per language, each laid out for its own
words - and edrsc.h, the tree and object numbers they all share.

The trees: the menu bar, every dialog (Print, the two pages of the
Preferences, Page Layout, Header and Footer, tables, pictures, Go to Page,
Page Numbers, New Document and the rest), the dialogs in the Find,
Spelling, Thesaurus, Import and Export windows, the progress window, the
font selector, the toolbar (16 and 8 pixel high icons, for ST high and ST
medium resolution) and the toolbar's popup menus. EdWord builds none of
them itself. They are ordinary GEM
objects, so they can be looked at and changed with a resource editor
(ResourceMaster, Interface, ORCS...) as long as the object numbers stay the
same.

Dialog rows are one character apart in the files; EdWord spreads them to
one and a half when it loads them (it marks the objects to move with the
extended object type: 1 an item in a row, 2 a box of radio buttons, 3 an
exit button, 4 the title; a dialog's box 5, or 6 without buttons), so they
look the same in every resolution.
"""
import os, sys, struct

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import mklang

LANGS = mklang.LANGS
RSCNAME = {'en': 'EDWORD.RSC', 'de': 'EDWORDDE.RSC', 'es': 'EDWORDES.RSC', 'it': 'EDWORDIT.RSC', 'fr': 'EDWORDFR.RSC',
           'cs': 'EDWORDCS.RSC'}

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
        return mklang.to_st(t.get(self.lg, t['en']), sid, self.lg)
    def names(self):
        # the languages by their own names, in this file's character set
        return [mklang.to_st(n if self.lg == 'cs' or n.isascii() or 'Č' not in n else 'Cestina', 'names', self.lg)
                for n in LANG_NAMES]

LANG_NAMES = ['English', 'Deutsch', 'Español', 'Italiano', 'Français', 'Čeština']
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
T_TITLE, T_LABEL, T_TEXT, T_RADIO, T_CHECK, T_EDIT, T_BUTTON, T_BIG, T_SIDE = range(9)

class Dlg:
    def __init__(self, title):
        self.items, self.row = [], 0
        if title is not None:
            self.item(T_TITLE, title, name='TITLE')
        self.row = 1
    def item(self, typ, s, wc=None, name=None, **kw):
        it = dict(type=typ, s=s, wc=len(s) if wc is None else wc, row=self.row, lrow=self.row, name=name, **kw)
        self.items.append(it)
        return it
    def nl(self): self.row += 1
    def label(self, s): self.item(T_LABEL, s)
    def text(self, s, name=None, wc=None): self.item(T_TEXT, s, wc, name)
    def radio(self, group, s, name, wc=None): self.item(T_RADIO, s, (wc or len(s)) + 2, name, group=group)
    def check(self, s, name): self.item(T_CHECK, s, len(s) + 2, name)
    def edit(self, prompt, n, after, valid, buf, name, post_name=None):
        pre = prompt + b' ' if prompt else b''
        post = (b' ' + after) if after else b''
        t = pre + b'_' * n + post
        self.item(T_EDIT, t, len(t), name, n=n, valid=valid, buf=buf, pre=pre, post=post, post_name=post_name)
    def button(self, s, name, flags=0): self.item(T_BUTTON, s, len(s), name, flags=flags)
    def big(self, s, wc, name, flags=EXIT): self.item(T_BIG, s, wc, name, flags=flags)
    # a box to the right of this row and the n-1 after it (a picture EdWord
    # draws in it), wc characters wide
    def side(self, wc, n, name): self.item(T_SIDE, b'', wc, name, n=n)

    def build(self, tname, limit=76, framed=True):
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
            if t == T_SIDE: continue
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
            if t == T_TEXT and x == labw and labw + it['wc'] > maxw:
                x = 0           # a long line of its own: from the left edge
            it['row'], it['col'] = vrow, x + 2
            x += it['wc'] + 2
            maxx = max(maxx, x)
        for it in items:
            if it['type'] != T_SIDE: continue
            span = [o for o in items if o['type'] not in (T_SIDE, T_BUTTON, T_TITLE) and it['lrow'] <= o['lrow'] < it['lrow'] + it['n']]
            it['row'] = min(o['row'] for o in span)
            it['col'] = max(o['col'] + o['wc'] for o in span) + 2
            it['nr'] = max(o['row'] for o in span) - it['row'] + 1
            maxx = max(maxx, it['col'] + it['wc'])
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
        root = Obj(G_BOX, cp(0), cp(0), cp(cols), cp(nrows), state=OUTLINED if framed else 0,
                   spec=0x00021100 if framed else 0x00001100, ext=5 if nbtn else 6, name='ROOT')
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
                                  spec=('str', post), ext=1, name=it.get('post_name')))
            elif t == T_BIG:
                add(root, Obj(G_BUTTON, cp(it['col']), cp(it['row']), cp(it['wc']), cp(1),
                              flags=SELECTABLE | it.get('flags', 0), spec=('str', it['s'].ljust(it['wc'])[:it['wc']] if not it['s'].strip() else it['s']),
                              ext=1, name=it['name']))
            elif t == T_SIDE:
                add(root, Obj(G_BOX, cp(it['col']), cp(it['row']), cp(it['wc']), cp(it['nr']),
                              spec=0x00011100, ext=2, name=it['name']))
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
    g.label(L('S_PRT_COLOURS'))
    g.radio(2, L('S_PRT_COLOUR'), 'COLOUR'); g.radio(2, L('S_PRT_MONO'), 'MONO')
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
    for i, n in enumerate(L.names()): g.radio(1, n, 'LANG%d' % i)
    g.nl(); g.label(L('S_DATE_FORMAT'))
    for i, w in enumerate((10, 10, 10, 10, 18, 28)): g.radio(2, b' ' * w, 'DATE%d' % i)   # today's date, at run time
    g.nl(); g.label(L('S_NEW_PAGES'))
    for i in range(4): g.radio(3, PAPER_NAMES[i], 'PAPER%d' % i)
    g.radio(4, L('S_PORTRAIT'), 'PORTRAIT'); g.radio(4, L('S_LANDSCAPE'), 'LAND')
    g.nl(); g.label(L('S_NEW_WINDOWS')); g.check(L('S_SHOW_TOOLBAR'), 'TOOLBAR')
    g.nl(); g.label(L('S_TEXT_COLOUR')); g.check(L('S_SHOW_COLOURS'), 'COLOURS')
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
    # spelling and PDF here: the first page has the languages and dates
    g.nl(); g.label(L('S_SPELLING')); g.check(L('S_LIVE_SPELL'), 'LIVE')
    g.nl(); g.label(L('S_DICTIONARY'))
    for i, n in enumerate(L.names()): g.radio(8, n, 'DICT%d' % i)
    g.nl(); g.label(L('S_PDF_PICTURES'))
    g.radio(5, L('S_SCREEN'), 'PDF1'); g.radio(5, b'2x', 'PDF2'); g.radio(5, b'3x', 'PDF3')
    g.nl(); g.label(L('S_DOCDIR'))
    g.edit(None, 40, None, anyc(40), b'', 'DOCDIR')
    g.button(L('S_CANCEL'), 'CANCEL')
    g.button(L('S_SAVE_BTN'), 'OK', DEFAULT)
    return g.build('PREFS2')


def blank(n): return b' ' * n

def dlg_page(L):
    g = Dlg(L('S_PAGE_TITLE'))
    g.label(L('S_PAPER'))
    for i in range(4): g.radio(1, PAPER_NAMES[i], 'PAPER%d' % i)
    g.nl(); g.label(L('S_ORIENT'))
    g.radio(2, L('S_PORTRAIT'), 'PORTRAIT'); g.radio(2, L('S_LANDSCAPE'), 'LAND')
    g.nl(); g.label(L('S_MARGINS_MM'))
    g.edit(L('S_TOP'), 3, None, digits(3), b'', 'MT')
    g.edit(L('S_BOTTOM'), 3, None, digits(3), b'', 'MB')
    g.edit(L('S_LEFT_M'), 3, None, digits(3), b'', 'ML')
    g.edit(L('S_RIGHT_M'), 3, None, digits(3), b'', 'MR')
    g.nl(); g.label(L('S_COLUMNS'))
    g.edit(None, 1, None, digits(1), b'', 'COLS')
    g.edit(L('S_GAP'), 3, L('S_MM'), digits(3), b'', 'GAP')
    g.nl(); g.label(L('S_WRAPCOL'))
    g.edit(None, 3, L('S_ZERO_OFF'), digits(3), b'', 'WRAP')
    g.nl(); g.label(L('S_APPLY_TO'))
    g.radio(3, L('S_WHOLE_DOC'), 'WHOLE'); g.radio(3, L('S_THIS_PAGE'), 'ONE'); g.radio(3, L('S_THIS_PAGE_ON'), 'AFTER')
    g.button(L('S_CANCEL'), 'CANCEL')
    g.button(L('S_BTN_OK'), 'OK', DEFAULT)
    return g.build('PAGE')

def dlg_wrap(L):
    g = Dlg(L('S_WRAP_TITLE'))
    g.label(L('S_WRAPCOL')); g.edit(None, 3, None, digits(3), b'', 'WRAP')
    g.nl(); g.text(L('S_WRAP_HINT'))
    g.button(L('S_CANCEL'), 'CANCEL')
    g.button(L('S_BTN_OK'), 'OK', DEFAULT)
    return g.build('WRAP')

def dlg_size(L):
    g = Dlg(L('S_SIZE_TITLE'))
    g.label(L('S_POINT_SIZE')); g.edit(None, 3, b'(4-120)', digits(3), b'', 'SIZE')
    g.button(L('S_CANCEL'), 'CANCEL')
    g.button(L('S_BTN_OK'), 'OK', DEFAULT)
    return g.build('SIZE')

def dlg_headfoot(L):
    g = Dlg(L('S_HF_TITLE'))
    jn = [L('S_J_LEFT'), L('S_J_CENTRE'), L('S_J_RIGHT')]
    for i in range(2):
        g.label(L('S_FOOTER' if i else 'S_HEADER'))
        g.edit(None, 56, None, anyc(56), b'', 'TEXT%d' % i)
        g.nl(); g.label(b'')
        for k in range(3): g.radio(1 + i, jn[k], 'J%d%d' % (i, k))
        g.nl()
    g.label(L('S_SIZE'))
    g.edit(None, 2, L('S_POINT'), digits(2), b'', 'SIZE')
    g.check(L('S_FIRST_PAGE_TOO'), 'FIRST')
    g.nl(); g.label(L('S_FIELDS')); g.text(b'{page} {pages} {date} {time} {file}')
    g.button(L('S_CANCEL'), 'CANCEL')
    g.button(L('S_BTN_OK'), 'OK', DEFAULT)
    return g.build('HEADFOOT')

def dlg_indents(L):
    g = Dlg(L('S_INDENTS_TITLE'))
    g.label(L('S_LEFT_INDENT')); g.edit(None, 3, L('S_MM'), digits(3), b'', 'LEFT')
    g.nl(); g.label(L('S_FIRST_LINE')); g.edit(None, 4, L('S_MM'), anyc(4), b'', 'FIRST')
    g.nl(); g.text(L('S_HANG_HINT'))
    g.button(L('S_CANCEL'), 'CANCEL')
    g.button(L('S_BTN_OK'), 'OK', DEFAULT)
    return g.build('INDENTS')

def dlg_table(L, insert):
    g = Dlg(L('S_TBL_INS_TITLE' if insert else 'S_TBL_PROP_TITLE'))
    g.label(L('S_ROWS')); g.edit(None, 3, None, digits(3), b'', 'ROWS')
    g.nl(); g.label(L('S_COLUMNS')); g.edit(None, 2, b'(1-12)', digits(2), b'', 'COLS')
    g.nl()
    if not insert:
        g.label(L('S_COL_WIDTHS'))
        for i in range(12): g.edit(None, 3, None, digits(3), b'', 'W%d' % i)
        g.nl()
    g.label(b''); g.check(L('S_GRID_LINES'), 'GRID')
    if not insert:
        # the table's rows and columns (not in the Insert menu: it would be
        # too tall for TOS)
        g.nl(); g.label(b'')
        for s, n in (('M_ROWINS', 'ROWINS'), ('M_ROWDEL', 'ROWDEL'), ('M_COLINS', 'COLINS'), ('M_COLDEL', 'COLDEL'),
                     ('M_TBLDEL', 'TBLDEL')):
            g.big(L(s), len(L(s)) + 2, n)
    g.button(L('S_CANCEL'), 'CANCEL')
    g.button(L('S_INSERT' if insert else 'S_BTN_OK'), 'OK', DEFAULT)
    return g.build('TBLINS' if insert else 'TBLPROP')

def dlg_objprops(L, drawing):
    g = Dlg(L('S_DRAW_TITLE' if drawing else 'S_PIC_TITLE'))
    g.label(L('S_POSITION_MM'))
    g.edit(L('S_FROM_LEFT'), 4, None, digits(4), b'', 'X')
    g.edit(L('S_FROM_TOP'), 4, None, digits(4), b'', 'Y')
    g.nl(); g.label(L('S_SIZE_MM'))
    g.edit(L('S_WIDTH'), 4, None, digits(4), b'', 'W')
    g.edit(L('S_HEIGHT'), 4, None, digits(4), b'', 'H')
    g.nl(); g.label(L('S_PAGE')); g.edit(None, 4, None, digits(4), b'', 'PAGE')
    g.nl(); g.label(L('S_TEXT_WRAP'))
    g.radio(1, L('S_WRAP_NONE'), 'WNONE'); g.radio(1, L('S_WRAP_BOX'), 'WBOX')
    if drawing: g.radio(1, L('S_WRAP_SHAPE'), 'WSHAPE')
    g.nl(); g.label(b''); g.check(L('S_FRAME'), 'FRAME')
    g.button(L('S_DELETE'), 'DEL')
    if drawing: g.button(L('S_EDIT_DRAWING'), 'EDIT')
    g.button(L('S_CANCEL'), 'CANCEL')
    g.button(L('S_BTN_OK'), 'OK', DEFAULT)
    return g.build('DRAWPROPS' if drawing else 'PICPROPS')

def dlg_summary(L):
    g = Dlg(L('S_SUMMARY_TITLE'))
    g.text(blank(40), 'NAME')
    g.nl()
    for i, s in enumerate(('S_ST_PAGES', 'S_ST_WORDS', 'S_ST_CHARS', 'S_ST_CHARS_NS', 'S_ST_PARAS', 'S_ST_LINES',
                           'S_ST_TABLES', 'S_ST_OBJECTS')):
        g.label(L(s)); g.text(blank(8), 'V%d' % i); g.nl()
    g.button(L('S_BTN_OK'), 'OK', DEFAULT)
    return g.build('SUMMARY')

def dlg_about(L):
    g = Dlg(b'EdWord')
    g.text(blank(40), 'VERSION'); g.nl()
    g.text(L('S_ABOUT_ATARI'), 'LINE1'); g.nl()
    g.text(L('S_ABOUT_EWR')); g.nl()
    g.text(L('S_ABOUT_LINE3')); g.nl()
    g.text(L('S_ABOUT_THES'))
    g.button(L('S_BTN_OK'), 'OK', DEFAULT)
    return g.build('ABOUT')

def dlg_textimport(L):
    g = Dlg(L('S_IMPORT_TITLE'))
    g.text(blank(60), 'INFO'); g.nl()
    g.label(L('S_CHARACTERS'))
    for i, s in enumerate((L('S_AUTO'), b'Atari ST', b'Windows', b'UTF-8', b'UTF-16')): g.radio(1, s, 'CS%d' % i)
    g.nl(); g.label(L('S_LINES'))
    g.radio(2, L('S_ONE_PARA'), 'ONE'); g.radio(2, L('S_JOIN_LINES'), 'JOIN')
    g.button(L('S_CANCEL'), 'CANCEL')
    g.button(L('S_IMPORT'), 'OK', DEFAULT)
    return g.build('TEXTIMP')

def dlg_textexport(L):
    g = Dlg(L('S_EXPORT_TITLE'))
    g.label(L('S_LINE_ENDINGS'))
    g.radio(1, b'Atari/DOS CR LF', 'DOS'); g.radio(1, b'Unix LF', 'UNIX')
    g.nl(); g.label(L('S_CHARACTERS'))
    for i, s in enumerate((b'Atari ST', b'Windows', b'UTF-8', b'UTF-16')): g.radio(2, s, 'CS%d' % i)
    g.nl(); g.text(L('S_EXPORT_HINT_EWR'))
    g.button(L('S_CANCEL'), 'CANCEL')
    g.button(L('S_EXPORT'), 'OK', DEFAULT)
    return g.build('TEXTEXP')

def dlg_goto(L):
    g = Dlg(L('S_GOTO_TITLE'))
    g.label(L('S_GOTO_PAGE')); g.edit(None, 4, blank(16), digits(4), b'', 'PAGE', post_name='OF')
    g.button(L('S_CANCEL'), 'CANCEL')
    g.button(L('S_GO'), 'OK', DEFAULT)
    return g.build('GOTO')

def dlg_pagenum(L):
    g = Dlg(L('S_PN_TITLE'))
    g.label(L('S_PN_WHERE')); g.radio(1, L('S_PN_TOP'), 'TOP'); g.radio(1, L('S_PN_BOTTOM'), 'BOTTOM')
    g.nl(); g.label(L('S_PN_ALIGN'))
    for k, s in enumerate(('S_J_LEFT', 'S_J_CENTRE', 'S_J_RIGHT')): g.radio(2, L(s), 'J%d' % k)
    g.nl(); g.label(L('S_PN_STYLE'))
    ex = [b'1', L('S_PN_PAGE') + b' 1', L('S_PN_PAGE') + b' 1 ' + L('S_PN_OF') + b' 3', b'- 1 -']
    for k in range(4): g.radio(3, ex[k], 'S%d' % k)
    g.nl(); g.label(b''); g.check(L('S_FIRST_PAGE_TOO'), 'FIRST')
    g.button(L('S_PN_REMOVE'), 'REMOVE')
    g.button(L('S_CANCEL'), 'CANCEL')
    g.button(L('S_BTN_OK'), 'OK', DEFAULT)
    return g.build('PAGENUM')

def dlg_template(L):
    g = Dlg(L('S_TPL_TITLE'))
    g.label(L('S_TPL_KIND'))
    for i, s in enumerate(('S_TPL_BLANK', 'S_TPL_UK', 'S_TPL_US', 'S_TPL_NEWS')): g.radio(1, L(s), 'K%d' % i)
    g.nl(); g.label(L('S_TPL_NAME')); g.edit(None, 40, None, anyc(40), b'', 'NAME')
    g.nl(); g.label(L('S_TPL_ADDR')); g.edit(None, 40, None, anyc(40), b'', 'A0')
    for i in (1, 2):
        g.nl(); g.label(b''); g.edit(None, 40, None, anyc(40), b'', 'A%d' % i)
    g.nl(); g.label(L('S_TPL_NTITLE')); g.edit(None, 40, None, anyc(40), b'', 'NTITLE')
    g.button(L('S_CANCEL'), 'CANCEL')
    g.button(L('S_TPL_CREATE'), 'OK', DEFAULT)
    return g.build('TEMPLATE')


# ---- colour themes (gemtheme.c): the same names, in the same order -----------
THEMES = [b'Atari', b'Aubergine', b'Blush', b'Classic', b'Damson', b'Drizzle', b'Dune', b'Harvest', b'Lagoon',
          b'Lavender', b'Nautical', b'Pine', b'Shale', b'Squash', b'Sycamore', b'Tempest', b'Terracotta']

def dlg_theme(L):
    g = Dlg(L('S_THEME_TITLE'))
    g.side(26, 7, 'PREVIEW')
    g.label(L('S_THEME')); g.big(blank(18), 18, 'PICK')
    for i, s in enumerate(('S_TH_FACE', 'S_TH_LIGHT', 'S_TH_DARK', 'S_TH_TEXT', 'S_TH_MARGIN', 'S_TH_DESK')):
        g.nl(); g.label(L(s))
        for c in b'RGB':
            g.edit(bytes([c]), 3, None, digits(3), b'', '%s%d' % (chr(c), i))
    g.nl(); g.label(L('S_APPLY_TO'))
    g.radio(9, L('S_THEME_THISDOC'), 'THISDOC'); g.radio(9, L('S_THEME_ALLDOCS'), 'ALLDOCS')
    g.nl(); g.text(L('S_THEME_HINT'))
    g.button(L('S_APPLY'), 'APPLY')
    g.button(L('S_CANCEL'), 'CANCEL')
    g.button(L('S_BTN_OK'), 'OK', DEFAULT)
    return g.build('THEME')

# ---- the tool windows (gemtwin.c): dialogs without a frame or a title ----------
def tw_find(L):
    g = Dlg(None)
    g.label(L('S_FIND_WHAT')); g.edit(None, 30, None, anyc(30), b'', 'WHAT')
    g.nl(); g.label(L('S_REPLACE_WITH')); g.edit(None, 30, None, anyc(30), b'', 'WITH')
    g.nl(); g.label(b'')
    g.check(L('S_MATCH_CASE'), 'CASE'); g.check(L('S_WHOLE_WORDS'), 'WORD'); g.check(L('S_BACKWARDS'), 'BACK')
    g.nl(); g.text(blank(40), 'STATUS')
    g.button(L('S_FIND_NEXT'), 'NEXT', DEFAULT)
    g.button(L('S_REPLACE'), 'REP')
    g.button(L('S_REPLACE_ALL'), 'ALL')
    g.button(L('S_CLOSE'), 'CLOSE')
    return g.build('FIND', framed=False)

def tw_spell(L):
    g = Dlg(None)
    g.text(blank(46), 'HEAD'); g.nl()
    g.label(L('S_CHANGE_TO')); g.edit(None, 31, None, anyc(31), b'', 'CHG')
    g.nl(); g.label(L('S_SUGGESTIONS'))
    for i in range(6): g.big(blank(18), 18, 'SUG%d' % i)
    g.button(L('S_REPLACE'), 'REP', DEFAULT)
    g.button(L('S_IGNORE'), 'IGN')
    g.button(L('S_IGNORE_ALL'), 'IGA')
    g.button(L('S_ADD'), 'ADD')
    g.button(L('S_STOP'), 'STOP')
    return g.build('SPELL', framed=False)

def tw_thes(L):
    g = Dlg(None)
    g.label(L('S_TH_WORD')); g.edit(None, 30, None, anyc(30), b'', 'WORD')
    g.big(L('S_LOOK_UP'), len(L('S_LOOK_UP')) + 4, 'LOOK')
    g.nl(); g.label(L('S_MEANINGS'))
    for i in range(6): g.big(blank(21), 21, 'M%d' % i)
    g.nl(); g.label(L('S_SYNONYMS'))
    for i in range(10): g.big(blank(16), 16, 'S%d' % i)
    g.nl(); g.label(L('S_REPLACE_WITH')); g.edit(None, 30, None, anyc(30), b'', 'WITH')
    g.nl(); g.text(blank(46), 'MSG')
    g.button(L('S_REPLACE'), 'REP', DEFAULT)
    g.button(L('S_CLOSE'), 'CLOSE')
    return g.build('THES', framed=False)

FMTS = lambda L: [b'RTF', L('S_FMT_TEXT'), b'Markdown', b'1st Word Plus', b'Protext', b'PDF']

def tw_import(L):
    g = Dlg(None)
    g.label(L('S_FORMAT'))
    g.radio(1, L('S_AUTO'), 'F0')
    for i in range(5):
        if i == 2: g.nl(); g.label(b'')
        g.radio(1, FMTS(L)[i], 'F%d' % (i + 1))
    g.nl(); g.label(L('S_CHARACTERS'))
    for i, s in enumerate((L('S_AUTO'), b'Atari ST', b'Windows', b'UTF-8', b'UTF-16')): g.radio(2, s, 'C%d' % i)
    g.nl(); g.label(L('S_LINES')); g.radio(3, L('S_ONE_PARA'), 'J0'); g.radio(3, L('S_JOIN_LINES'), 'J1')
    g.nl(); g.label(L('S_INTO')); g.radio(4, L('S_INTO_NEW'), 'I0')
    g.nl(); g.label(b''); g.radio(4, L('S_INTO_CURSOR'), 'I1')
    g.nl(); g.text(blank(46), 'STATUS')
    g.button(L('S_CLOSE'), 'CLOSE')
    g.button(L('M_IMPORT'), 'GO', DEFAULT)
    return g.build('IMPORT', framed=False)

def tw_export(L):
    g = Dlg(None)
    lab = L('S_XF_DOC').split(b'%')[0].rstrip()
    g.label(lab); g.text(blank(36), 'DOC')
    g.nl(); g.label(L('S_FORMAT'))
    for i in range(6):
        if i == 3: g.nl(); g.label(b'')
        g.radio(1, FMTS(L)[i], 'F%d' % i)
    g.nl(); g.label(L('S_LINE_ENDINGS')); g.radio(2, b'Atari/DOS CR LF', 'E0'); g.radio(2, b'Unix LF', 'E1')
    g.nl(); g.label(L('S_CHARACTERS'))
    for i, s in enumerate((b'Atari ST', b'Windows', b'UTF-8', b'UTF-16')): g.radio(3, s, 'C%d' % i)
    g.nl(); g.label(L('S_PDF_PICTURES'))
    g.radio(4, L('S_SCREEN'), 'P0'); g.radio(4, b'2x', 'P1'); g.radio(4, b'3x', 'P2')
    g.nl(); g.text(blank(46), 'STATUS')
    g.button(L('S_CLOSE'), 'CLOSE')
    g.button(L('M_EXPORT'), 'GO', DEFAULT)
    return g.build('EXPORT', framed=False)

# ---- the progress window (gemdlg.c): a bar, a percentage, maybe Cancel -----
GAUGEW = 36
def gauge(L, cancel):
    g = Dlg(None)
    g.text(blank(GAUGEW), 'BAR'); g.nl()
    g.text(b'  0%', 'PCT')
    if cancel: g.button(L('S_CANCEL'), 'CANCEL')
    t = g.build('GAUGEC' if cancel else 'GAUGE', framed=False)
    # the bar: a white box with a frame and the black fill inside
    for o in t.root.children:
        if o.name == 'BAR':
            o.typ, o.spec = G_BOX, 0x00011170
            o.children.append(Obj(G_BOX, cp(0), cp(0), cp(0), cp(1), spec=0x00001171, name='FILL'))
    return t

# ---- the font selector (gemtool.c) ------------------------------------------------
FROWS = 9
def fontsel(L):
    # positions as in fs_layout(): characters and pixels from the window's work area
    top = cp(1, 6)
    lh = FROWS
    root = Obj(G_BOX, cp(0), cp(0), cp(60), cp(1 + FROWS + 3 + 1, 6 + 8 + 8 + 10 + 4 + 8), spec=0x00001100, name='ROOT')
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
    by = cp(1 + lh + 3, 6 + 8 + 8 + 10)
    add(root, Obj(G_BUTTON, cp(60 - 1 - cl - 1 - ap), by, cp(ap), cp(1, 4), flags=SELECTABLE | EXIT | DEFAULT,
                  spec=('str', L('S_APPLY')), name='APPLY'))
    add(root, Obj(G_BUTTON, cp(60 - 1 - cl), by, cp(cl), cp(1, 4), flags=SELECTABLE | EXIT, spec=('str', L('S_CLOSE')), name='CLOSE'))
    return Tree('FONTSEL', root)

# ---- the toolbar and its icons ---------------------------------------------------
def read_icons(path):
    """the 16 x 16 icons from tools/icons.h: name -> 16 words; and the colour
    layers: [(name, pen, 16 words)]"""
    import re
    src = open(path, encoding='latin-1').read()
    icons, layers = {}, []
    for m in re.finditer(r'/\*\s*(\w+)\s*\*/\s*\{([^}]*)\}', src):
        icons[m.group(1)] = [int(v, 16) for v in re.findall(r'0x([0-9A-Fa-f]{4})', m.group(2))]
    for m in re.finditer(r'\{ IC_(\w+), (\d+), \{([^}]*)\} \}', src):
        layers.append((m.group(1), int(m.group(2)), [int(v, 16) for v in re.findall(r'0x([0-9A-Fa-f]{4})', m.group(3))]))
    return icons, layers

TB = [('UNDO', 'C_UNDO'), ('REDO', 'C_REDO'), None,
      ('BOLD', 'C_BOLD'), ('ITALIC', 'C_ITALIC'), ('UNDER', 'C_UNDER'), None,
      ('LEFT', 'C_JLEFT'), ('CENTRE', 'C_JCENTRE'), ('RIGHT', 'C_JRIGHT'), ('JUSTIFY', 'C_JFULL'), None,
      ('BULLETS', 'C_BULLETS'), ('NUMBERS', 'C_NUMBERS'), ('TABLE', 'C_TBLINS'), ('PGBRK', 'C_PGBRK'), None,
      ('FONT', 'TB_FONT'), ('SIZE', 'TB_SIZE'), ('COLOUR', 'TB_COLOR'), None,
      ('ZOOM', 'TB_ZOOM'), ('SPELL', 'C_SPELL'), ('CODES', 'C_SPLIT'), None,
      ('RULER', 'C_RULERS'), ('GUIDES', 'C_GUIDES')]
BTNW, POPW = 22, 30
ARROW = [0xFE00, 0x7C00, 0x3800]                     # the popup mark, 7 x 3

def half(rows):
    """16 rows to 8: each pair OR-ed together"""
    return [rows[2 * r] | rows[2 * r + 1] for r in range(8)]

def toolbar(icons, big):
    """each button: a box holding its icon as images - the whole icon in black
    (IMG, for fewer than 16 colours), and for 16 colours and more the
    coloured layers (L0, L1) under the rest of the icon in black (IMGC) -
    and the popup mark (ARR). EdWord hides the ones the screen doesn't want."""
    icons, layers = icons
    tbh = 24 if big else 15
    ih = 16 if big else 8
    bh = tbh - 5
    root = Obj(G_BOX, cp(0), cp(0), cp(80), cp(0, tbh), spec=0x00001100, name='ROOT')    # white (the AES's grey with 3D), no frame
    x = 4
    mono, colour, coloured = [], [], []
    for t in TB:
        if t is None:
            x += 6; continue
        icon, cmd = t
        pop = cmd.startswith('TB_')
        w = (POPW if pop else BTNW) - 2
        box = add(root, Obj(G_BOX, cp(x // 8, x % 8), cp(0, 2), cp(w // 8, w % 8), cp(0, bh), flags=TOUCHEXIT,
                            spec=0x00011170, name=icon))
        data = icons[icon]
        mine = [(p, m) for n, p, m in layers if n == icon]
        if not big:
            data = half(data)
            mine = [(p, half(m)) for p, m in mine]
        def img(words, color, name):
            return add(box, Obj(G_IMAGE, cp(0, 3), cp(0, (bh + 1 - ih) // 2), cp(2), cp(0, ih),
                                spec=('img', dict(words=words, wb=2, hl=ih, color=color)), name=name))
        cover = [0] * ih
        for k, (pen, m) in enumerate(mine):
            colour.append(img(m, pen, '%sL%d' % (icon, k)))
            cover = [a | b for a, b in zip(cover, m)]
        if mine:
            coloured.append(img([d & ~c & 0xFFFF for d, c in zip(data, cover)], 1, icon + 'IMGC'))
            mono.append(img(data, 1, icon + 'IMG'))
        else:
            img(data, 1, icon + 'IMG')          # the same in every number of colours
        if pop:
            add(box, Obj(G_IMAGE, cp(0, w - 9), cp(0, bh // 2 - 1), cp(1), cp(0, 3),
                         spec=('img', dict(words=ARROW, wb=2, hl=3, color=1)), name=icon + 'ARR'))
        x += w + 2
    add(root, Obj(G_BOX, cp(0), cp(0, tbh - 1), cp(80), cp(0, 1), spec=0x00001171, name='LINE'))
    t = Tree('TOOLBAR' if big else 'TOOLBAR8', root)
    t.groups = dict(mono=mono, colour=colour, coloured=coloured)
    return t, [t for t in TB if t]

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
              ('M_PREVPAGE', '^\x01', 'C_PREVPAGE'), ('M_NEXTPAGE', '^\x02', 'C_NEXTPAGE'), ('M_GOTOPAGE', '^J', 'C_GOTOPAGE')]
    # rows, columns and deleting a table are in Table Settings
    m_insert = [('M_DATE', '^D', 'C_INSDATE'), ('M_TIME', None, 'C_INSTIME'), SEP,
                ('M_PGBRK', '^Ret', 'C_PGBRK'), ('M_COLBRK', '^\x01Ret', 'C_COLBRK'), ('M_PAGENUM', None, 'C_PAGENUM'), SEP,
                ('M_TBLINS', None, 'C_TBLINS'), ('M_TBLPROP', None, 'C_TBLPROP'), SEP,
                ('M_CHART', None, 'C_CHART'), ('M_PICTURE', None, 'C_PICTURE'), ('M_DRAWING', None, 'C_DRAWING'),
                ('M_TOC', None, 'C_TOC'), ('M_INSFILE', None, 'C_INSFILE')]
    m_font = [('M_FONTSEL', '^\x01F', 'C_FONTSEL'), ('M_SIZEOTHER', None, 'C_SIZEOTHER'), SEP] + \
             [(('SIZE', i), None, 'C_SIZE0 + %d' % i) for i in range(len(SIZES))] + [SEP] + \
             [('DYN', 'FONT', 24, 24, 'C_FONT0')] + [('M_MOREFONTS', None, 'C_MOREFONTS')]
    m_style = [('M_PLAIN', None, 'C_PLAIN'), ('M_BOLD', '^B', 'C_BOLD'), ('M_ITALIC', '^I', 'C_ITALIC'),
               ('M_UNDER', '^\x01U', 'C_UNDER'), SEP,
               ('M_HEAD1', None, 'C_HEAD1'), ('M_HEAD2', None, 'C_HEAD2'), ('M_HEAD3', None, 'C_HEAD3'), ('M_BODY', None, 'C_BODY'), SEP] + \
              [(('COLOUR', i), None, 'C_COLOR0 + %d' % i) for i in range(NCOLORS)]
    m_para = [('M_JLEFT', '^L', 'C_JLEFT'), ('M_JCENTRE', '^E', 'C_JCENTRE'), ('M_JRIGHT', '^R', 'C_JRIGHT'),
              ('M_JFULL', '^\x01J', 'C_JFULL'), SEP,
              ('M_SP1', None, 'C_SP1'), ('M_SP15', None, 'C_SP15'), ('M_SP2', None, 'C_SP2'), SEP,
              ('M_INDENTS', None, 'C_INDENTS'), ('M_HANGING', None, 'C_HANGING'), ('M_BULLETS', 'F9', 'C_BULLETS'),
              ('M_NUMBERS', 'F10', 'C_NUMBERS'), SEP,
              ('M_WRAPCOL', None, 'C_WRAPCOL')]
    m_tools = [('M_SPELL', '^K', 'C_SPELL'), ('M_LIVESPELL', None, 'C_LIVESPELL'), ('M_THES', '\x01F7', 'C_THES'),
               ('M_SUMMARY', None, 'C_SUMMARY'), SEP, ('M_UDLOAD', None, 'C_UDLOAD'), ('M_UDNEW', None, 'C_UDNEW'), SEP,
               ('DYN', 'UD', 4, 24, 'C_UD0'), SEP, ('M_PREFS', None, 'C_PREFS')]
    m_view = [('M_VDOC', 'F4', 'C_VDOC'), ('M_SPLIT', '^Y', 'C_SPLIT'), ('M_VCODES', 'F6', 'C_VCODES'), SEP,
              ('M_ZOOMIN', 'F8', 'C_ZOOMIN'), ('M_ZOOMOUT', 'F7', 'C_ZOOMOUT'),
              ('M_ZOOMFIT', '\x01F8', 'C_ZOOMFIT'), ('M_ZOOMPAGE', None, 'C_ZOOMPAGE'), SEP,
              ('M_TOOLBAR', '^T', 'C_TOOLBAR'), ('M_RULERS', '^\x01R', 'C_RULERS'), ('M_GUIDES', None, 'C_GUIDES'), SEP,
              ('M_REPAG', 'F2', 'C_REPAG'), ('M_THEME', None, 'C_THEME')]
    m_window = [('M_TILEV', None, 'C_TILEV'), ('M_TILEH', None, 'C_TILEH'), ('M_CASCADE', None, 'C_CASCADE'), SEP,
                ('M_ICONIFY', '^Z', 'C_ICONIFY'), ('M_ICONALL', None, 'C_ICONALL'), ('M_BOTTOM', None, 'C_BOTTOM'),
                ('M_CYCLE', '^W', 'C_CYCLE'), SEP, ('DYN', 'WIN', 24, 24, 'C_WIN0'), ('M_WINMORE', None, 'C_WINMORE')]
    return [('MT_DESK', m_desk), ('MT_FILE', m_file), ('MT_EDIT', m_edit), ('MT_INSERT', m_insert), ('MT_FONT', m_font),
            ('MT_STYLE', m_style), ('MT_PARA', m_para), ('MT_TOOLS', m_tools), ('MT_VIEW', m_view), ('MT_WINDOW', m_window)]

# TOS 1-4 save the screen under a drop-down menu in a buffer a quarter of the
# screen's size and don't check its size: a bigger menu writes over memory that
# isn't theirs and can crash the machine (EdWord's own Window menu did, at
# about 1.25 times the buffer; a 23 x 22 menu of Lattice C's, at 1.07, didn't).
# Each fixed menu has to stay under MENU_LIMIT of it, in ST high and ST
# medium resolution alike (the Font and Window menus are cut to fit at run
# time, see gem.c's menu_rows_fit).
MENU_LIMIT = 1.05
def menu_fits(L, tid, wchars, rows):
    for sw, sh, hc in ((640, 400, 16), (640, 200, 8)):
        save = ((wchars * 8 + 4 + 15) // 16) * 2 * (rows * hc + 4)
        r = save / (sw * sh / 8 / 4)
        if r > MENU_LIMIT:
            sys.exit('%s menu %s too big for the TOS menu buffer: %d x %d characters, %.2f of it'
                     % (L.lg, tid, wchars, rows, r))

def menubar(L):
    root = Obj(G_IBOX, cp(0), cp(0), cp(80), cp(25), name='ROOT')
    bar = add(root, Obj(G_BOX, cp(0), cp(0), cp(80), cp(1, 2), spec=0x00001100, name='BAR'))
    ms = menus()
    titles = [b' ' + L(t)[:12] + b' ' for t, _ in ms]
    total = sum(len(t) for t in titles)
    if total > 78:          # as gem.c does: one space before each title when two don't fit
        titles = [b' ' + L(t)[:12] for t, _ in ms]
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
        if not any(r is not SEP and r[0] == 'DYN' for r in rows):
            menu_fits(L, tid, maxl, len(rows))
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
    icons = read_icons(os.path.join(here, 'icons.h'))
    names = None
    for lg in LANGS:
        L = Lang(text, lg)
        menu, cmds = menubar(L)
        trees = [menu, dlg_print(L), dlg_prefs(L), dlg_prefs2(L), fontsel(L),
                 dlg_page(L), dlg_wrap(L), dlg_size(L), dlg_headfoot(L), dlg_indents(L), dlg_table(L, True),
                 dlg_table(L, False), dlg_objprops(L, False), dlg_objprops(L, True), dlg_summary(L), dlg_about(L),
                 dlg_textimport(L), dlg_textexport(L), dlg_goto(L), dlg_pagenum(L), dlg_template(L),
                 tw_find(L), tw_spell(L), tw_thes(L), tw_import(L), tw_export(L), gauge(L, False), gauge(L, True),
                 dlg_theme(L)]
        tb16, tbl = toolbar(icons, True)
        tb8, _ = toolbar(icons, False)
        trees += [tb16, tb8,
                  popup('POPSIZE', [b'%d ' % s + L('S_POINT') for s in SIZES] + [L('M_SIZEOTHER')]),
                  popup('POPCOLOUR', [L(s) for s in COLOURS]),
                  popup('POPZOOM', [b'%d%%' % z for z in ZOOMS] + [L('M_ZOOMFIT'), L('M_ZOOMPAGE')]),
                  popup('POPTHEME', THEMES)]
        size = write_rsc(os.path.join(outdir, RSCNAME[lg]), trees)
        # the object numbers: the same in every language
        nm = []
        for t in trees:
            fl = t.flatten()
            nm.append((t.name, [(i, f[0].name) for i, f in enumerate(fl)]))
        if names is None:
            names = nm
            idx = dict((id(f[0]), i) for i, f in enumerate(menu.flatten()))
            first_cmds, first_tb, first_tb16 = [(idx[id(o)], c) for o, c in cmds], tbl, tb16
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
               'PAGE': 'DPG', 'WRAP': 'DWR', 'SIZE': 'DSZ', 'HEADFOOT': 'DHF', 'INDENTS': 'DIN', 'TBLINS': 'DTI',
               'TBLPROP': 'DTP', 'PICPROPS': 'DPP', 'DRAWPROPS': 'DDP', 'SUMMARY': 'DSU', 'ABOUT': 'DAB',
               'TEXTIMP': 'DTXI', 'TEXTEXP': 'DTXE', 'GOTO': 'DGT', 'PAGENUM': 'DPN', 'TEMPLATE': 'DTM',
               'FIND': 'WFD', 'SPELL': 'WSP', 'THES': 'WTH', 'IMPORT': 'WIX', 'EXPORT': 'WEX', 'GAUGE': 'DGA', 'GAUGEC': 'DGC', 'THEME': 'DTH', 'POPTHEME': 'PT',
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
        f.write('#define RSC_NTB %d\n\n' % len(first_tb))
        idx = dict((id(fl[0]), i) for i, fl in enumerate(first_tb16.flatten()))
        for key, what in (('mono', 'the whole icons in black: fewer than 16 colours'),
                          ('colour', 'the coloured layers: 16 colours and more'),
                          ('coloured', 'the rest of those icons, in black: 16 colours and more')):
            f.write('/* toolbar images: %s */\n#define RSC_TB_%s ' % (what, key.upper()))
            f.write(', '.join(str(idx[id(o)]) for o in first_tb16.groups[key]) + '\n')
        f.write('\n#endif\n')

if __name__ == '__main__':
    main()
