/* Copyright (c) 2026 Francis C Bullen. SPDX-License-Identifier: MIT (see LICENSE) */
/*
 * ewforms.m - dialogs, built in code by a small form builder: a column of
 * labels on the left, a row of controls beside each, buttons at the foot.
 */
#import "ewmac.h"
#include <stdarg.h>

@interface EWFlipView : NSView
@end
@implementation EWFlipView
- (BOOL)isFlipped { return YES; }
@end

@interface EWBlock : NSObject
@property (copy) EWAction block;
- (void)fire:(id)sender;
@end
@implementation EWBlock
- (void)fire:(id)sender { (void)sender; if (self.block) self.block(); }
@end

static NSTextField *label(NSString *s, BOOL wrap)
{
    NSTextField *t = [[NSTextField alloc] initWithFrame:NSMakeRect(0, 0, 10, 10)];
    t.stringValue = s ? s : @"";
    t.editable = NO;
    t.selectable = NO;
    t.bordered = NO;
    t.drawsBackground = NO;
    t.font = [NSFont systemFontOfSize:[NSFont systemFontSize]];
    if (wrap) {
        t.preferredMaxLayoutWidth = 440;
        [t.cell setWraps:YES];
        t.frame = NSMakeRect(0, 0, 440, 10);
        NSSize sz = [t.cell cellSizeForBounds:NSMakeRect(0, 0, 440, 1000)];
        t.frame = NSMakeRect(0, 0, 440, ceil(sz.height));
    } else [t sizeToFit];
    return t;
}

@interface EWForm () <NSWindowDelegate>
@end

@implementation EWForm
{
    NSMutableArray *rows;           /* each: [label or NSNull, controls...] */
    NSMutableArray *targets;
    NSMutableArray *buttons;
    NSInteger result;
    BOOL modal;
}

/* the first field to type in, so typing goes there at once */
- (NSView *)firstField
{
    for (NSArray *r in rows)
        for (id v in r)
            if ([v isKindOfClass:[NSTextField class]] && [v isEditable]) return v;
    return nil;
}

/* the close button of a dialog is Cancel; without this the dialog went
 * away and left the application waiting for it */
- (BOOL)windowShouldClose:(NSWindow *)w
{
    (void)w;
    if (modal) {
        result = (NSInteger)buttons.count - 1;
        modal = NO;
        [NSApp stopModal];
    }
    return YES;
}

+ (EWForm *)formWithTitle:(NSString *)title
{
    EWForm *f = [[EWForm alloc] init];
    f->rows = [NSMutableArray array];
    f->targets = [NSMutableArray array];
    f->buttons = [NSMutableArray array];
    f.win = [[NSPanel alloc] initWithContentRect:NSMakeRect(0, 0, 300, 100)
                                          styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable
                                            backing:NSBackingStoreBuffered defer:YES];
    f.win.title = title ? title : @"";
    f.win.releasedWhenClosed = NO;
    return f;
}

- (void)row:(NSString *)l
{
    [rows addObject:[NSMutableArray arrayWithObject:l ? (id)label(l, NO) : (id)[NSNull null]]];
}

- (void)addView:(NSView *)v
{
    if (!rows.count) [self row:nil];
    [rows.lastObject addObject:v];
}

- (NSTextField *)field:(NSString *)value width:(int)chars
{
    NSTextField *t = [[NSTextField alloc] initWithFrame:NSMakeRect(0, 0, chars * 7.5 + 12, 22)];
    t.stringValue = value ? value : @"";
    /* a field made in code wraps: what is typed past its end goes onto a
     * second line nobody can see. Keep it on one line that scrolls. */
    t.editable = YES;
    t.selectable = YES;
    [t.cell setWraps:NO];
    [t.cell setScrollable:YES];
    if ([t.cell respondsToSelector:@selector(setUsesSingleLineMode:)]) [t.cell setUsesSingleLineMode:YES];
    if ([t.cell respondsToSelector:@selector(setLineBreakMode:)]) [t.cell setLineBreakMode:NSLineBreakByClipping];
    [self addView:t];
    return t;
}

