/* Copyright (c) 2026 Francis C Bullen. SPDX-License-Identifier: MIT (see LICENSE) */
/*
 * ewpage.m - the page view: draws the pages, takes the keyboard and the
 * mouse, selects and moves pictures and drawings.
 */
#import "ewmac.h"

#define GAP 16.0                    /* around and between pages, points */
#define HS  4.0                     /* half a handle, points             */

@implementation EWPageView
{
    NSTimer *blink;
    BOOL caretOn;
    NSString *marked;               /* text being composed (dead keys)  */
}

- (instancetype)initWithFrame:(NSRect)r
{
    self = [super initWithFrame:r];
    caretOn = YES;
    blink = [NSTimer scheduledTimerWithTimeInterval:0.53 target:self selector:@selector(blinkTick:)
                                           userInfo:nil repeats:YES];
    return self;
}

- (void)dealloc { [blink invalidate]; }

- (BOOL)isFlipped { return YES; }
- (BOOL)acceptsFirstResponder { return YES; }
- (BOOL)becomeFirstResponder { caretOn = YES; [self setNeedsDisplay:YES]; return YES; }
- (BOOL)resignFirstResponder { [self setNeedsDisplay:YES]; return YES; }
- (DOC *)d { return self.owner ? [self.owner doc] : NULL; }

/* ---- geometry ------------------------------------------------------------ */

- (double)scale { return self.owner ? [self d]->zoom / 100.0 / units_per_pt() : 1; }

- (NSSize)contentSize
{
    DOC *d = [self d];
    double s = [self scale];
    int n = doc_npages(d);
    return NSMakeSize(page_w_px(d) * s + 2 * GAP, n * (page_h_px(d) * s + GAP) + GAP);
}

- (void)resize
{
    NSSize c = [self contentSize], v = self.enclosingScrollView.contentSize;
    [self setFrameSize:NSMakeSize(MAX(c.width, v.width), MAX(c.height, v.height))];
    [self setNeedsDisplay:YES];
}

- (NSRect)pageRect:(int)page
{
    DOC *d = [self d];
    double s = [self scale], pw = page_w_px(d) * s, ph = page_h_px(d) * s;
    double x = MAX(GAP, (self.bounds.size.width - pw) / 2);
    return NSMakeRect(floor(x), GAP + page * (ph + GAP), pw, ph);
}

- (int)pageAt:(NSPoint)p x:(long *)px y:(long *)py
{
    DOC *d = [self d];
    double s = [self scale], ph = page_h_px(d) * s;
    int page = (int)floor((p.y - GAP) / (ph + GAP)), n = doc_npages(d);
    NSRect r;
    if (page < 0) page = 0;
    if (page >= n) page = n - 1;
    r = [self pageRect:page];
    *px = lround((p.x - r.origin.x) / s);
    *py = lround((p.y - r.origin.y) / s);
    return page;
}

- (NSRect)unitsToView:(NSRect)u page:(int)page
{
    NSRect r = [self pageRect:page];
    double s = [self scale];
    return NSMakeRect(r.origin.x + u.origin.x * s, r.origin.y + u.origin.y * s, u.size.width * s, u.size.height * s);
}

- (NSRect)caretRect
{
    DOC *d = [self d];
    long li = doc_find_line(d, d->cursor);
    LINE *ln = &d->lines[li];
    return NSInsetRect([self unitsToView:NSMakeRect(doc_x_of(d, li, d->cursor), ln->py, units_per_pt(),
                                                    ln->asc + ln->desc) page:ln->page], -2, -2);
}

- (void)showCaret
{
    if (![self d] || ![self d]->nlines) return;
    [self scrollRectToVisible:NSInsetRect([self caretRect], -20, -20)];
}

- (void)redrawLines:(long)a to:(long)b
{
    DOC *d = [self d];
    long i;
    if (a < 0) a = 0;
    if (b >= d->nlines) b = d->nlines - 1;
    for (i = a; i <= b; i++) {
        LINE *ln = &d->lines[i];
        [self setNeedsDisplayInRect:NSInsetRect([self unitsToView:NSMakeRect(0, ln->py, page_w_px(d), ln->lh + ln->desc)
                                                             page:ln->page], 0, -2)];
    }
}

- (void)blinkTick:(NSTimer *)t
{
    (void)t;
    if (!self.owner || self.window.firstResponder != self || !self.window.isKeyWindow) {
        if (!caretOn) { caretOn = YES; }
        return;
    }
    if ([self d]->anchor >= 0 || [self d]->selobj >= 0) return;
    caretOn = !caretOn;
    [self setNeedsDisplayInRect:[self caretRect]];
}

- (void)caretShow { caretOn = YES; }

/* ---- drawing -------------------------------------------------------------- */

