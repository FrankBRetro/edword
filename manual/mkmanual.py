#!/usr/bin/env python3
# Copyright (c) 2026 Francis C Bullen. SPDX-License-Identifier: MIT (see LICENSE)
"""mkmanual.py - build the EdWord manual as a PDF from manual/src/<lang>.txt

usage: mkmanual.py LANG OUT.PDF   (run from the EdWord source folder)

The source is plain text with a little markup:
  @key value        title page settings (title, subtitle, intro, shotnote, contents)
  # Chapter         ## Section
  ![shot] caption   a screenshot from manual/shots/LANG/shot.png
  | a | b |         table rows (the first row is the heading)
  - item            list items
  **bold** *italic* `code`, and {ID} for a user interface string from
  lang/strings.txt in this language (a trailing "..." or ":" is dropped).
"""
import sys, os, re
from reportlab.lib.pagesizes import A4
from reportlab.lib.units import cm
from reportlab.lib import colors
from reportlab.lib.styles import ParagraphStyle
from reportlab.pdfbase import pdfmetrics
from reportlab.pdfbase.ttfonts import TTFont
from reportlab.platypus import (BaseDocTemplate, PageTemplate, Frame, Paragraph, Spacer, Image,
                                Table, TableStyle, KeepTogether, PageBreak, ListFlowable, ListItem)
from reportlab.platypus.tableofcontents import TableOfContents

FONTDIR = '/usr/share/fonts/truetype/dejavu'
for name, f in (('Sans', 'DejaVuSans.ttf'), ('Sans-Bold', 'DejaVuSans-Bold.ttf'),
                ('Sans-Oblique', 'DejaVuSans-Oblique.ttf'), ('Sans-BoldOblique', 'DejaVuSans-BoldOblique.ttf'),
                ('Serif', 'DejaVuSerif.ttf'), ('Serif-Bold', 'DejaVuSerif-Bold.ttf'),
                ('Serif-Italic', 'DejaVuSerif-Italic.ttf'), ('Serif-BoldItalic', 'DejaVuSerif-BoldItalic.ttf'),
                ('Mono', 'DejaVuSansMono.ttf')):
    pdfmetrics.registerFont(TTFont(name, os.path.join(FONTDIR, f)))
pdfmetrics.registerFontFamily('Serif', normal='Serif', bold='Serif-Bold', italic='Serif-Italic',
                              boldItalic='Serif-BoldItalic')
pdfmetrics.registerFontFamily('Sans', normal='Sans', bold='Sans-Bold', italic='Sans-Oblique',
                              boldItalic='Sans-BoldOblique')

INK = colors.HexColor('#1a1a1a')
ACCENT = colors.HexColor('#2b4a6f')
RULE = colors.HexColor('#b8c2cc')
SHADE = colors.HexColor('#eef1f4')

body = ParagraphStyle('body', fontName='Serif', fontSize=10, leading=14, textColor=INK, spaceAfter=6)
cell = ParagraphStyle('cell', parent=body, fontSize=9, leading=12, spaceAfter=0)
cellh = ParagraphStyle('cellh', parent=cell, fontName='Sans-Bold', textColor=ACCENT)
h1 = ParagraphStyle('h1', fontName='Sans-Bold', fontSize=18, leading=22, textColor=ACCENT, spaceBefore=4, keepWithNext=1,
                    spaceAfter=10)
h2 = ParagraphStyle('h2', fontName='Sans-Bold', fontSize=12.5, leading=16, textColor=INK, spaceBefore=10, keepWithNext=1,
                    spaceAfter=4)
cap = ParagraphStyle('cap', fontName='Sans-Oblique', fontSize=8.5, leading=11, textColor=colors.HexColor('#555555'),
                     alignment=1, spaceBefore=3, spaceAfter=10)
toc1 = ParagraphStyle('toc1', fontName='Sans-Bold', fontSize=10.5, leading=16, leftIndent=0)
toc2 = ParagraphStyle('toc2', fontName='Sans', fontSize=9.5, leading=13, leftIndent=14)