- (NSTextField *)number:(long)value width:(int)chars
{
    NSTextField *t = [self field:[NSString stringWithFormat:@"%ld", value] width:chars < 3 ? 3 : chars];
    t.alignment = NSTextAlignmentRight;
    return t;
}

- (NSTextField *)text:(NSString *)s
{
    NSTextField *t = label(s, NO);
    [self addView:t];
    return t;
}

- (void)note:(NSString *)s
{
    [self row:nil];
    [self addView:label(s, YES)];
}

- (NSButton *)check:(NSString *)title on:(BOOL)on
{
    NSButton *b = [[NSButton alloc] initWithFrame:NSMakeRect(0, 0, 10, 10)];
    [b setButtonType:NSButtonTypeSwitch];
    b.title = title;
    b.state = on ? NSControlStateValueOn : NSControlStateValueOff;
    [b sizeToFit];
    [self addView:b];
    return b;
}

- (NSPopUpButton *)popup:(NSArray *)items selected:(NSInteger)sel
{
    NSPopUpButton *p = [[NSPopUpButton alloc] initWithFrame:NSMakeRect(0, 0, 10, 26) pullsDown:NO];
    [p addItemsWithTitles:items];
    if (sel >= 0 && sel < (NSInteger)items.count) [p selectItemAtIndex:sel];
    [p sizeToFit];
    [self addView:p];
    return p;
}

- (void)onChange:(NSControl *)c do:(EWAction)a
{
    EWBlock *t = [[EWBlock alloc] init];
    t.block = a;
    [targets addObject:t];
    c.target = t;
    c.action = @selector(fire:);
}

- (NSButton *)button:(NSString *)title action:(EWAction)a
{
    NSButton *b = [[NSButton alloc] initWithFrame:NSMakeRect(0, 0, 10, 10)];
    EWBlock *t = [[EWBlock alloc] init];
    t.block = a;
    [targets addObject:t];
    b.title = title;
    b.bezelStyle = NSBezelStyleRounded;
    b.target = t;
    b.action = @selector(fire:);
    [b sizeToFit];
    [self addView:b];
    return b;
}

/* place everything; returns the content size */
- (NSSize)layout:(NSArray *)titles view:(NSView *)cv tapped:(void (^)(NSInteger))tapped
{
    double lw = 0, y = 18, maxx = 0, x, h, bx;
    NSInteger i;
    for (NSArray *r in rows)
        if (r[0] != [NSNull null]) lw = MAX(lw, ((NSView *)r[0]).frame.size.width);
    for (NSArray *r in rows) {
        h = 0;
        for (NSView *v in r) if ((id)v != [NSNull null]) h = MAX(h, v.frame.size.height);
        x = 20;
        if (r[0] != [NSNull null]) {
            NSView *l = r[0];
            l.frame = NSMakeRect(20 + lw - l.frame.size.width, y + (h - l.frame.size.height) / 2,
                                 l.frame.size.width, l.frame.size.height);
            [cv addSubview:l];
        }
        if (lw > 0) x = 20 + lw + 10;
        for (i = 1; i < (NSInteger)r.count; i++) {
            NSView *v = r[i];
            v.frame = NSMakeRect(x, y + (h - v.frame.size.height) / 2, v.frame.size.width, v.frame.size.height);
            [cv addSubview:v];
            x += v.frame.size.width + 8;
        }
        maxx = MAX(maxx, x);
        y += h + 8;
    }
    y += 10;
    /* the buttons, right to left */
    bx = MAX(maxx, 300) + 12;
    for (i = 0; i < (NSInteger)titles.count; i++) {
        NSButton *b = [[NSButton alloc] initWithFrame:NSMakeRect(0, 0, 10, 10)];
        EWBlock *t = [[EWBlock alloc] init];
        NSInteger idx = i;
        b.title = titles[i];
        b.bezelStyle = NSBezelStyleRounded;
        [b sizeToFit];
        if (b.frame.size.width < 90) b.frame = NSMakeRect(0, 0, 90, b.frame.size.height);
        bx -= b.frame.size.width;
        b.frame = NSMakeRect(bx, y, b.frame.size.width, b.frame.size.height);
        bx -= 8;
        t.block = ^{ tapped(idx); };
        [targets addObject:t];
        b.target = t;
        b.action = @selector(fire:);
        if (i == 0) b.keyEquivalent = @"\r";
        else if (i == (NSInteger)titles.count - 1) b.keyEquivalent = @"\033";
        [cv addSubview:b];
        [buttons addObject:b];
    }
    return NSMakeSize(MAX(maxx, 300) + 12, y + 44);
}