- (void)drawRect:(NSRect)dirty
{
    DOC *d = [self d];
    int p, n;
    double s;
    DRAWOPT o;
    long a, b;
    if (!d) return;
    s = [self scale];
    n = doc_npages(d);
    [[NSColor colorWithCalibratedWhite:0.82 alpha:1] set];
    NSRectFill(dirty);
    o.live = 1;
    o.selobj = d->selobj;
    if (doc_sel(d, &a, &b)) { o.sa = a; o.sb = b; } else o.sa = o.sb = -1;
    o.caret = (caretOn && self.window.firstResponder == self && d->selobj < 0 &&
               (d->view == VIEW_DOC || d->focus == 0)) ? d->cursor : -1;
    for (p = 0; p < n; p++) {
        NSRect r = [self pageRect:p], c;
        NSAffineTransform *t;
        if (!NSIntersectsRect(NSInsetRect(r, -6, -6), dirty)) continue;
        [[NSColor colorWithCalibratedWhite:0.55 alpha:1] set];
        NSRectFill(NSMakeRect(NSMaxX(r), r.origin.y + 4, 4, r.size.height));
        NSRectFill(NSMakeRect(r.origin.x + 4, NSMaxY(r), r.size.width, 4));
        c = NSIntersectionRect(r, dirty);
        if (NSIsEmptyRect(c)) continue;
        [NSGraphicsContext saveGraphicsState];
        NSRectClip(c);
        t = [NSAffineTransform transform];
        [t translateXBy:r.origin.x yBy:r.origin.y];
        [t scaleBy:s];
        [t concat];
        ew_draw_page(d, p, NSMakeRect((c.origin.x - r.origin.x) / s, (c.origin.y - r.origin.y) / s,
                                      c.size.width / s, c.size.height / s), &o);
        if (pref_guides) {
            /* dotted lines at the margins and the column gaps */
            PGFMT f;
            NSBezierPath *g = [NSBezierPath bezierPath];
            CGFloat dash[2];
            double l, t, rt, bt;
            int k;
            page_fmt(d, p, &f);
            l = pt2px_x(f.ml); rt = pt2px_x(PS(d).w - f.mr);
            t = pt2px_y(f.mt); bt = pt2px_y(PS(d).h - f.mb);
            [g appendBezierPathWithRect:NSMakeRect(l, t, rt - l, bt - t)];
            if (f.cols > 1) {
                double cw = (rt - l - pt2px_x(f.colgap) * (f.cols - 1)) / f.cols;
                for (k = 1; k < f.cols; k++) {
                    double cx = l + k * cw + (k - 1) * pt2px_x(f.colgap);
                    [g moveToPoint:NSMakePoint(cx, t)]; [g lineToPoint:NSMakePoint(cx, bt)];
                    cx += pt2px_x(f.colgap);
                    [g moveToPoint:NSMakePoint(cx, t)]; [g lineToPoint:NSMakePoint(cx, bt)];
                }
            }
            dash[0] = 2 / s; dash[1] = 2 / s;
            [g setLineDash:dash count:2 phase:0];
            g.lineWidth = 0.5 / s;
            [[NSColor colorWithCalibratedRed:0.45 green:0.6 blue:0.85 alpha:1] set];
            [g stroke];
        }
        [NSGraphicsContext restoreGraphicsState];
    }
    if (d->selobj >= 0 && d->selobj < d->a.nobj) {
        OBJ *ob = &d->a.objs[d->selobj];
        NSRect r = [self unitsToView:NSMakeRect(pt2px_x(ob->x), pt2px_y(ob->y), pt2px_x(ob->w), pt2px_y(ob->h))
                                page:ob->page];
        NSBezierPath *f = [NSBezierPath bezierPathWithRect:r];
        CGFloat dash[2] = { 3, 3 };
        int k;
        [f setLineDash:dash count:2 phase:0];
        [[NSColor keyboardFocusIndicatorColor] set];
        [f stroke];
        for (k = 0; k < 4; k++) {
            double hx = k & 1 ? NSMaxX(r) : NSMinX(r), hy = k & 2 ? NSMaxY(r) : NSMinY(r);
            NSRect h = NSMakeRect(hx - HS, hy - HS, 2 * HS, 2 * HS);
            [[NSColor whiteColor] set];
            NSRectFill(h);
            [[NSColor keyboardFocusIndicatorColor] set];
            NSFrameRect(h);
        }
    }
}

/* ---- editing helpers --------------------------------------------------------- */

- (void)moveTo:(long)pos extend:(BOOL)ext
{
    DOC *d = [self d];
    if (pos < 0) pos = 0;
    if (pos > d->len) pos = d->len;
    if (ext) { if (d->anchor < 0) d->anchor = d->cursor; }
    else d->anchor = -1;
    d->cursor = pos;
    if (d->anchor == d->cursor) d->anchor = -1;
    d->selobj = -1;
    caretOn = YES;
    [self.owner moved];
}

