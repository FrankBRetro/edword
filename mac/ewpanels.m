/* Copyright (c) 2026 Francis C Bullen. SPDX-License-Identifier: MIT (see LICENSE) */
/*
 * ewpanels.m - the tool panels: Find and Replace, Spelling, Thesaurus,
 * the spelling popup, and the drawing editor. They stay open while you
 * work and act on the front document.
 */
#import "ewmac.h"

NSString *support_dir(void)
{
    NSString *d = [NSSearchPathForDirectoriesInDomains(NSApplicationSupportDirectory, NSUserDomainMask, YES)
                       .firstObject stringByAppendingPathComponent:@"EdWord"];
    [[NSFileManager defaultManager] createDirectoryAtPath:d withIntermediateDirectories:YES attributes:nil error:NULL];
    return d;
}

int spell_ensure(int quiet)
{
    if (!spell_main_loaded()) {
        NSString *name = [[NSString stringWithUTF8String:spell_dic_name(spell_lang)] stringByDeletingPathExtension];
        NSString *p = [[NSBundle mainBundle] pathForResource:name ofType:@"DIC"];
        if (!p || !spell_load_main(p.fileSystemRepresentation)) {
            if (!quiet) ew_alert(AL_NO_DIC);
            if (!spell_nuser) return 0;
        }
    }
    if (spell_nuser == 0)
        spell_user_load([support_dir() stringByAppendingPathComponent:@"USER.UDC"].fileSystemRepresentation, 1);
    return 1;
}

static void word_of(DOC *d, long a, long b, char *w)
{
    long i;
    for (i = a; i < b && i - a < WORDMAX - 1; i++) w[i - a] = (char)d->text[i].ch;
    w[i - a] = 0;
}

/* ================================================================= */
/*  Find and replace                                                  */
/* ================================================================= */

static NSPanel *findPanel;
static NSTextField *fWhat, *fWith, *fStatus;
static NSButton *fCase, *fWord, *fBack;

static int f_flags(void)
{
    return (fCase.state ? FIND_CASE : 0) | (fWord.state ? FIND_WORD : 0) | (fBack.state ? FIND_BACK : 0);
}

static int f_find(EWDocument *doc)
{
    DOC *d = [doc doc];
    char what[64];
    long from, e, s, a, b;
    int flags = f_flags();
    if (!ns2st(fWhat.stringValue, what, sizeof what)) return 0;
    if (doc_sel(d, &a, &b)) from = flags & FIND_BACK ? a : b;
    else from = d->cursor;
    s = find_next(d, from, what, flags, &e);
    if (s < 0) s = find_next(d, flags & FIND_BACK ? d->len : 0, what, flags, &e);
    if (s < 0) { fStatus.stringValue = LS(S_NOT_FOUND); NSBeep(); return 0; }
    [doc selectFrom:s to:e];
    fStatus.stringValue = @"";
    return 1;
}

static void f_tap(NSInteger i)
{
    EWDocument *doc = ew_active_doc();
    DOC *d = doc ? [doc doc] : NULL;
    char what[64], with[64];
    long a, b, e;
    if (i == 3) { [findPanel orderOut:nil]; return; }
    if (!d) return;
    if (d->cv.dirty) [doc applyCodes];
    ns2st(fWhat.stringValue, what, sizeof what);
    ns2st(fWith.stringValue, with, sizeof with);
    if (i == 0) f_find(doc);
    else if (i == 1) {
        if (doc_sel(d, &a, &b) && find_next(d, a, what, f_flags() & ~FIND_BACK, &e) == a && e == b) {
            doc_replace(d, a, b, with);
            d->cursor = a + (long)strlen(with);
            d->anchor = -1;
            [doc edited:a typing:NO];
        }
        f_find(doc);
    } else if (i == 2 && what[0]) {
        int n = find_replace_all(d, what, with, f_flags(), 0);
        char msg[64];
        if (n) [doc changed];
        snprintf(msg, sizeof msg, L(S_N_REPLACED), (long)n);
        fStatus.stringValue = [NSString stringWithUTF8String:msg];
    }
}

void find_open(EWDocument *doc)
{
    DOC *d = [doc doc];
    long a, b;
    if (!findPanel) {
        EWForm *f = [EWForm formWithTitle:LS(S_FIND_TITLE)];
        [f row:LS(S_FIND_WHAT)];    fWhat = [f field:@"" width:36];
        [f row:LS(S_REPLACE_WITH)]; fWith = [f field:@"" width:36];
        [f row:@""];
        fCase = [f check:LS(S_MATCH_CASE) on:NO];
        fWord = [f check:LS(S_WHOLE_WORDS) on:NO];
        fBack = [f check:LS(S_BACKWARDS) on:NO];
        [f row:@""];
        fStatus = [f text:@"                                        "];
        findPanel = [f panelWithButtons:@[LS(S_FIND_NEXT), LS(S_REPLACE), LS(S_REPLACE_ALL), LS(S_CLOSE)]
                                 tapped:^(NSInteger i) { f_tap(i); }];
        [findPanel center];
    }
    if (doc_sel(d, &a, &b) && b - a < 40) fWhat.stringValue = cells2ns(d, a, b);
    fStatus.stringValue = @"";
    [findPanel makeKeyAndOrderFront:nil];
    [findPanel makeFirstResponder:fWhat];
}