- (NSInteger)runWithButtons:(NSArray *)titles
{
    EWFlipView *cv = [[EWFlipView alloc] initWithFrame:NSMakeRect(0, 0, 10, 10)];
    __weak EWForm *me = self;
    NSWindow *was = NSApp.keyWindow;
    NSView *first;
    NSSize sz = [self layout:titles view:cv tapped:^(NSInteger i) {
        EWForm *f = me;
        if (!f || !f->modal) return;
        f->result = i;
        f->modal = NO;
        [NSApp stopModal];
    }];
    cv.frame = NSMakeRect(0, 0, sz.width, sz.height);
    self.win.contentView = cv;
    [self.win setContentSize:sz];
    [self.win center];
    self.win.delegate = self;
    first = [self firstField];
    self.win.initialFirstResponder = first;
    [self.win recalculateKeyViewLoop];
    result = (NSInteger)titles.count - 1;
    modal = YES;
    [NSApp activateIgnoringOtherApps:YES];
    [self.win makeKeyAndOrderFront:nil];
    if (first) {
        [self.win makeFirstResponder:first];
        [(NSTextField *)first selectText:nil];
    }
    [NSApp runModalForWindow:self.win];
    modal = NO;
    self.win.delegate = nil;
    [self.win orderOut:nil];
    if (was && was.isVisible) [was makeKeyAndOrderFront:nil];
    return result;
}

- (NSPanel *)panelWithButtons:(NSArray *)titles tapped:(void (^)(NSInteger))tapped
{
    EWFlipView *cv = [[EWFlipView alloc] initWithFrame:NSMakeRect(0, 0, 10, 10)];
    NSPanel *p;
    NSSize sz = [self layout:titles view:cv tapped:tapped];
    cv.frame = NSMakeRect(0, 0, sz.width, sz.height);
    p = [[NSPanel alloc] initWithContentRect:NSMakeRect(0, 0, sz.width, sz.height)
                                   styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable |
                                             NSWindowStyleMaskUtilityWindow
                                     backing:NSBackingStoreBuffered defer:YES];
    p.title = self.win.title;
    p.contentView = cv;
    p.initialFirstResponder = [self firstField];
    [p recalculateKeyViewLoop];
    p.releasedWhenClosed = NO;
    p.hidesOnDeactivate = YES;
    p.becomesKeyOnlyIfNeeded = NO;
    self.win = p;
    return p;
}
@end

/* ---- helpers -------------------------------------------------------------- */

void ew_alert(int text, ...)
{
    char buf[400];
    va_list ap;
    NSAlert *a = [[NSAlert alloc] init];
    NSString *s;
    va_start(ap, text);
    vsnprintf(buf, sizeof buf, L(text), ap);
    va_end(ap);
    s = [[NSString stringWithUTF8String:buf] stringByReplacingOccurrencesOfString:@"|" withString:@" "];
    a.messageText = s ? s : @"";
    [a addButtonWithTitle:LS(S_BTN_OK)];
    [a runModal];
}

int mm_of(int pt) { return (int)((pt * 254L + (pt >= 0 ? 360 : -360)) / 720); }
static int pt_mm(int mm) { return (int)((mm * 720L + (mm >= 0 ? 127 : -127)) / 254); }
int pt_of_mm(NSTextField *f, int oldpt)
{
    int mm = f.intValue;
    return mm == mm_of(oldpt) ? oldpt : pt_mm(mm);
}

static NSArray *strings(const int *ids, int n)
{
    NSMutableArray *a = [NSMutableArray array];
    int i;
    for (i = 0; i < n; i++) [a addObject:LS(ids[i])];
    return a;
}