def load_strings(lang):
    text, cur = {}, None
    for line in open('lang/strings.txt', encoding='utf-8'):
        line = line.rstrip('\n')
        if line.startswith('@'):
            cur = line[1:].strip(); text[cur] = {}
        elif cur and re.match(r'\w\w: ?', line):
            text[cur][line[:2]] = line[4:] if len(line) > 3 else ''
    out = {}
    for k, v in text.items():
        s = v.get(lang, v.get('en', ''))
        s = s.replace('\\n', ' ').strip()
        if s.endswith('...'): s = s[:-3]
        if s.endswith(':'): s = s[:-1]
        out[k] = s
    return out


def inline(s, ui):
    s = re.sub(r'\{([A-Z][A-Z0-9_]+)\}', lambda m: '\x01' + ui.get(m.group(1), m.group(0)) + '\x02', s)
    s = s.replace('&', '&amp;').replace('<', '&lt;').replace('>', '&gt;')
    s = re.sub(r'`([^`]+)`', lambda m: '<font name="Mono" size="8.5">' + m.group(1) + '</font>', s)
    s = re.sub(r'\*\*([^*]+)\*\*', r'<b>\1</b>', s)
    s = re.sub(r'(?<![\w*])\*([^*]+)\*(?![\w*])', r'<i>\1</i>', s)
    s = s.replace('\x01', '<font name="Sans-Bold" size="9">').replace('\x02', '</font>')
    return s


class Doc(BaseDocTemplate):
    def __init__(self, path, meta, **kw):
        BaseDocTemplate.__init__(self, path, pagesize=A4, leftMargin=2.3 * cm, rightMargin=2.3 * cm,
                                 topMargin=2.2 * cm, bottomMargin=2.2 * cm, title=meta['title'] + ' - ' + meta['subtitle'],
                                 author='EdWord', **kw)
        self.meta = meta
        f = Frame(self.leftMargin, self.bottomMargin, self.width, self.height, id='f')
        self.addPageTemplates([PageTemplate('first', [f], onPage=self.cover_deco),
                               PageTemplate('main', [f], onPageEnd=self.deco)])
        self.chapter = ''

    def beforeDocument(self):
        self.chapter = ''

    def cover_deco(self, c, d):
        pass

    def deco(self, c, d):
        c.saveState()
        c.setStrokeColor(RULE); c.setLineWidth(0.5)
        c.line(self.leftMargin, A4[1] - 1.6 * cm, A4[0] - self.rightMargin, A4[1] - 1.6 * cm)
        c.setFont('Sans', 8); c.setFillColor(colors.HexColor('#666666'))
        c.drawString(self.leftMargin, A4[1] - 1.45 * cm, self.meta['title'] + ' - ' + self.meta['subtitle'])
        c.drawRightString(A4[0] - self.rightMargin, A4[1] - 1.45 * cm, self.chapter)
        c.drawCentredString(A4[0] / 2, 1.2 * cm, str(c.getPageNumber()))
        c.restoreState()

    def afterFlowable(self, fl):
        if isinstance(fl, Paragraph) and getattr(fl, 'toc_level', None) is not None:
            text = fl.getPlainText()
            if fl.toc_level == 0: self.chapter = text
            key = 'h%d' % id(fl)
            self.canv.bookmarkPage(key)
            self.canv.addOutlineEntry(text, key, level=fl.toc_level, closed=fl.toc_level > 0)
            self.notify('TOCEntry', (fl.toc_level, text, self.page, key))