- (void)typeCells:(const char *)s length:(long)n
{
    DOC *d = [self d];
    long a, b, from = doc_sel(d, &a, &b) ? a : d->cursor, i;
    CELL c[256];
    int simple = n == 1 && s[0] != '\n' && !doc_sel(d, &a, &b);
    if (n > 256) n = 256;
    for (i = 0; i < n; i++) { c[i] = d->ins; c[i].ch = (unsigned char)s[i]; }
    if (doc_type(d, c, n)) [self.owner edited:from typing:simple];
    else ew_alert(AL_NO_MEMORY);
    caretOn = YES;
}

- (BOOL)objectKey:(NSEvent *)e
{
    DOC *d = [self d];
    OBJ *o;
    unichar k;
    int step = (e.modifierFlags & NSEventModifierFlagShift) ? 1 : 6;
    if (d->selobj < 0 || d->selobj >= d->a.nobj || !e.charactersIgnoringModifiers.length) return NO;
    o = &d->a.objs[d->selobj];
    k = [e.charactersIgnoringModifiers characterAtIndex:0];
    switch (k) {
    case NSDeleteCharacter: case NSDeleteFunctionKey: case NSBackspaceCharacter:
        [self.owner command:C_OBJDEL]; return YES;
    case 27: d->selobj = -1; [self setNeedsDisplay:YES]; return YES;
    case '\r': case 3:
        [self.owner command:o->type == OB_DRAW ? C_OBJEDIT : C_OBJPROP]; return YES;
    case NSLeftArrowFunctionKey:  o->x = (short)(o->x > step ? o->x - step : 0); break;
    case NSRightArrowFunctionKey: if (o->x + o->w + step <= PS(d).w) o->x += step; break;
    case NSUpArrowFunctionKey:    o->y = (short)(o->y > step ? o->y - step : 0); break;
    case NSDownArrowFunctionKey:  if (o->y + o->h + step <= PS(d).h) o->y += step; break;
    default: d->selobj = -1; [self setNeedsDisplay:YES]; return NO;
    }
    d->dirty = 1;
    [self.owner changed];
    return YES;
}

- (void)keyDown:(NSEvent *)e
{
    if ([self d]->selobj >= 0 && [self objectKey:e]) return;
    [self interpretKeyEvents:@[e]];
}

/* ---- NSTextInputClient ---------------------------------------------------------- */

- (void)insertText:(id)s replacementRange:(NSRange)r
{
    NSString *str = [s isKindOfClass:[NSAttributedString class]] ? [s string] : s;
    char buf[256];
    int n;
    (void)r;
    marked = nil;
    n = ns2st(str, buf, sizeof buf);
    if (n) [self typeCells:buf length:n];
}
- (void)insertText:(id)s { [self insertText:s replacementRange:NSMakeRange(NSNotFound, 0)]; }
- (void)setMarkedText:(id)s selectedRange:(NSRange)sel replacementRange:(NSRange)r
{
    (void)sel; (void)r;
    marked = [s isKindOfClass:[NSAttributedString class]] ? [s string] : s;
}
- (void)unmarkText { marked = nil; }
- (NSRange)selectedRange
{
    DOC *d = [self d];
    long a, b;
    if (doc_sel(d, &a, &b)) return NSMakeRange((NSUInteger)a, (NSUInteger)(b - a));
    return NSMakeRange((NSUInteger)d->cursor, 0);
}
- (NSRange)markedRange { return marked.length ? NSMakeRange((NSUInteger)[self d]->cursor, marked.length) : NSMakeRange(NSNotFound, 0); }
- (BOOL)hasMarkedText { return marked.length > 0; }
- (NSAttributedString *)attributedSubstringForProposedRange:(NSRange)r actualRange:(NSRangePointer)a
{
    DOC *d = [self d];
    long s = (long)r.location, e = (long)(r.location + r.length);
    if (s > d->len) return nil;
    if (e > d->len) e = d->len;
    if (a) *a = NSMakeRange((NSUInteger)s, (NSUInteger)(e - s));
    return [[NSAttributedString alloc] initWithString:cells2ns(d, s, e)];
}
- (NSArray *)validAttributesForMarkedText { return @[]; }
- (NSRect)firstRectForCharacterRange:(NSRange)r actualRange:(NSRangePointer)a
{
    (void)r; (void)a;
    return [self.window convertRectToScreen:[self convertRect:[self caretRect] toView:nil]];
}
- (NSUInteger)characterIndexForPoint:(NSPoint)p
{
    long px, py;
    NSPoint v = [self convertPoint:[self.window convertRectFromScreen:NSMakeRect(p.x, p.y, 0, 0)].origin fromView:nil];
    int page = [self pageAt:v x:&px y:&py];
    return (NSUInteger)doc_hit([self d], page, px, py);
}

/* ---- key commands (from interpretKeyEvents) ------------------------------------------- */