static NSArray *ok_cancel(int ok) { return @[LS(ok), LS(S_CANCEL)]; }

/* ---- page layout ------------------------------------------------------------- */

int dlg_page(DOC *d, int page)
{
    EWForm *f = [EWForm formWithTitle:LS(S_PAGE_TITLE)];
    PAGESET *ps = &PS(d);
    PGFMT g;
    NSMutableArray *papers = [NSMutableArray array];
    NSPopUpButton *paper, *orient, *apply;
    NSTextField *mt, *mb, *ml, *mr, *nc, *gap, *wc;
    static const int ap[3] = { S_WHOLE_DOC, S_THIS_PAGE, S_THIS_PAGE_ON };
    static const int or[2] = { S_PORTRAIT, S_LANDSCAPE };
    int i, how = d->a.novr ? 1 : 0, v;

    page_fmt(d, page, &g);
    for (i = 0; i < NPAPER; i++) [papers addObject:[NSString stringWithUTF8String:paper_name[i]]];
    for (i = 0; i < d->a.novr; i++) {
        PGOVR *o = &d->a.ovr[i];
        if (o->page == page) { how = o->following ? 2 : 1; break; }
        if (o->page < page && o->following) how = 2;
    }
    [f row:LS(S_PAPER)];   paper = [f popup:papers selected:ps->paper];
    [f row:LS(S_ORIENT)];  orient = [f popup:strings(or, 2) selected:ps->landscape];
    [f row:LS(S_MARGINS_MM)];
    [f text:LS(S_TOP)];    mt = [f number:mm_of(g.mt) width:3];
    [f text:LS(S_BOTTOM)]; mb = [f number:mm_of(g.mb) width:3];
    [f text:LS(S_LEFT_M)]; ml = [f number:mm_of(g.ml) width:3];
    [f text:LS(S_RIGHT_M)]; mr = [f number:mm_of(g.mr) width:3];
    [f row:LS(S_COLUMNS)]; nc = [f number:g.cols width:2];
    [f text:LS(S_GAP)];    gap = [f number:mm_of(g.colgap) width:3]; [f text:LS(S_MM)];
    [f row:LS(S_WRAPCOL)]; wc = [f number:ps->wrapcol width:3]; [f text:LS(S_ZERO_OFF)];
    [f row:LS(S_APPLY_TO)]; apply = [f popup:strings(ap, 3) selected:how];
    if ([f runWithButtons:ok_cancel(S_BTN_OK)] != 0) return 0;

    i = (int)paper.indexOfSelectedItem;
    v = (int)orient.indexOfSelectedItem;
    if (i != ps->paper || v != ps->landscape) page_set_paper(ps, i, v);
    g.mt = (short)pt_of_mm(mt, g.mt); g.mb = (short)pt_of_mm(mb, g.mb);
    g.ml = (short)pt_of_mm(ml, g.ml); g.mr = (short)pt_of_mm(mr, g.mr);
    v = nc.intValue;
    g.cols = (short)(v < 1 ? 1 : v > 4 ? 4 : v);
    g.colgap = (short)pt_of_mm(gap, g.colgap);
    v = wc.intValue;
    ps->wrapcol = (short)(v < 0 ? 0 : v > 250 ? 250 : v);
    how = (int)apply.indexOfSelectedItem;
    /* margins that leave no room for text would leave nothing to lay out */
    if (g.ml < 0) g.ml = 0;
    if (g.mr < 0) g.mr = 0;
    if (g.mt < 0) g.mt = 0;
    if (g.mb < 0) g.mb = 0;
    if (g.ml + g.mr > ps->w - 85) g.ml = g.mr = (short)((ps->w - 85) / 2);
    if (g.mt + g.mb > ps->h - 85) g.mt = g.mb = (short)((ps->h - 85) / 2);
    if (how > 0) page_ovr_set(d, page, how == 2, &g);
    else {
        ps->mt = g.mt; ps->mb = g.mb; ps->ml = g.ml; ps->mr = g.mr;
        ps->cols = g.cols; ps->colgap = g.colgap;
        page_ovr_clear(d);
    }
    if (ps->ml + ps->mr > ps->w - 85) ps->ml = ps->mr = (short)((ps->w - 85) / 2);
    if (ps->mt + ps->mb > ps->h - 85) ps->mt = ps->mb = (short)((ps->h - 85) / 2);
    d->dirty = 1;
    return 1;
}