void find_again(EWDocument *doc)
{
    if (!fWhat.stringValue.length) { find_open(doc); return; }
    f_find(doc);
}

/* ================================================================= */
/*  Spelling                                                          */
/* ================================================================= */

static NSPanel *spellPanel;
static NSTextField *sHead, *sChange;
static NSPopUpButton *sSug;
static __weak EWDocument *sDoc;
static long sS, sE, sFrom, sStart;
static int sWrapped;
static char sWord[WORDMAX];

static void sp_close(void) { [spellPanel orderOut:nil]; sDoc = nil; }

static void sp_find(void)
{
    EWDocument *doc = sDoc;
    DOC *d;
    long s, e;
    char sug[8][WORDMAX];
    int n, i;
    if (!doc) { sp_close(); return; }
    d = [doc doc];
    for (;;) {
        s = spell_next(d, sFrom, &e, sWord);
        if (sWrapped && (s < 0 || s >= sStart)) s = -1;
        else if (s < 0 && !sWrapped && sStart > 0) {
            NSAlert *a = [[NSAlert alloc] init];
            a.messageText = [LS(AL_SPELL_WRAP) stringByReplacingOccurrencesOfString:@"|" withString:@" "];
            [a addButtonWithTitle:[LS(AB_CONT_STOP) componentsSeparatedByString:@"|"][0]];
            [a addButtonWithTitle:[LS(AB_CONT_STOP) componentsSeparatedByString:@"|"].lastObject];
            if ([a runModal] == NSAlertFirstButtonReturn) { sWrapped = 1; sFrom = 0; continue; }
        }
        break;
    }
    if (s < 0) { sp_close(); ew_alert(AL_SPELL_DONE); return; }
    sS = s; sE = e;
    [doc selectFrom:s to:e];
    n = spell_suggest(sWord, sug, 8);
    sHead.stringValue = [NSString stringWithFormat:@"%@  %@", LS(S_NOT_IN_DIC), st2ns((unsigned char *)sWord, (long)strlen(sWord))];
    [sHead sizeToFit];
    [sSug removeAllItems];
    for (i = 0; i < n; i++) [sSug addItemWithTitle:st2ns((unsigned char *)sug[i], (long)strlen(sug[i]))];
    if (!n) [sSug addItemWithTitle:LS(S_NO_SUGGESTIONS)];
    sSug.enabled = n > 0;
    sChange.stringValue = n ? [sSug itemTitleAtIndex:0] : st2ns((unsigned char *)sWord, (long)strlen(sWord));
}

static int sp_still_there(DOC *d)
{
    long n = (long)strlen(sWord), i;
    if (sE > d->len || sE - sS != n) return 0;
    for (i = 0; i < n; i++) if (d->text[sS + i].ch != (unsigned char)sWord[i]) return 0;
    return 1;
}

static void sp_tap(NSInteger i)
{
    EWDocument *doc = sDoc;
    DOC *d;
    char with[WORDMAX];
    if (i == 4 || !doc) { sp_close(); return; }
    d = [doc doc];
    if (!sp_still_there(d)) { sFrom = sS <= d->len ? sS : d->len; sp_find(); return; }
    if (i == 0) {
        ns2st(sChange.stringValue, with, sizeof with);
        if (strcmp(with, sWord)) {
            long delta = (long)strlen(with) - (sE - sS);
            doc_replace(d, sS, sE, with);
            d->cursor = sS + (long)strlen(with);
            d->anchor = -1;
            [doc edited:sS typing:NO];
            if (sS < sStart) sStart += delta;
            sE += delta;
        }
    } else if (i == 2) { spell_ignore(sWord); [doc.page setNeedsDisplay:YES]; }
    else if (i == 3) { spell_user_add(sWord); [doc.page setNeedsDisplay:YES]; }
    sFrom = sE;
    sp_find();
}

@interface EWSugTarget : NSObject
- (void)picked:(NSPopUpButton *)p;
@end
@implementation EWSugTarget
- (void)picked:(NSPopUpButton *)p { sChange.stringValue = p.titleOfSelectedItem; }
@end
static EWSugTarget *sugTarget;

void spell_run(EWDocument *doc)
{
    DOC *d = [doc doc];
    long a, b;
    if (!spell_ensure(0)) return;
    if (d->cv.dirty) [doc applyCodes];
    if (!spellPanel) {
        EWForm *f = [EWForm formWithTitle:LS(S_SPELL_TITLE)];
        [f row:nil];
        sHead = [f text:@"                                                  "];
        [f row:LS(S_CHANGE_TO)];   sChange = [f field:@"" width:30];
        [f row:LS(S_SUGGESTIONS)]; sSug = [f popup:@[@"                              "] selected:0];
        sugTarget = [[EWSugTarget alloc] init];
        sSug.target = sugTarget;
        sSug.action = @selector(picked:);
        spellPanel = [f panelWithButtons:@[LS(S_REPLACE), LS(S_IGNORE), LS(S_IGNORE_ALL), LS(S_ADD), LS(S_STOP)]
                                  tapped:^(NSInteger i) { sp_tap(i); }];
        [spellPanel center];
    }
    sDoc = doc;
    sWrapped = 0;
    sStart = sFrom = doc_sel(d, &a, &b) ? a : d->cursor;
    [spellPanel orderFront:nil];
    sp_find();
}