- (void)doCommandBySelector:(SEL)sel
{
    if ([self respondsToSelector:sel]) {
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Warc-performSelector-leaks"
        [self performSelector:sel withObject:nil];
#pragma clang diagnostic pop
    }
}

- (void)moveHoriz:(int)dir extend:(BOOL)ext
{
    DOC *d = [self d];
    long a, b;
    d->want_x = -1;
    if (!ext && doc_sel(d, &a, &b)) { [self moveTo:dir < 0 ? a : b extend:NO]; return; }
    [self moveTo:d->cursor + dir extend:ext];
}

- (void)moveVert:(int)dir extend:(BOOL)ext
{
    DOC *d = [self d];
    long li = doc_find_line(d, d->cursor), nl, wx;
    if (d->want_x < 0) d->want_x = doc_x_of(d, li, d->cursor);
    wx = d->want_x;
    nl = doc_line_vert(d, li, wx, dir);
    if (nl < 0) { [self moveTo:0 extend:ext]; return; }
    if (nl >= d->nlines) { [self moveTo:d->len extend:ext]; return; }
    [self moveTo:doc_pos_in_line(d, nl, wx) extend:ext];
    d->want_x = wx;
}

- (void)lineEdge:(BOOL)end extend:(BOOL)ext
{
    DOC *d = [self d];
    long li = doc_find_line(d, d->cursor);
    LINE *ln = &d->lines[li];
    long e = ln->start + ln->len;
    d->want_x = -1;
    if (!end) { [self moveTo:ln->start extend:ext]; return; }
    if (e > ln->start && (ISEND(d->text[e - 1].ch) || li < d->nlines - 1)) e--;
    [self moveTo:e extend:ext];
}

- (void)moveLeft:(id)s { [self moveHoriz:-1 extend:NO]; }
- (void)moveRight:(id)s { [self moveHoriz:1 extend:NO]; }
- (void)moveLeftAndModifySelection:(id)s { [self moveHoriz:-1 extend:YES]; }
- (void)moveRightAndModifySelection:(id)s { [self moveHoriz:1 extend:YES]; }
- (void)moveUp:(id)s { [self moveVert:-1 extend:NO]; }
- (void)moveDown:(id)s { [self moveVert:1 extend:NO]; }
- (void)moveUpAndModifySelection:(id)s { [self moveVert:-1 extend:YES]; }
- (void)moveDownAndModifySelection:(id)s { [self moveVert:1 extend:YES]; }
- (void)moveToBeginningOfLine:(id)s { [self lineEdge:NO extend:NO]; }
- (void)moveToEndOfLine:(id)s { [self lineEdge:YES extend:NO]; }
- (void)moveToLeftEndOfLine:(id)s { [self lineEdge:NO extend:NO]; }
- (void)moveToRightEndOfLine:(id)s { [self lineEdge:YES extend:NO]; }
- (void)moveToBeginningOfLineAndModifySelection:(id)s { [self lineEdge:NO extend:YES]; }
- (void)moveToEndOfLineAndModifySelection:(id)s { [self lineEdge:YES extend:YES]; }
- (void)moveToLeftEndOfLineAndModifySelection:(id)s { [self lineEdge:NO extend:YES]; }
- (void)moveToRightEndOfLineAndModifySelection:(id)s { [self lineEdge:YES extend:YES]; }
- (void)moveToBeginningOfDocument:(id)s { [self moveTo:0 extend:NO]; }
- (void)moveToEndOfDocument:(id)s { [self moveTo:[self d]->len extend:NO]; }
- (void)moveToBeginningOfDocumentAndModifySelection:(id)s { [self moveTo:0 extend:YES]; }
- (void)moveToEndOfDocumentAndModifySelection:(id)s { [self moveTo:[self d]->len extend:YES]; }
- (void)moveWord:(int)dir extend:(BOOL)ext
{
    DOC *d = [self d];
    long p = d->cursor;
    if (dir < 0) {
        while (p > 0 && !st_isalpha(d->text[p - 1].ch)) p--;
        while (p > 0 && st_isalpha(d->text[p - 1].ch)) p--;
    } else {
        while (p < d->len && !st_isalpha(d->text[p].ch)) p++;
        while (p < d->len && st_isalpha(d->text[p].ch)) p++;
    }
    d->want_x = -1;
    [self moveTo:p extend:ext];
}
- (void)moveWordLeft:(id)s { [self moveWord:-1 extend:NO]; }
- (void)moveWordRight:(id)s { [self moveWord:1 extend:NO]; }
- (void)moveWordLeftAndModifySelection:(id)s { [self moveWord:-1 extend:YES]; }
- (void)moveWordRightAndModifySelection:(id)s { [self moveWord:1 extend:YES]; }
- (void)pageDownBy:(int)dir extend:(BOOL)ext
{
    DOC *d = [self d];
    int i, n = (int)(self.enclosingScrollView.contentSize.height / ([self scale] * get_metrics(d->ins.font, d->ins.size, 0)->asc * 1.3));
    for (i = 0; i < (n > 1 ? n - 1 : 1); i++) [self moveVert:dir extend:ext];
}
- (void)pageDown:(id)s { [self pageDownBy:1 extend:NO]; }
- (void)pageUp:(id)s { [self pageDownBy:-1 extend:NO]; }
- (void)scrollPageDown:(id)s { [self pageDownBy:1 extend:NO]; }
- (void)scrollPageUp:(id)s { [self pageDownBy:-1 extend:NO]; }
- (void)pageDownAndModifySelection:(id)s { [self pageDownBy:1 extend:YES]; }
- (void)pageUpAndModifySelection:(id)s { [self pageDownBy:-1 extend:YES]; }