def build(lang, out):
    ui = load_strings(lang)
    src = open('manual/src/%s.txt' % lang, encoding='utf-8').read().split('\n')
    shots = 'manual/shots/%s' % lang
    meta = {'title': 'EdWord', 'subtitle': 'Manual', 'intro': '', 'shotnote': '', 'contents': 'Contents'}
    story, para, rows, items = [], [], [], []

    def flush():
        nonlocal para, rows, items
        if para:
            story.append(Paragraph(inline(' '.join(para), ui), body)); para = []
        if items:
            story.append(ListFlowable([ListItem(Paragraph(inline(t, ui), body), leftIndent=12)
                                       for t in items], bulletType='bullet', start='•', leftIndent=12,
                                      bulletFontName='Sans', bulletFontSize=8))
            items = []
        if rows:
            data = [[Paragraph(inline(c, ui), cellh if i == 0 else cell) for c in r] for i, r in enumerate(rows)]
            n = len(rows[0])
            widths = [5.6 * cm, 10.8 * cm] if n == 2 else None
            t = Table(data, colWidths=widths, repeatRows=1, hAlign='LEFT')
            t.setStyle(TableStyle([('LINEBELOW', (0, 0), (-1, 0), 0.8, ACCENT),
                                   ('LINEBELOW', (0, 1), (-1, -1), 0.3, RULE),
                                   ('BACKGROUND', (0, 0), (-1, 0), SHADE),
                                   ('VALIGN', (0, 0), (-1, -1), 'TOP'),
                                   ('TOPPADDING', (0, 0), (-1, -1), 3), ('BOTTOMPADDING', (0, 0), (-1, -1), 3)]))
            story.append(t); story.append(Spacer(1, 8)); rows = []

    body_started = False
    for line in src:
        if line.startswith('@'):
            k, _, v = line[1:].partition(' ')
            meta[k] = v.strip()
            continue
        if not body_started and line.startswith('#'):
            body_started = True
            cover(story, meta, ui)
        if not line.strip():
            flush(); continue
        if line.startswith('## '):
            flush()
            p = Paragraph(inline(line[3:], ui), h2); p.toc_level = 1
            story.append(p); continue
        if line.startswith('# '):
            flush()
            if len(story) > 4: story.append(PageBreak())
            p = Paragraph(inline(line[2:], ui), h1); p.toc_level = 0
            story.append(p); continue
        m = re.match(r'!\[(\w+)\]\s*(.*)$', line)
        if m:
            flush()
            path = os.path.join(shots, m.group(1) + '.png')
            if not os.path.exists(path):
                sys.exit('missing screenshot ' + path)
            img = Image(path, width=13 * cm, height=13 * cm * 400 / 640)
            frame = Table([[img]], colWidths=[13.2 * cm])
            frame.setStyle(TableStyle([('BOX', (0, 0), (-1, -1), 0.6, RULE),
                                       ('LEFTPADDING', (0, 0), (-1, -1), 1), ('RIGHTPADDING', (0, 0), (-1, -1), 1),
                                       ('TOPPADDING', (0, 0), (-1, -1), 1), ('BOTTOMPADDING', (0, 0), (-1, -1), 1)]))
            story.append(KeepTogether([Spacer(1, 4), frame, Paragraph(inline(m.group(2), ui), cap)]))
            continue
        if line.startswith('|'):
            if para or items: flush()
            rows.append([c.strip() for c in line.strip().strip('|').split('|')])
            continue
        if line.startswith('- '):
            if para or rows: flush()
            items.append(line[2:]); continue
        if rows or items: flush()
        para.append(line.strip())
    flush()

    doc = Doc(out, meta)
    doc.multiBuild(story)


def cover(story, meta, ui):
    from reportlab.platypus import NextPageTemplate
    story.append(Spacer(1, 5 * cm))
    story.append(Paragraph(meta['title'], ParagraphStyle('t', fontName='Sans-Bold', fontSize=40, leading=46,
                                                         textColor=ACCENT)))
    story.append(Paragraph(meta['subtitle'], ParagraphStyle('st', fontName='Sans', fontSize=20, leading=26,
                                                            textColor=INK, spaceBefore=6)))
    story.append(Spacer(1, 1.2 * cm))
    story.append(Paragraph(inline(meta['intro'], ui), ParagraphStyle('in', parent=body, fontSize=12, leading=17)))
    story.append(Spacer(1, 6 * cm))
    story.append(Paragraph(inline(meta['shotnote'], ui), ParagraphStyle('sn', parent=body, fontSize=8.5, leading=12,
                                                                        textColor=colors.HexColor('#555555'))))
    story.append(NextPageTemplate('main'))
    story.append(PageBreak())
    story.append(Paragraph(meta['contents'], h1))
    toc = TableOfContents()
    toc.levelStyles = [toc1, toc2]
    toc.dotsMinLevel = 0
    story.append(toc)
    story.append(PageBreak())


if __name__ == '__main__':
    build(sys.argv[1], sys.argv[2])