/* ---- the spelling popup --------------------------------------------------------- */

@interface EWLiveTarget : NSObject
@property (weak) EWDocument *doc;
@property long a, b;
- (void)pick:(NSMenuItem *)m;
@end
@implementation EWLiveTarget
- (void)pick:(NSMenuItem *)m
{
    EWDocument *doc = self.doc;
    DOC *d = [doc doc];
    char w[WORDMAX];
    if (!doc) return;
    if (m.tag == 1) { word_of(d, self.a, self.b, w); spell_ignore(w); [doc.page setNeedsDisplay:YES]; return; }
    if (m.tag == 2) { word_of(d, self.a, self.b, w); spell_user_add(w); [doc.page setNeedsDisplay:YES]; return; }
    ns2st(m.title, w, sizeof w);
    doc_replace(d, self.a, self.b, w);
    d->cursor = self.a + (long)strlen(w);
    d->anchor = -1;
    [doc edited:self.a typing:NO];
}
@end
static EWLiveTarget *liveTarget;

void live_popup(EWDocument *doc, long a, long b, NSEvent *ev, NSView *view)
{
    DOC *d = [doc doc];
    char w[WORDMAX], sug[6][WORDMAX];
    NSMenu *m = [[NSMenu alloc] init];
    NSMenuItem *it;
    int n, i;
    if (b - a >= WORDMAX) return;
    spell_ensure(1);
    word_of(d, a, b, w);
    n = spell_suggest(w, sug, 6);
    if (!liveTarget) liveTarget = [[EWLiveTarget alloc] init];
    liveTarget.doc = doc; liveTarget.a = a; liveTarget.b = b;
    for (i = 0; i < n; i++) {
        it = [m addItemWithTitle:st2ns((unsigned char *)sug[i], (long)strlen(sug[i])) action:@selector(pick:) keyEquivalent:@""];
        it.target = liveTarget;
    }
    if (!n) { it = [m addItemWithTitle:LS(S_NO_SUGGESTIONS) action:NULL keyEquivalent:@""]; it.enabled = NO; }
    [m addItem:[NSMenuItem separatorItem]];
    it = [m addItemWithTitle:LS(S_IGNORE_ALL) action:@selector(pick:) keyEquivalent:@""];
    it.target = liveTarget; it.tag = 1;
    it = [m addItemWithTitle:LS(S_ADD_TO_DIC) action:@selector(pick:) keyEquivalent:@""];
    it.target = liveTarget; it.tag = 2;
    [NSMenu popUpContextMenu:m withEvent:ev forView:view];
}

/* ================================================================= */
/*  Thesaurus                                                         */
/* ================================================================= */

static NSPanel *thesPanel;
static NSTextField *tWord, *tWith, *tMsg;
static NSPopUpButton *tMean, *tSyn;
static __weak EWDocument *tDoc;
static long tA, tB;
static THSRES tRes;

static void th_sense(int m)
{
    int i;
    [tSyn removeAllItems];
    if (m < tRes.nsense)
        for (i = 0; i < tRes.nsyn[m]; i++)
            [tSyn addItemWithTitle:st2ns((unsigned char *)tRes.syn[m][i], (long)strlen(tRes.syn[m][i]))];
    tSyn.enabled = tSyn.numberOfItems > 0;
    if (tSyn.numberOfItems) tWith.stringValue = [tSyn itemTitleAtIndex:0];
}

static void th_lookup(void)
{
    static const int pos[4] = { S_POS_N, S_POS_V, S_POS_A, S_POS_R };
    char w[WORDMAX], msg[100];
    int i;
    if (!thes_ready()) {
        NSString *p = [[NSBundle mainBundle] pathForResource:@"EDWORD" ofType:@"THS"];
        if (!p || !thes_open(p.fileSystemRepresentation)) { ew_alert(AL_NO_THS); return; }
    }
    ns2st(tWord.stringValue, w, sizeof w);
    thes_lookup(w, &tRes);
    [tMean removeAllItems];
    for (i = 0; i < tRes.nsense; i++) {
        int k = tRes.pos[i] == 'v' ? 1 : tRes.pos[i] == 'a' ? 2 : tRes.pos[i] == 'r' ? 3 : 0;
        [tMean addItemWithTitle:[NSString stringWithFormat:@"%d. %@ %@", i + 1, LS(pos[k]),
                                 st2ns((unsigned char *)tRes.gloss[i], (long)strlen(tRes.gloss[i]))]];
    }
    tMean.enabled = tRes.nsense > 0;
    if (tRes.nsense) {
        if (strcmp(tRes.word, w)) { snprintf(msg, sizeof msg, L(S_TH_BASE), tRes.word); tMsg.stringValue = st2ns((unsigned char *)msg, (long)strlen(msg)); }
        else tMsg.stringValue = @"";
    } else {
        snprintf(msg, sizeof msg, L(S_TH_NONE), w);
        tMsg.stringValue = [NSString stringWithUTF8String:msg] ? [NSString stringWithUTF8String:msg] : @"";
        tWith.stringValue = @"";
    }
    th_sense(0);
}