int dlg_wrapcol(DOC *d)
{
    EWForm *f = [EWForm formWithTitle:LS(S_WRAP_TITLE)];
    NSTextField *wc;
    int v;
    [f row:LS(S_WRAPCOL)]; wc = [f number:PS(d).wrapcol width:3];
    [f note:LS(S_WRAP_HINT)];
    if ([f runWithButtons:ok_cancel(S_BTN_OK)] != 0) return 0;
    v = wc.intValue;
    PS(d).wrapcol = (short)(v < 0 ? 0 : v > 250 ? 250 : v);
    d->dirty = 1;
    return 1;
}

int dlg_headfoot(DOC *d)
{
    EWForm *f = [EWForm formWithTitle:LS(S_HF_TITLE)];
    static const int jn[3] = { S_J_LEFT, S_J_CENTRE, S_J_RIGHT };
    NSTextField *t[2], *sz;
    NSPopUpButton *j[2];
    NSButton *first;
    int i, v;
    for (i = 0; i < 2; i++) {
        [f row:LS(i ? S_FOOTER : S_HEADER)];
        t[i] = [f field:st2ns((unsigned char *)HF(d).text[i], (long)strlen(HF(d).text[i])) width:44];
        j[i] = [f popup:strings(jn, 3) selected:HF(d).just[i]];
    }
    [f row:LS(S_SIZE)];
    sz = [f number:HF(d).size width:2];
    [f text:LS(S_POINT)];
    first = [f check:LS(S_FIRST_PAGE_TOO) on:HF(d).first];
    [f row:LS(S_FIELDS)];
    [f text:@"{page} {pages} {date} {time} {file}"];
    if ([f runWithButtons:ok_cancel(S_BTN_OK)] != 0) return 0;
    for (i = 0; i < 2; i++) {
        ns2st(t[i].stringValue, HF(d).text[i], sizeof HF(d).text[i]);
        HF(d).just[i] = (unsigned char)j[i].indexOfSelectedItem;
    }
    v = sz.intValue;
    HF(d).size = (unsigned char)(v < 4 ? 4 : v > 72 ? 72 : v);
    HF(d).first = first.state == NSControlStateValueOn;
    d->dirty = 1;
    return 1;
}

/* Insert > Page Numbers: a page number in the header or footer */
int dlg_pagenum(DOC *d)
{
    EWForm *f = [EWForm formWithTitle:LS(S_PN_TITLE)];
    static const int jn[3] = { S_J_LEFT, S_J_CENTRE, S_J_RIGHT }, wn[2] = { S_PN_TOP, S_PN_BOTTOM };
    NSPopUpButton *w, *j, *st;
    NSButton *first;
    NSString *pg = LS(S_PN_PAGE), *of = LS(S_PN_OF);
    char pw[34], ow[24];
    int where = 1, style = 1, just = 1;
    NSInteger r;
    if (hf_pagenum_find(d, &where, &style)) just = HF(d).just[where];
    [f row:LS(S_PN_WHERE)]; w = [f popup:strings(wn, 2) selected:where];
    [f row:LS(S_PN_ALIGN)]; j = [f popup:strings(jn, 3) selected:just];
    [f row:LS(S_PN_STYLE)];
    st = [f popup:@[@"1", [NSString stringWithFormat:@"%@ 1", pg], [NSString stringWithFormat:@"%@ 1 %@ 3", pg, of], @"- 1 -"]
         selected:style];
    first = [f check:LS(S_FIRST_PAGE_TOO) on:HF(d).first];
    r = [f runWithButtons:@[LS(S_BTN_OK), LS(S_CANCEL), LS(S_PN_REMOVE)]];
    if (r == 2) { hf_pagenum_clear(d); return 1; }
    if (r != 0) return 0;
    ns2st(pg, pw, sizeof pw);
    ns2st(of, ow, sizeof ow);
    hf_pagenum_set(d, (int)w.indexOfSelectedItem, (int)j.indexOfSelectedItem, (int)st.indexOfSelectedItem, pw, ow);
    HF(d).first = first.state == NSControlStateValueOn;
    d->dirty = 1;
    return 1;
}