- (void)deleteBackward:(id)s
{
    DOC *d = [self d];
    long a, b;
    if (doc_sel(d, &a, &b)) { doc_delete_sel(d); [self.owner edited:a typing:NO]; }
    else if (d->cursor > 0) {
        doc_delete_range(d, d->cursor - 1, d->cursor);
        d->cursor--;
        [self.owner edited:d->cursor typing:YES];
    }
    caretOn = YES;
}

- (void)deleteForward:(id)s
{
    DOC *d = [self d];
    long a, b;
    if (doc_sel(d, &a, &b)) { doc_delete_sel(d); [self.owner edited:a typing:NO]; }
    else if (d->cursor < d->len) { doc_delete_range(d, d->cursor, d->cursor + 1); [self.owner edited:d->cursor typing:YES]; }
    caretOn = YES;
}

- (void)deleteWordBackward:(id)s
{
    DOC *d = [self d];
    long p = d->cursor, e = d->cursor;
    if (d->anchor >= 0) { [self deleteBackward:s]; return; }
    while (p > 0 && !st_isalpha(d->text[p - 1].ch)) p--;
    while (p > 0 && st_isalpha(d->text[p - 1].ch)) p--;
    if (p == e) return;
    doc_delete_range(d, p, e);
    d->cursor = p;
    [self.owner edited:p typing:NO];
}

/* Tab and Return move between table cells */
- (BOOL)tableKey:(BOOL)tab back:(BOOL)back
{
    DOC *d = [self d];
    long p, t0, t1;
    if (!doc_in_table(d, d->cursor, NULL, NULL)) return NO;
    p = doc_next_cell(d, d->cursor, back ? -1 : 1);
    if (p < 0 && !back) {
        if (tab) { if (tbl_row_insert(d, d->cursor, 1)) [self.owner edited:d->cursor typing:NO]; return YES; }
        tbl_range(d, d->cursor, &t0, &t1);
        [self moveTo:t1 extend:NO];
        return YES;
    }
    if (p >= 0) [self.owner selectFrom:p to:doc_cell_end(d, p)];
    return YES;
}

- (void)insertNewline:(id)s { if (![self tableKey:NO back:NO]) [self typeCells:"\n" length:1]; }
- (void)insertNewlineIgnoringFieldEditor:(id)s { [self insertNewline:s]; }
- (void)insertTab:(id)s { if (![self tableKey:YES back:NO]) [self typeCells:"\t" length:1]; }
- (void)insertBacktab:(id)s { [self tableKey:YES back:YES]; }
- (void)insertTabIgnoringFieldEditor:(id)s { [self insertTab:s]; }
- (void)cancelOperation:(id)s
{
    DOC *d = [self d];
    if (d->anchor >= 0) [self moveTo:d->cursor extend:NO];
    else if (d->selobj >= 0) { d->selobj = -1; [self setNeedsDisplay:YES]; }
}

/* ---- the standard edit menu --------------------------------------------------------- */

- (void)undo:(id)s { [self.owner command:C_UNDO]; }
- (void)redo:(id)s { [self.owner command:C_REDO]; }
- (void)selectAll:(id)s { [self.owner selectFrom:0 to:[self d]->len]; }

static NSString *const EWPboardType = @"org.edword.rtf";