@interface EWThesTarget : NSObject
- (void)meaning:(NSPopUpButton *)p;
- (void)synonym:(NSPopUpButton *)p;
@end
@implementation EWThesTarget
- (void)meaning:(NSPopUpButton *)p { th_sense((int)p.indexOfSelectedItem); }
- (void)synonym:(NSPopUpButton *)p { tWith.stringValue = p.titleOfSelectedItem; }
@end
static EWThesTarget *thTarget;

static void th_tap(NSInteger i)
{
    EWDocument *doc = tDoc;
    DOC *d;
    char w[WORDMAX], orig[WORDMAX];
    if (i == 1) { [thesPanel orderOut:nil]; return; }
    if (!doc) return;
    d = [doc doc];
    ns2st(tWith.stringValue, w, sizeof w);
    if (!w[0] || tB <= tA || tB > d->len) return;
    word_of(d, tA, tB, orig);
    if (orig[0] && st_upper((unsigned char)orig[0]) == (unsigned char)orig[0] && st_isalpha((unsigned char)orig[0]))
        w[0] = (char)st_upper((unsigned char)w[0]);
    doc_replace(d, tA, tB, w);
    d->cursor = tA + (long)strlen(w);
    d->anchor = tA;
    tB = d->cursor;
    [doc edited:tA typing:NO];
    [thesPanel orderOut:nil];
}

void thes_run(EWDocument *doc)
{
    DOC *d = [doc doc];
    long a, b, pos;
    char w[WORDMAX];
    if (d->cv.dirty) [doc applyCodes];
    if (!thesPanel) {
        EWForm *f = [EWForm formWithTitle:LS(S_THES_TITLE)];
        thTarget = [[EWThesTarget alloc] init];
        [f row:LS(S_TH_WORD)];
        tWord = [f field:@"" width:24];
        [f button:LS(S_LOOK_UP) action:^{ th_lookup(); }];
        [f row:LS(S_MEANINGS)];
        tMean = [f popup:@[@"                                             "] selected:0];
        tMean.target = thTarget; tMean.action = @selector(meaning:);
        [f row:LS(S_SYNONYMS)];
        tSyn = [f popup:@[@"                              "] selected:0];
        tSyn.target = thTarget; tSyn.action = @selector(synonym:);
        [f row:LS(S_REPLACE_WITH)];
        tWith = [f field:@"" width:24];
        [f row:@""];
        tMsg = [f text:@"                                        "];
        thesPanel = [f panelWithButtons:@[LS(S_REPLACE), LS(S_CLOSE)] tapped:^(NSInteger i) { th_tap(i); }];
        [thesPanel center];
    }
    pos = doc_sel(d, &a, &b) ? a : d->cursor;
    if (!word_at(d, pos, &a, &b, w, WORDMAX) && !(pos > 0 && word_at(d, pos - 1, &a, &b, w, WORDMAX))) {
        a = b = pos;
        w[0] = 0;
    }
    tDoc = doc; tA = a; tB = b;
    if (b > a) [doc selectFrom:a to:b];
    tWord.stringValue = st2ns((unsigned char *)w, (long)strlen(w));
    tWith.stringValue = @"";
    tMsg.stringValue = @"";
    [thesPanel makeKeyAndOrderFront:nil];
    if (w[0]) th_lookup();
}

/* ================================================================= */
/*  The drawing editor                                                */
/* ================================================================= */

enum { T_SELECT, T_RECT, T_RRECT, T_ELLIPSE, T_LINE, T_TRI };

@interface EWCanvas : NSView
@end

static NSPanel *drawPanel;
static EWCanvas *canvas;
static NSSegmentedControl *dTools;
static NSPopUpButton *dFill, *dWidth, *dColour;
static __weak EWDocument *dDoc;
static int dObj = -1, dSel = -1, dFillV = FL_NONE, dLw = 1, dCol = 0;

static OBJ *dobj(void)
{
    EWDocument *doc = dDoc;
    DOC *d = doc ? [doc doc] : NULL;
    if (!d || dObj < 0 || dObj >= d->a.nobj || d->a.objs[dObj].type != OB_DRAW) return NULL;
    return &d->a.objs[dObj];
}

static SHAPE *dshape(void)
{
    OBJ *o = dobj();
    return o && dSel >= 0 && dSel < o->nsh ? &o->sh[dSel] : NULL;
}

static void d_changed(void)
{
    EWDocument *doc = dDoc;
    if (!doc) return;
    [doc doc]->dirty = 1;
    [doc doc]->selobj = dObj;
    [doc changed];
    [canvas setNeedsDisplay:YES];
}

static void d_sync(void)
{
    SHAPE *s = dshape();
    [dFill selectItemAtIndex:s ? s->fill : dFillV];
    [dWidth selectItemAtIndex:s ? s->lw : dLw];
    [dColour selectItemAtIndex:s ? s->color : dCol];
    [canvas setNeedsDisplay:YES];
}

@implementation EWCanvas
- (BOOL)isFlipped { return YES; }
- (BOOL)acceptsFirstResponder { return YES; }