int dlg_indents(int *left, int *first)
{
    EWForm *f = [EWForm formWithTitle:LS(S_INDENTS_TITLE)];
    NSTextField *l, *fl;
    int a, b;
    [f row:LS(S_LEFT_INDENT)]; l = [f number:mm_of(*left) width:3];
    [f row:LS(S_FIRST_LINE)];  fl = [f number:mm_of(*first) width:4];
    [f note:LS(S_HANG_HINT)];
    if ([f runWithButtons:ok_cancel(S_BTN_OK)] != 0) return 0;
    a = pt_of_mm(l, *left);
    b = pt_of_mm(fl, *first);
    if (a < 0) a = 0;
    if (a > 400) a = 400;
    if (b < -a) b = -a;
    if (b > 400) b = 400;
    *left = a; *first = b;
    return 1;
}

int dlg_table(int insert, int *rows, int *cols, short *w, int *border)
{
    EWForm *f = [EWForm formWithTitle:LS(insert ? S_TBL_INS_TITLE : S_TBL_PROP_TITLE)];
    NSTextField *r, *c, *wf[MAX_COLS];
    NSButton *b;
    int i, n;
    [f row:LS(S_ROWS)];    r = [f number:*rows width:3];
    [f row:LS(S_COLUMNS)]; c = [f number:*cols width:2]; [f text:@"(1-12)"];
    if (!insert) {
        [f row:LS(S_COL_WIDTHS)];
        for (i = 0; i < *cols && i < MAX_COLS; i++) {
            if (i == 6) [f row:@""];
            wf[i] = [f number:mm_of(w[i]) width:3];
        }
    }
    [f row:@""];
    b = [f check:LS(S_GRID_LINES) on:*border];
    if ([f runWithButtons:ok_cancel(insert ? S_INSERT : S_BTN_OK)] != 0) return 0;
    n = r.intValue;
    *rows = n < 1 ? 1 : n > 200 ? 200 : n;
    n = c.intValue;
    n = n < 1 ? 1 : n > MAX_COLS ? MAX_COLS : n;
    if (!insert)
        for (i = 0; i < *cols && i < MAX_COLS; i++) {
            int v = pt_of_mm(wf[i], w[i]);
            w[i] = (short)(v < 12 ? 12 : v);
        }
    *cols = n;
    *border = b.state == NSControlStateValueOn;
    return 1;
}

int dlg_objprops(DOC *d, OBJ *o)
{
    EWForm *f = [EWForm formWithTitle:LS(o->type == OB_IMG ? S_PIC_TITLE : S_DRAW_TITLE)];
    NSTextField *x, *y, *w, *h, *pg;
    NSPopUpButton *wrap;
    NSButton *frame;
    NSMutableArray *wraps = [NSMutableArray arrayWithObjects:LS(S_WRAP_NONE), LS(S_WRAP_BOX), nil];
    NSArray *btn;
    NSInteger r;
    int v;
    if (o->type == OB_DRAW) [wraps addObject:LS(S_WRAP_SHAPE)];
    [f row:LS(S_POSITION_MM)];
    [f text:LS(S_FROM_LEFT)]; x = [f number:mm_of(o->x) width:4];
    [f text:LS(S_FROM_TOP)];  y = [f number:mm_of(o->y) width:4];
    [f row:LS(S_SIZE_MM)];
    [f text:LS(S_WIDTH)];  w = [f number:mm_of(o->w) width:4];
    [f text:LS(S_HEIGHT)]; h = [f number:mm_of(o->h) width:4];
    [f row:LS(S_PAGE)]; pg = [f number:o->page + 1 width:3];
    [f row:LS(S_TEXT_WRAP)];
    wrap = [f popup:wraps selected:o->wrap == WR_SHAPE && o->type != OB_DRAW ? WR_BOX : o->wrap];
    [f row:@""];
    frame = [f check:LS(S_FRAME) on:o->border];
    btn = o->type == OB_DRAW ? @[LS(S_BTN_OK), LS(S_EDIT_DRAWING), LS(S_DELETE), LS(S_CANCEL)]
                             : @[LS(S_BTN_OK), LS(S_DELETE), LS(S_CANCEL)];
    r = [f runWithButtons:btn];
    if (r == (NSInteger)btn.count - 1) return 0;
    if ([btn[r] isEqual:LS(S_DELETE)]) return 2;
    o->x = (short)pt_of_mm(x, o->x); o->y = (short)pt_of_mm(y, o->y);
    v = pt_of_mm(w, o->w); o->w = (short)(v < 6 ? 6 : v);
    v = pt_of_mm(h, o->h); o->h = (short)(v < 6 ? 6 : v);
    v = pg.intValue - 1;
    o->page = (short)(v < 0 ? 0 : v >= doc_npages(d) ? doc_npages(d) - 1 : v);
    o->wrap = (short)wrap.indexOfSelectedItem;
    o->border = frame.state == NSControlStateValueOn;
    return r == 1 && o->type == OB_DRAW ? 3 : 1;
}