- (void)copy:(id)s
{
    DOC *d = [self d], t;
    long a, b;
    NSString *tmp;
    NSData *rtf;
    NSPasteboard *pb = [NSPasteboard generalPasteboard];
    if (d->selobj >= 0 && d->selobj < d->a.nobj && d->a.objs[d->selobj].type == OB_IMG) {
        /* a picture: for other programs as an image */
        NSData *tiff = ew_img_tiff(d, d->selobj);
        if (tiff) { [pb clearContents]; [pb setData:tiff forType:NSPasteboardTypeTIFF]; }
        return;
    }
    if (!doc_sel(d, &a, &b)) return;
    doc_copy(d);
    doc_init(&t);
    t.a = d->a;
    t.a.ps.wrapcol = 0; t.a.nobj = 0; t.a.novr = 0;
    doc_insert(&t, 0, d->text + a, b - a);
    tmp = [NSTemporaryDirectory() stringByAppendingPathComponent:@"edword-clip.rtf"];
    rtf_export(&t, tmp.fileSystemRepresentation);
    doc_free(&t);
    rtf = [NSData dataWithContentsOfFile:tmp];
    [pb clearContents];
    if (rtf) {
        [pb setData:rtf forType:EWPboardType];
        [pb setData:rtf forType:NSPasteboardTypeRTF];
    }
    [pb setString:cells2ns(d, a, b) forType:NSPasteboardTypeString];
}

- (void)cut:(id)s
{
    DOC *d = [self d];
    long a, b;
    if (d->selobj >= 0 && d->selobj < d->a.nobj && d->a.objs[d->selobj].type == OB_IMG) {
        [self copy:s];
        [self.owner command:C_OBJDEL];
        return;
    }
    if (!doc_sel(d, &a, &b)) return;
    [self copy:s];
    doc_delete_sel(d);
    [self.owner edited:a typing:NO];
}

- (void)paste:(id)s
{
    DOC *d = [self d], t;
    long a, b, from = doc_sel(d, &a, &b) ? a : d->cursor;
    NSPasteboard *pb = [NSPasteboard generalPasteboard];
    NSData *rtf = [pb dataForType:EWPboardType];
    NSString *str = [pb stringForType:NSPasteboardTypeString];
    int ok = 0;
    if (!rtf && !str && [pb availableTypeFromArray:@[NSPasteboardTypeTIFF, NSPasteboardTypePNG]]) {
        /* a picture from another program */
        NSString *tmp = [NSTemporaryDirectory() stringByAppendingPathComponent:@"edword-paste.tiff"];
        NSData *img = [pb dataForType:NSPasteboardTypeTIFF];
        if (!img) { img = [pb dataForType:NSPasteboardTypePNG]; tmp = [tmp.stringByDeletingPathExtension stringByAppendingPathExtension:@"png"]; }
        if (img && [img writeToFile:tmp atomically:NO]) insert_picture(self.owner, [NSURL fileURLWithPath:tmp]);
        return;
    }
    doc_init(&t);
    t.ins = d->ins;
    if (rtf) {
        NSString *tmp = [NSTemporaryDirectory() stringByAppendingPathComponent:@"edword-paste.rtf"];
        [rtf writeToFile:tmp atomically:NO];
        ok = rtf_import(&t, tmp.fileSystemRepresentation);
    } else if (str) {
        const char *u = str.UTF8String;
        ok = text_import_buf(&t, (const unsigned char *)u, (long)strlen(u), CS_UTF8, 0);
    }
    if (ok && t.len) {
        /* a trailing paragraph end of plain text is not wanted */
        long n = t.len;
        if (!rtf && n > 0 && t.text[n - 1].ch == '\n' && ![str hasSuffix:@"\n"]) n--;
        if (doc_insert_doc(d, &t, 0, n)) [self.owner edited:from typing:NO];
    }
    doc_free(&t);
}

- (void)delete:(id)s { [self deleteForward:s]; }

- (BOOL)validateMenuItem:(NSMenuItem *)m
{
    DOC *d = [self d];
    long a, b;
    SEL s = m.action;
    if (!d) return NO;
    if (s == @selector(undo:)) return undo_can(d) || d->ch_lo >= 0;
    if (s == @selector(redo:)) return redo_can(d);
    if (s == @selector(copy:) || s == @selector(cut:))
        return doc_sel(d, &a, &b) || (d->selobj >= 0 && d->selobj < d->a.nobj && d->a.objs[d->selobj].type == OB_IMG);
    if (s == @selector(delete:)) return doc_sel(d, &a, &b);
    if (s == @selector(paste:))
        return [[NSPasteboard generalPasteboard] availableTypeFromArray:
                    @[EWPboardType, NSPasteboardTypeString, NSPasteboardTypeTIFF, NSPasteboardTypePNG]] != nil;
    return [super respondsToSelector:@selector(validateMenuItem:)] ? YES : YES;
}

/* the font panel */
- (void)changeFont:(id)sender
{
    DOC *d = [self d];
    NSFont *cur = ew_font(d->ins.font, d->ins.size, d->ins.attr);
    NSFont *f = [sender convertFont:cur];
    int i = font_index(f.familyName), sz = (int)lround(f.pointSize);
    if (i >= 0 && i != d->ins.font) [self.owner command:C_FONT0 + i];
    if (sz != d->ins.size && sz >= 4 && sz <= 120) {
        long a, b2;
        if (doc_sel(d, &a, &b2)) { doc_apply(d, a, b2, AP_SIZE, sz); d->ins.size = (unsigned char)sz; [self.owner edited:a typing:NO]; }
        else { d->ins.size = (unsigned char)sz; [self.owner updateInfo]; }
    }
}