/* the drawing box on the canvas: points to view */
- (double)scaleFor:(OBJ *)o
{
    double sx = (self.bounds.size.width - 24) / MAX(1, o->w), sy = (self.bounds.size.height - 24) / MAX(1, o->h);
    return MIN(MIN(sx, sy), 8);
}
- (NSPoint)origin:(OBJ *)o
{
    double s = [self scaleFor:o];
    return NSMakePoint((self.bounds.size.width - o->w * s) / 2, (self.bounds.size.height - o->h * s) / 2);
}
- (NSPoint)toPt:(NSPoint)p obj:(OBJ *)o
{
    double s = [self scaleFor:o];
    NSPoint org = [self origin:o];
    return NSMakePoint(MAX(0, MIN(o->w, (p.x - org.x) / s)), MAX(0, MIN(o->h, (p.y - org.y) / s)));
}

- (void)drawRect:(NSRect)r
{
    OBJ *o = dobj();
    double s, u = units_per_pt();
    NSPoint org;
    NSAffineTransform *t;
    int k;
    SHAPE *sel = dshape();
    (void)r;
    [[NSColor colorWithCalibratedWhite:0.8 alpha:1] set];
    NSRectFill(self.bounds);
    if (!o) return;
    s = [self scaleFor:o];
    org = [self origin:o];
    [[NSColor whiteColor] set];
    NSRectFill(NSMakeRect(org.x, org.y, o->w * s, o->h * s));
    [[NSColor grayColor] set];
    NSFrameRect(NSInsetRect(NSMakeRect(org.x, org.y, o->w * s, o->h * s), -1, -1));
    [NSGraphicsContext saveGraphicsState];
    NSRectClip(NSMakeRect(org.x, org.y, o->w * s, o->h * s));
    t = [NSAffineTransform transform];
    [t translateXBy:org.x yBy:org.y];
    [t scaleBy:s / u];
    [t concat];
    for (k = 0; k < o->nsh; k++) ew_draw_shape(&o->sh[k], 0, 0);
    [NSGraphicsContext restoreGraphicsState];
    if (sel) {
        NSRect b = NSMakeRect(org.x + MIN(sel->x1, sel->x2) * s - 3, org.y + MIN(sel->y1, sel->y2) * s - 3,
                              abs(sel->x2 - sel->x1) * s + 6, abs(sel->y2 - sel->y1) * s + 6);
        NSBezierPath *f = [NSBezierPath bezierPathWithRect:b];
        CGFloat dash[2] = { 3, 3 };
        [f setLineDash:dash count:2 phase:0];
        [[NSColor keyboardFocusIndicatorColor] set];
        [f stroke];
        NSRectFill(NSMakeRect(NSMaxX(b) - 4, NSMaxY(b) - 4, 8, 8));
    }
}

- (void)mouseDown:(NSEvent *)e
{
    OBJ *o = dobj();
    NSPoint p = [self convertPoint:e.locationInWindow fromView:nil], a, q;
    double s;
    int tool = (int)dTools.selectedSegment, k;
    SHAPE *sel = dshape(), orig;
    if (!o) return;
    s = [self scaleFor:o];
    a = [self toPt:p obj:o];
    if (tool == T_SELECT) {
        NSPoint org = [self origin:o];
        int resize = 0;
        if (sel) {
            double hx = org.x + MAX(sel->x1, sel->x2) * s + 3, hy = org.y + MAX(sel->y1, sel->y2) * s + 3;
            resize = fabs(p.x - hx) < 7 && fabs(p.y - hy) < 7;
        }
        if (!resize) {
            dSel = -1;
            for (k = o->nsh - 1; k >= 0; k--) {
                SHAPE *h = &o->sh[k];
                if (a.x >= MIN(h->x1, h->x2) - 3 && a.x <= MAX(h->x1, h->x2) + 3 &&
                    a.y >= MIN(h->y1, h->y2) - 3 && a.y <= MAX(h->y1, h->y2) + 3) { dSel = k; break; }
            }
            d_sync();
        }
        sel = dshape();
        if (!sel) return;
        orig = *sel;
        for (;;) {
            NSEvent *m = [self.window nextEventMatchingMask:NSEventMaskLeftMouseDragged | NSEventMaskLeftMouseUp];
            short dx, dy;
            if (m.type == NSEventTypeLeftMouseUp) break;
            q = [self toPt:[self convertPoint:m.locationInWindow fromView:nil] obj:o];
            dx = (short)(q.x - a.x); dy = (short)(q.y - a.y);
            *sel = orig;
            if (resize) {
                if (sel->x2 >= sel->x1) sel->x2 += dx; else sel->x1 += dx;
                if (sel->y2 >= sel->y1) sel->y2 += dy; else sel->y1 += dy;
            } else {
                short lx = MIN(orig.x1, orig.x2), hx = MAX(orig.x1, orig.x2), ly = MIN(orig.y1, orig.y2), hy = MAX(orig.y1, orig.y2);
                if (lx + dx < 0) dx = (short)-lx;
                if (hx + dx > o->w) dx = (short)(o->w - hx);
                if (ly + dy < 0) dy = (short)-ly;
                if (hy + dy > o->h) dy = (short)(o->h - hy);
                sel->x1 += dx; sel->x2 += dx; sel->y1 += dy; sel->y2 += dy;
            }
            [self setNeedsDisplay:YES];
        }
        d_changed();
        return;
    }
    /* draw a new shape */
    if (o->nsh >= MAX_SHAPES) { ew_alert(AL_TOO_MANY_SHAPES); return; }
    {
        static const unsigned char types[6] = { 0, SH_RECT, SH_RRECT, SH_ELLIPSE, SH_LINE, SH_TRI };
        SHAPE *n = &o->sh[o->nsh];
        memset(n, 0, sizeof *n);
        n->type = types[tool];
        n->fill = (unsigned char)(n->type == SH_LINE ? FL_NONE : dFillV);
        n->lw = (unsigned char)(n->type == SH_LINE && !dLw ? 1 : dLw);
        if (!n->lw && n->fill == FL_NONE) n->lw = 1;
        n->color = (unsigned char)dCol;
        n->x1 = n->x2 = (short)a.x; n->y1 = n->y2 = (short)a.y;
        dSel = o->nsh++;
        for (;;) {
            NSEvent *m = [self.window nextEventMatchingMask:NSEventMaskLeftMouseDragged | NSEventMaskLeftMouseUp];
            if (m.type == NSEventTypeLeftMouseUp) break;
            q = [self toPt:[self convertPoint:m.locationInWindow fromView:nil] obj:o];
            n->x2 = (short)q.x; n->y2 = (short)q.y;
            [self setNeedsDisplay:YES];
        }
        if (n->x1 == n->x2 && n->y1 == n->y2) { o->nsh--; dSel = -1; [self setNeedsDisplay:YES]; return; }
    }
    d_changed();
}