int dlg_size(int *size)
{
    EWForm *f = [EWForm formWithTitle:LS(S_SIZE_TITLE)];
    NSTextField *s;
    int v;
    [f row:LS(S_POINT_SIZE)]; s = [f number:*size width:3]; [f text:@"(4-120)"];
    if ([f runWithButtons:ok_cancel(S_BTN_OK)] != 0) return 0;
    v = s.intValue;
    if (v < 4 || v > 120) return 0;
    *size = v;
    return 1;
}

void dlg_summary(DOC *d, NSString *name)
{
    EWForm *f = [EWForm formWithTitle:LS(S_SUMMARY_TITLE)];
    static const int lab[8] = { S_ST_PAGES, S_ST_WORDS, S_ST_CHARS, S_ST_CHARS_NS, S_ST_PARAS, S_ST_LINES,
                                S_ST_TABLES, S_ST_OBJECTS };
    DSTATS s;
    long val[8];
    int i;
    doc_layout(d);
    doc_stats(d, &s);
    val[0] = s.pages; val[1] = s.words; val[2] = s.chars; val[3] = s.chars_ns;
    val[4] = s.paras; val[5] = s.lines; val[6] = s.tables; val[7] = s.objects;
    [f row:nil];
    [f text:name];
    for (i = 0; i < 8; i++) {
        [f row:LS(lab[i])];
        [f text:[NSString stringWithFormat:@"%ld", val[i]]];
    }
    [f runWithButtons:@[LS(S_BTN_OK)]];
}

int dlg_prefs(void)
{
    EWForm *f = [EWForm formWithTitle:LS(S_PREFS_TITLE)];
    static const int order[NDATEFMT] = { DT_SHORT, DT_US, DT_ISO, DT_LONG, DT_DAYLONG };
    static const int or[2] = { S_PORTRAIT, S_LANDSCAPE };
    NSMutableArray *langs = [NSMutableArray array], *dates = [NSMutableArray array], *papers = [NSMutableArray array];
    NSPopUpButton *lang, *date, *paper, *orient, *dict;
    NSButton *tbar, *live;
    int i, dsel = 0;
    char buf[48];
    for (i = 0; i < NLANG; i++) [langs addObject:[NSString stringWithUTF8String:lang_name[i]]];
    for (i = 0; i < NDATEFMT; i++) {
        fmt_now(buf, order[i]);
        [dates addObject:st2ns((unsigned char *)buf, (long)strlen(buf))];
        if (pref_date == order[i]) dsel = i;
    }
    for (i = 0; i < NPAPER; i++) [papers addObject:[NSString stringWithUTF8String:paper_name[i]]];
    [f row:LS(S_LANGUAGE)];    lang = [f popup:langs selected:ui_lang];
    [f row:LS(S_DATE_FORMAT)]; date = [f popup:dates selected:dsel];
    [f row:LS(S_NEW_PAGES)];   paper = [f popup:papers selected:pref_paper];
    orient = [f popup:strings(or, 2) selected:pref_land];
    [f row:LS(S_NEW_WINDOWS)]; tbar = [f check:LS(S_SHOW_TOOLBAR) on:pref_toolbar];
    [f row:LS(S_SPELLING)];    live = [f check:LS(S_LIVE_SPELL) on:pref_live];
    [f row:LS(S_DICTIONARY)];  dict = [f popup:langs selected:spell_lang];
    if ([f runWithButtons:@[LS(S_SAVE_BTN), LS(S_CANCEL)]] != 0) return 0;
    pref_date = order[date.indexOfSelectedItem];
    pref_paper = (int)paper.indexOfSelectedItem;
    pref_land = (int)orient.indexOfSelectedItem;
    pref_toolbar = tbar.state == NSControlStateValueOn;
    pref_live = live.state == NSControlStateValueOn;
    if ((int)lang.indexOfSelectedItem != ui_lang) set_language((int)lang.indexOfSelectedItem);
    if ((int)dict.indexOfSelectedItem != spell_lang) {     /* the other dictionary, when next needed */
        spell_unload_main();
        spell_lang = (int)dict.indexOfSelectedItem;
    }
    prefs_save();
    return 1;
}