/* ---- the mouse --------------------------------------------------------------- */

- (int)objectAt:(NSPoint)p
{
    DOC *d = [self d];
    int i;
    for (i = d->a.nobj - 1; i >= 0; i--) {
        OBJ *o = &d->a.objs[i];
        NSRect r = [self unitsToView:NSMakeRect(pt2px_x(o->x), pt2px_y(o->y), pt2px_x(o->w), pt2px_y(o->h)) page:o->page];
        if (NSPointInRect(p, r)) return i;
    }
    return -1;
}

- (int)handleAt:(NSPoint)p
{
    DOC *d = [self d];
    OBJ *o;
    NSRect r;
    int k;
    if (d->selobj < 0 || d->selobj >= d->a.nobj) return -1;
    o = &d->a.objs[d->selobj];
    r = [self unitsToView:NSMakeRect(pt2px_x(o->x), pt2px_y(o->y), pt2px_x(o->w), pt2px_y(o->h)) page:o->page];
    for (k = 0; k < 4; k++) {
        double hx = k & 1 ? NSMaxX(r) : NSMinX(r), hy = k & 2 ? NSMaxY(r) : NSMinY(r);
        if (fabs(p.x - hx) <= HS + 2 && fabs(p.y - hy) <= HS + 2) return k;
    }
    return -1;
}

/* drag the selected object (corner < 0) or one of its corners */
- (void)dragObject:(NSEvent *)down corner:(int)corner
{
    DOC *d = [self d];
    OBJ *o = &d->a.objs[d->selobj], orig = *o;
    NSPoint start = [self convertPoint:down.locationInWindow fromView:nil];
    double s = [self scale], u = units_per_pt();
    BOOL moved = NO;
    for (;;) {
        NSEvent *e = [self.window nextEventMatchingMask:NSEventMaskLeftMouseDragged | NSEventMaskLeftMouseUp];
        NSPoint p;
        double dx, dy;
        if (e.type == NSEventTypeLeftMouseUp) break;
        [self autoscroll:e];
        p = [self convertPoint:e.locationInWindow fromView:nil];
        dx = (p.x - start.x) / s / u;        /* points */
        dy = (p.y - start.y) / s / u;
        if (!moved && fabs(p.x - start.x) < 3 && fabs(p.y - start.y) < 3) continue;
        moved = YES;
        *o = orig;
        if (corner < 0) {
            o->x = (short)MAX(0, MIN(PS(d).w - o->w, orig.x + dx));
            o->y = (short)MAX(0, MIN(PS(d).h - o->h, orig.y + dy));
        } else {
            double x0 = orig.x, y0 = orig.y, x1 = orig.x + orig.w, y1 = orig.y + orig.h;
            if (corner & 1) x1 += dx; else x0 += dx;
            if (corner & 2) y1 += dy; else y0 += dy;
            if (x1 < x0 + 8) { if (corner & 1) x1 = x0 + 8; else x0 = x1 - 8; }
            if (y1 < y0 + 8) { if (corner & 2) y1 = y0 + 8; else y0 = y1 - 8; }
            if (orig.type == OB_IMG && !(e.modifierFlags & NSEventModifierFlagShift) && orig.w > 0) {
                double nh = (x1 - x0) * orig.h / orig.w;
                if (corner & 2) y1 = y0 + nh; else y0 = y1 - nh;
            }
            o->x = (short)MAX(0, x0); o->y = (short)MAX(0, y0);
            o->w = (short)(x1 - x0); o->h = (short)(y1 - y0);
            if (orig.type == OB_DRAW && orig.w > 0 && orig.h > 0) {
                int k;
                for (k = 0; k < o->nsh; k++) {
                    SHAPE *sh = &o->sh[k];
                    sh->x1 = (short)(orig.sh[k].x1 * o->w / orig.w); sh->x2 = (short)(orig.sh[k].x2 * o->w / orig.w);
                    sh->y1 = (short)(orig.sh[k].y1 * o->h / orig.h); sh->y2 = (short)(orig.sh[k].y2 * o->h / orig.h);
                }
            }
        }
        doc_layout(d);
        [self setNeedsDisplay:YES];
        [self displayIfNeeded];
    }
    if (moved) { d->dirty = 1; [self.owner changed]; }
}