- (void)keyDown:(NSEvent *)e
{
    OBJ *o = dobj();
    SHAPE *s = dshape();
    unichar k = e.charactersIgnoringModifiers.length ? [e.charactersIgnoringModifiers characterAtIndex:0] : 0;
    short step = (e.modifierFlags & NSEventModifierFlagShift) ? 1 : 4, dx = 0, dy = 0;
    if (!o) return;
    switch (k) {
    case '\t': if (o->nsh) { dSel = (dSel + 1) % o->nsh; dTools.selectedSegment = T_SELECT; d_sync(); } return;
    case NSDeleteCharacter: case NSDeleteFunctionKey:
        if (s) { memmove(s, s + 1, (o->nsh - dSel - 1) * sizeof(SHAPE)); o->nsh--; dSel = -1; d_changed(); }
        return;
    case 27: [drawPanel orderOut:nil]; return;
    case NSLeftArrowFunctionKey: dx = (short)-step; break;
    case NSRightArrowFunctionKey: dx = step; break;
    case NSUpArrowFunctionKey: dy = (short)-step; break;
    case NSDownArrowFunctionKey: dy = step; break;
    default: [super keyDown:e]; return;
    }
    if (!s) return;
    s->x1 += dx; s->x2 += dx; s->y1 += dy; s->y2 += dy;
    d_changed();
}
@end

@interface EWDrawTarget : NSObject
- (void)fill:(NSPopUpButton *)p;
- (void)width:(NSPopUpButton *)p;
- (void)colour:(NSPopUpButton *)p;
- (void)tool:(NSSegmentedControl *)s;
@end
@implementation EWDrawTarget
- (void)fill:(NSPopUpButton *)p
{
    SHAPE *s = dshape();
    dFillV = (int)p.indexOfSelectedItem;
    if (s && s->type != SH_LINE) { s->fill = (unsigned char)dFillV; d_changed(); }
}
- (void)width:(NSPopUpButton *)p
{
    SHAPE *s = dshape();
    dLw = (int)p.indexOfSelectedItem;
    if (s) { s->lw = (unsigned char)dLw; d_changed(); }
}
- (void)colour:(NSPopUpButton *)p
{
    SHAPE *s = dshape();
    dCol = (int)p.indexOfSelectedItem;
    if (s) { s->color = (unsigned char)dCol; d_changed(); }
}
- (void)tool:(NSSegmentedControl *)s { if (s.selectedSegment != T_SELECT) { dSel = -1; [canvas setNeedsDisplay:YES]; } }
@end
static EWDrawTarget *drawTarget;

static void d_tap(NSInteger i)
{
    OBJ *o = dobj();
    SHAPE *s = dshape();
    if (i == 0 || !o) { [drawPanel orderOut:nil]; return; }
    if (!s) return;
    if (i == 1) {                                   /* delete */
        memmove(s, s + 1, (o->nsh - dSel - 1) * sizeof(SHAPE));
        o->nsh--;
        dSel = -1;
    } else if (i == 2 && dSel < o->nsh - 1) {       /* to front */
        SHAPE t = *s;
        memmove(s, s + 1, (o->nsh - dSel - 1) * sizeof(SHAPE));
        o->sh[o->nsh - 1] = t;
        dSel = o->nsh - 1;
    }
    d_changed();
}