int dlg_textimport(int eol, int det, int *cs, int *join)
{
    EWForm *f = [EWForm formWithTitle:LS(S_IMPORT_TITLE)];
    static const int eols[] = { S_EOL_NONE, S_EOL_LF, S_EOL_CRLF, S_EOL_CR, S_EOL_MIXED };
    static const char *css[] = { "", "Atari ST", "Windows", "UTF-8", "UTF-16" };
    char info[200];
    NSPopUpButton *c, *j;
    snprintf(info, sizeof info, L(S_FOUND), L(eols[eol]), css[det]);
    [f note:[NSString stringWithUTF8String:info]];
    [f row:LS(S_CHARACTERS)];
    c = [f popup:@[LS(S_AUTO), @"Atari ST", @"Windows", @"UTF-8", @"UTF-16"] selected:0];
    [f row:LS(S_LINES)];
    j = [f popup:@[LS(S_ONE_PARA), LS(S_JOIN_LINES)] selected:0];
    if ([f runWithButtons:ok_cancel(S_IMPORT)] != 0) return 0;
    *cs = (int)c.indexOfSelectedItem;
    *join = (int)j.indexOfSelectedItem;
    return 1;
}

int dlg_textexport(int *crlf, int *cs)
{
    EWForm *f = [EWForm formWithTitle:LS(S_EXPORT_TITLE)];
    NSPopUpButton *e, *c;
    [f row:LS(S_LINE_ENDINGS)];
    e = [f popup:@[@"Unix / macOS LF", @"Atari/DOS CR LF"] selected:0];
    [f row:LS(S_CHARACTERS)];
    c = [f popup:@[@"UTF-8", @"UTF-16", @"Windows", @"Atari ST"] selected:0];
    [f note:LS(S_EXPORT_HINT)];
    if ([f runWithButtons:ok_cancel(S_EXPORT)] != 0) return 0;
    *crlf = (int)e.indexOfSelectedItem;
    *cs = c.indexOfSelectedItem == 0 ? CS_UTF8 : c.indexOfSelectedItem == 1 ? CS_UTF16 : c.indexOfSelectedItem == 2 ? CS_WIN : CS_ATARI;
    return 1;
}

int dlg_goto(DOC *d, int *page)
{
    EWForm *f = [EWForm formWithTitle:LS(S_GOTO_TITLE)];
    NSTextField *p;
    long v;
    [f row:LS(S_GOTO_PAGE)];
    p = [f number:*page + 1 width:5];
    [f text:[NSString stringWithFormat:LS(S_OF_N), doc_npages(d)]];
    if ([f runWithButtons:ok_cancel(S_GO)] != 0) return 0;
    v = p.integerValue;
    if (v < 1) v = 1;
    if (v > doc_npages(d)) v = doc_npages(d);
    *page = (int)v - 1;
    return 1;
}