- (void)mouseDown:(NSEvent *)e
{
    DOC *d = [self d];
    NSPoint p = [self convertPoint:e.locationInWindow fromView:nil];
    long px, py, pos, wa, wb;
    int page, h, i;
    char word[WORDMAX];
    BOOL shift = (e.modifierFlags & NSEventModifierFlagShift) != 0;

    [self.window makeFirstResponder:self];
    if ([self.owner doc]->view == VIEW_SPLIT) [self d]->focus = 0;
    if (d->view != VIEW_DOC && d->cv.dirty) [self.owner applyCodes];
    h = [self handleAt:p];
    if (h >= 0) { [self dragObject:e corner:h]; return; }
    i = [self objectAt:p];
    if (i >= 0) {
        d->selobj = i;
        d->anchor = -1;
        [self setNeedsDisplay:YES];
        [self.owner updateInfo];
        if (e.clickCount >= 2) { [self.owner command:d->a.objs[i].type == OB_DRAW ? C_OBJEDIT : C_OBJPROP]; return; }
        [self dragObject:e corner:-1];
        return;
    }
    if (d->selobj >= 0) { d->selobj = -1; [self setNeedsDisplay:YES]; }
    page = [self pageAt:p x:&px y:&py];
    pos = doc_hit(d, page, px, py);

    /* holding the button on an underlined word offers corrections */
    if (pref_live && e.clickCount < 2 && word_at(d, pos, &wa, &wb, word, WORDMAX) && word_misspelt(d, wa, wb)) {
        NSEvent *n = [self.window nextEventMatchingMask:NSEventMaskLeftMouseUp | NSEventMaskLeftMouseDragged
                                              untilDate:[NSDate dateWithTimeIntervalSinceNow:0.5]
                                                 inMode:NSEventTrackingRunLoopMode dequeue:NO];
        if (!n) {
            [self.owner selectFrom:wa to:wb];
            live_popup(self.owner, wa, wb, e, self);
            return;
        }
    }
    if (e.clickCount == 2) {
        if (word_at(d, pos, &wa, &wb, word, WORDMAX)) [self.owner selectFrom:wa to:wb];
        return;
    }
    if (e.clickCount >= 3) { [self.owner selectFrom:doc_para_start(d, pos) to:doc_para_end(d, pos)]; return; }
    if (shift) { if (d->anchor < 0) d->anchor = d->cursor; }
    else d->anchor = pos;
    d->cursor = pos;
    d->want_x = -1;
    caretOn = YES;
    [self.owner moved];
    for (;;) {
        NSEvent *m = [self.window nextEventMatchingMask:NSEventMaskLeftMouseDragged | NSEventMaskLeftMouseUp];
        if (m.type == NSEventTypeLeftMouseUp) break;
        [self autoscroll:m];
        p = [self convertPoint:m.locationInWindow fromView:nil];
        page = [self pageAt:p x:&px y:&py];
        pos = doc_hit(d, page, px, py);
        if (pos != d->cursor) { d->cursor = pos; [self setNeedsDisplay:YES]; }
    }
    if (d->anchor == d->cursor) d->anchor = -1;
    [self.owner moved];
}

- (NSMenu *)menuForEvent:(NSEvent *)e
{
    DOC *d = [self d];
    NSPoint p = [self convertPoint:e.locationInWindow fromView:nil];
    long px, py, pos, wa, wb;
    char word[WORDMAX];
    int page = [self pageAt:p x:&px y:&py];
    NSMenu *m;
    pos = doc_hit(d, page, px, py);
    if (word_at(d, pos, &wa, &wb, word, WORDMAX) && (spell_ensure(1), word_misspelt(d, wa, wb))) {
        [self.owner selectFrom:wa to:wb];
        live_popup(self.owner, wa, wb, e, self);
        return nil;
    }
    m = [[NSMenu alloc] init];
    [m addItemWithTitle:LS(M_CUT) action:@selector(cut:) keyEquivalent:@""];
    [m addItemWithTitle:LS(M_COPY) action:@selector(copy:) keyEquivalent:@""];
    [m addItemWithTitle:LS(M_PASTE) action:@selector(paste:) keyEquivalent:@""];
    /* on a table or its chart: the chart's kind and place */
    {
        int t;
        long into = -1;
        for (t = 0; t < d->a.ntbl; t++)
            if (d->a.tbls[t].chart && d->cbox[t].page == page && px >= d->cbox[t].x && px < d->cbox[t].x + d->cbox[t].w &&
                py >= d->cbox[t].y && py < d->cbox[t].y + d->cbox[t].h) {
                long k;
                for (k = 0; k < d->nrows; k++) if (d->rows[k].tbl == t) { into = d->rows[k].start; break; }
            }
        if (into < 0 && doc_in_table(d, pos, NULL, NULL)) into = pos;
        if (into >= 0) {
            NSMenu *cm = [[NSMenu alloc] initWithTitle:LSX(M_CHART)];
            d->cursor = into; d->anchor = -1;
            [self.owner moved];
            ew_add_chart_items(cm);
            [m addItem:[NSMenuItem separatorItem]];
            [m addItemWithTitle:LSX(M_CHART) action:NULL keyEquivalent:@""].submenu = cm;
        }
    }
    return m;
}
@end