void draw_edit(EWDocument *doc, int obj)
{
    DOC *d = [doc doc];
    if (obj < 0 || obj >= d->a.nobj || d->a.objs[obj].type != OB_DRAW) return;
    if (!drawPanel) {
        EWForm *f = [EWForm formWithTitle:LS(S_DRAW_TITLE)];
        NSMutableArray *cols = [NSMutableArray array];
        static const int tl[6] = { S_DT_SELECT, S_DT_RECT, S_DT_RRECT, S_DT_ELLIPSE, S_DT_LINE, S_DT_TRI };
        int i;
        drawTarget = [[EWDrawTarget alloc] init];
        dTools = [[NSSegmentedControl alloc] initWithFrame:NSMakeRect(0, 0, 10, 24)];
        dTools.segmentCount = 6;
        for (i = 0; i < 6; i++) [dTools setLabel:LS(tl[i]) forSegment:i];
        dTools.selectedSegment = T_RECT;
        dTools.target = drawTarget;
        dTools.action = @selector(tool:);
        [dTools sizeToFit];
        [f row:nil];
        [f addView:dTools];
        [f row:nil];
        [f text:LS(S_DT_FILL)];
        dFill = [f popup:@[LS(S_FILL_NONE), LS(S_FILL_WHITE), LS(S_FILL_GREY), LS(S_FILL_SOLID)] selected:dFillV];
        dFill.target = drawTarget; dFill.action = @selector(fill:);
        [f text:LS(S_DT_WIDTH)];
        dWidth = [f popup:@[@"0", @"1", @"2", @"3", @"4", @"5"] selected:dLw];
        dWidth.target = drawTarget; dWidth.action = @selector(width:);
        for (i = 0; i < NCOLORS; i++) [cols addObject:LS(S_COL_BLACK + i)];
        [f text:LSX(S_DT_COLOUR)];
        dColour = [f popup:cols selected:dCol];
        dColour.target = drawTarget; dColour.action = @selector(colour:);
        canvas = [[EWCanvas alloc] initWithFrame:NSMakeRect(0, 0, 520, 340)];
        [f row:nil];
        [f addView:canvas];
        drawPanel = [f panelWithButtons:@[LS(S_DT_DONE), LS(S_DELETE), LS(S_DT_FRONT)]
                                 tapped:^(NSInteger i) { d_tap(i); }];
        drawPanel.hidesOnDeactivate = YES;
        [drawPanel center];
    }
    dDoc = doc;
    dObj = obj;
    dSel = -1;
    drawPanel.title = [NSString stringWithFormat:@"%@ - %@", LS(S_DRAW_TITLE), doc.displayName];
    d_sync();
    [drawPanel makeKeyAndOrderFront:nil];
    [drawPanel makeFirstResponder:canvas];
}

void panels_doc_closed(EWDocument *doc)
{
    if (dDoc == doc) { [drawPanel orderOut:nil]; dDoc = nil; }
    if (sDoc == doc) sp_close();
    if (tDoc == doc) { [thesPanel orderOut:nil]; tDoc = nil; }
    [[NSOperationQueue mainQueue] addOperationWithBlock:^{ xfer_refresh(); }];
}

/* ================================================================= */
/*  Import and Export                                                 */
/* ================================================================= */

/* Two panels that stay open while you work: the format and its options,
 * then Import... or Export... asks for the file. Export acts on the
 * front document, named in the panel. */

static EWForm *importForm, *exportForm;
static NSPanel *importPanel, *exportPanel;
static NSPopUpButton *iFmt, *iCs, *iJoin, *iInto, *eFmt, *eEol, *eCs;
static NSTextField *iStatus, *eDoc, *eStatus;
static const char *pad40 = "                                                  ";

static NSArray *fmt_names(BOOL automatic, BOOL pdf)
{
    NSMutableArray *a = [NSMutableArray array];
    if (automatic) [a addObject:LS(S_AUTO)];
    [a addObjectsFromArray:@[@"RTF", LS(S_FMT_TEXT), @"Markdown", @"1st Word Plus", @"Protext"]];
    if (pdf) [a addObject:@"PDF"];
    return a;
}

static NSString *fmt_status(int text, NSString *name)
{
    NSString *f = [LS(text) stringByReplacingOccurrencesOfString:@"%s" withString:@"%@"];
    return [NSString stringWithFormat:f, name ? name : @""];
}

/* ---- Import ------------------------------------------------------------- */

static void i_enable(void)
{
    NSInteger f = iFmt.indexOfSelectedItem;
    BOOL text = f == 0 || f == 1 + FT_TEXT;
    BOOL doc = ew_active_doc() != nil;
    iCs.enabled = text || f == 1 + FT_MD;
    iJoin.enabled = text;
    ((NSMenuItem *)[iInto itemAtIndex:1]).enabled = doc;
    if (!doc) [iInto selectItemAtIndex:0];
}

static void i_import(void)
{
    NSOpenPanel *op = [NSOpenPanel openPanel];
    EWDocument *into = iInto.indexOfSelectedItem == 1 ? ew_active_doc() : nil;
    NSInteger f = iFmt.indexOfSelectedItem;
    int type, ok;
    DOC t;
    const char *path;
    if ([op runModal] != NSModalResponseOK) return;
    path = op.URL.path.fileSystemRepresentation;
    type = f == 0 ? file_detect(path, 1) : (int)f - 1;
    if (into) {
        doc_init(&t);
        t.ins = [into doc]->ins;
        ok = doc_read(&t, path, type, (int)iCs.indexOfSelectedItem, (int)iJoin.indexOfSelectedItem);
        if (ok && t.len) [into insertLoaded:&t];
        doc_free(&t);
    } else ok = ew_open_import(path, type, (int)iCs.indexOfSelectedItem, (int)iJoin.indexOfSelectedItem) != nil;
    if (!ok) { ew_alert(AL_CANT_READ); iStatus.stringValue = LS(S_XF_FAILED); return; }
    iStatus.stringValue = fmt_status(S_XF_READ, op.URL.lastPathComponent);
}

void import_open(void)
{
    if (!importPanel) {
        EWForm *f = [EWForm formWithTitle:LSX(M_IMPORT)];
        [f row:LS(S_FORMAT)];
        iFmt = [f popup:fmt_names(YES, NO) selected:0];
        [f row:LS(S_CHARACTERS)];
        iCs = [f popup:@[LS(S_AUTO), @"Atari ST", @"Windows", @"UTF-8", @"UTF-16"] selected:0];
        [f row:LS(S_LINES)];
        iJoin = [f popup:@[LS(S_ONE_PARA), LS(S_JOIN_LINES)] selected:0];
        [f row:LS(S_INTO)];
        iInto = [f popup:@[LS(S_INTO_NEW), LS(S_INTO_CURSOR)] selected:0];
        iInto.autoenablesItems = NO;
        [f row:@""];
        iStatus = [f text:[NSString stringWithUTF8String:pad40]];
        [f onChange:iFmt do:^{ i_enable(); }];
        importPanel = [f panelWithButtons:@[LS(M_IMPORT), LS(S_CLOSE)]
                                   tapped:^(NSInteger i) { if (i == 0) i_import(); else [importPanel orderOut:nil]; }];
        importForm = f;
        [importPanel center];
    }
    iStatus.stringValue = @"";
    i_enable();
    [importPanel makeKeyAndOrderFront:nil];
}

/* ---- Export ------------------------------------------------------------- */

static void e_enable(void)
{
    EWDocument *doc = ew_active_doc();
    BOOL text = eFmt.indexOfSelectedItem == FT_TEXT || eFmt.indexOfSelectedItem == FT_MD;
    eEol.enabled = text;
    eCs.enabled = text;
    eDoc.stringValue = doc ? [doc baseName] : LS(S_XF_NODOC);
}

static void e_export(void)
{
    EWDocument *doc = ew_active_doc();
    static NSString *ext[] = { @"rtf", @"txt", @"md", @"doc", @"pro", @"pdf" };
    NSSavePanel *sp;
    int fmt = (int)eFmt.indexOfSelectedItem, ok;
    DOC *d;
    if (!doc) { NSBeep(); e_enable(); return; }
    d = [doc doc];
    sp = [NSSavePanel savePanel];
    sp.nameFieldStringValue = [[[doc baseName] stringByDeletingPathExtension] stringByAppendingPathExtension:ext[fmt]];
    if ([sp runModal] != NSModalResponseOK) return;
    if (d->cv.dirty) [doc applyCodes];
    if (fmt == FT_PDF) ok = [doc exportPDFTo:sp.URL];
    else {
        NSInteger c = eCs.indexOfSelectedItem;
        ok = doc_write(d, sp.URL.path.fileSystemRepresentation, fmt, (int)eEol.indexOfSelectedItem,
                       c == 0 ? CS_UTF8 : c == 1 ? CS_UTF16 : c == 2 ? CS_WIN : CS_ATARI);
    }
    if (!ok) ew_alert(fmt == FT_PDF ? AL_CANT_PDF : AL_CANT_WRITE);
    eStatus.stringValue = ok ? fmt_status(S_XF_DONE, sp.URL.lastPathComponent) : LS(S_XF_FAILED);
}

void export_open(void)
{
    if (!exportPanel) {
        EWForm *f = [EWForm formWithTitle:LSX(M_EXPORT)];
        NSString *docl = LS(S_XF_DOC);
        NSRange r = [docl rangeOfString:@"%s"];
        if (r.location != NSNotFound) docl = [[docl substringToIndex:r.location]
                                              stringByTrimmingCharactersInSet:NSCharacterSet.whitespaceCharacterSet];
        [f row:docl];
        eDoc = [f text:[NSString stringWithUTF8String:pad40]];
        [f row:LS(S_FORMAT)];
        eFmt = [f popup:fmt_names(NO, YES) selected:0];
        [f row:LS(S_LINE_ENDINGS)];
        eEol = [f popup:@[@"Unix / macOS LF", @"Atari/DOS CR LF"] selected:0];
        [f row:LS(S_CHARACTERS)];
        eCs = [f popup:@[@"UTF-8", @"UTF-16", @"Windows", @"Atari ST"] selected:0];
        [f row:@""];
        eStatus = [f text:[NSString stringWithUTF8String:pad40]];
        [f onChange:eFmt do:^{ e_enable(); eStatus.stringValue = eFmt.indexOfSelectedItem == FT_TEXT ? LS(S_XF_STYLES) : @""; }];
        exportPanel = [f panelWithButtons:@[LS(M_EXPORT), LS(S_CLOSE)]
                                   tapped:^(NSInteger i) { if (i == 0) e_export(); else [exportPanel orderOut:nil]; }];
        exportForm = f;
        [exportPanel center];
        [[NSNotificationCenter defaultCenter] addObserverForName:NSWindowDidBecomeMainNotification object:nil
            queue:nil usingBlock:^(NSNotification *n) { (void)n; xfer_refresh(); }];
    }
    eStatus.stringValue = @"";
    e_enable();
    [exportPanel makeKeyAndOrderFront:nil];
}

/* another document came to the front, or one closed */
void xfer_refresh(void)
{
    if (exportPanel) e_enable();
    if (importPanel) i_enable();
}
