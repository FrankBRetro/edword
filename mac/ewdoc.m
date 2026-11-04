/* Copyright (c) 2026 Francis C Bullen. SPDX-License-Identifier: MIT (see LICENSE) */
/*
 * ewdoc.m - a document: its window (toolbar, page view, codes view, info
 * line), reading and writing RTF, and the commands that act on it.
 */
#import "ewmac.h"

static NSString *const TB_UNDO = @"undo", *const TB_REDO = @"redo", *const TB_STYLE = @"style",
                *const TB_ALIGN = @"align", *const TB_LIST = @"list", *const TB_TABLE = @"table",
                *const TB_FONT = @"font", *const TB_SIZE = @"size", *const TB_COLOUR = @"colour",
                *const TB_ZOOM = @"zoom", *const TB_SPELL = @"spell", *const TB_CODES = @"codes",
                *const TB_RULER = @"ruler", *const TB_GUIDES = @"guides";

const short sizes[NSIZES] = { 8, 9, 10, 11, 12, 14, 18, 24, 36, 48 };
const short zooms[NZOOM] = { 50, 75, 100, 125, 150, 200, 300 };

EWDocument *ew_active_doc(void)
{
    NSDocument *d = [[NSDocumentController sharedDocumentController] currentDocument];
    if (![d isKindOfClass:[EWDocument class]]) {
        for (NSWindow *w in [NSApp orderedWindows])
            if ([[w.windowController document] isKindOfClass:[EWDocument class]]) return [w.windowController document];
        return nil;
    }
    return (EWDocument *)d;
}

static NSImage *symbol(NSString *name, NSString *fallback)
{
#ifndef GNUSTEP
    if (@available(macOS 11.0, *)) {
        NSImage *i = [NSImage imageWithSystemSymbolName:name accessibilityDescription:fallback];
        if (i) return i;
    }
#endif
    (void)name; (void)fallback;
    return nil;
}

@interface EWDocument () <NSToolbarDelegate, NSTextViewDelegate, NSSplitViewDelegate, NSWindowDelegate>
@end

@implementation EWDocument
{
    NSTimer *codesTimer;
    BOOL rebuilding, imported;
    LINE *oldLines;
    NSRect oldCaret;
    long oldN, oldLen;
    NSSegmentedControl *styleSeg, *alignSeg, *listSeg;
    NSPopUpButton *fontPop, *sizePop, *colourPop, *zoomPop;
}

- (instancetype)init
{
    self = [super init];
    doc_init(&d);
    page_set_paper(&d.a.ps, pref_paper, pref_land);
    d.toolbar = pref_toolbar;
    d.zoom = 100;
    d.cv.cols = 80;
    doc_layout(&d);
    undo_reset(&d);
    oldN = -1;
    return self;
}

- (void)dealloc
{
    [codesTimer invalidate];
    undo_free(&d);
    codes_free(&d.cv);
    doc_free(&d);
    free(oldLines);
}

- (DOC *)doc { return &d; }

+ (BOOL)autosavesInPlace { return NO; }
+ (NSArray *)writableTypes { return @[@"public.rtf"]; }
- (BOOL)hasUndoManager { return NO; }

/* ---- files ----------------------------------------------------------------- */

- (BOOL)readFromURL:(NSURL *)url ofType:(NSString *)type error:(NSError **)err
{
    const char *path = url.path.fileSystemRepresentation;
    int ok;
    (void)type;
    doc_free(&d);
    doc_init(&d);
    d.toolbar = pref_toolbar;
    d.zoom = 100;
    d.cv.cols = 80;
    {
        int type = file_detect(path, 0);
        ok = doc_read(&d, path, type, CS_AUTO, 0);
        imported = type != FT_RTF;
    }
    if (!ok) {
        if (err) *err = [NSError errorWithDomain:NSCocoaErrorDomain code:NSFileReadCorruptFileError userInfo:nil];
        return NO;
    }
    doc_layout(&d);
    undo_reset(&d);
    d.dirty = 0;
    return YES;
}

- (BOOL)writeToURL:(NSURL *)url ofType:(NSString *)type error:(NSError **)err
{
    (void)type;
    if (d.cv.dirty) [self applyCodes];
    if (!rtf_export(&d, url.path.fileSystemRepresentation)) {
        if (err) *err = [NSError errorWithDomain:NSCocoaErrorDomain code:NSFileWriteUnknownError userInfo:nil];
        return NO;
    }
    return YES;
}

- (void)updateChangeCount:(NSDocumentChangeType)change
{
    [super updateChangeCount:change];
    if (change == NSChangeCleared) d.dirty = 0;
}

- (void)noteDirty
{
    if ((d.dirty || d.cv.dirty) && ![self isDocumentEdited]) [self updateChangeCount:NSChangeDone];
}

- (NSPrintOperation *)printOperationWithSettings:(NSDictionary *)s error:(NSError **)err
{
    (void)s; (void)err;
    if (d.cv.dirty) [self applyCodes];
    return ew_print_op(self, nil);
}

- (NSString *)baseName
{
    return self.fileURL ? self.fileURL.lastPathComponent : self.displayName;
}

/* ---- the window --------------------------------------------------------------- */

- (void)makeWindowControllers
{
    NSRect sf = [NSScreen mainScreen].visibleFrame;
    double w = MIN(sf.size.width - 40, page_w_px(&d) / units_per_pt() + 80), h = sf.size.height - 40;
    NSWindow *win = [[NSWindow alloc] initWithContentRect:NSMakeRect(0, 0, w, h)
                                                styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable |
                                                          NSWindowStyleMaskMiniaturizable | NSWindowStyleMaskResizable
                                                  backing:NSBackingStoreBuffered defer:NO];
    NSWindowController *wc = [[NSWindowController alloc] initWithWindow:win];
    NSView *cv = win.contentView;
    NSRect b = cv.bounds;
    NSToolbar *tb;

    self.win = win;
    win.delegate = self;
    win.minSize = NSMakeSize(420, 300);
    self.info = [[NSTextField alloc] initWithFrame:NSMakeRect(8, 3, b.size.width - 16, 17)];
    self.info.editable = NO; self.info.bordered = NO; self.info.drawsBackground = NO;
    self.info.font = [NSFont systemFontOfSize:11];
    self.info.textColor = [NSColor secondaryLabelColor];
    self.info.autoresizingMask = NSViewWidthSizable | NSViewMaxYMargin;
    [cv addSubview:self.info];

    self.split = [[NSSplitView alloc] initWithFrame:NSMakeRect(0, 22, b.size.width, b.size.height - 22)];
    self.split.vertical = NO;
    self.split.dividerStyle = NSSplitViewDividerStyleThin;
    self.split.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
    self.split.delegate = self;
    [cv addSubview:self.split];

    self.pageScroll = [[NSScrollView alloc] initWithFrame:NSMakeRect(0, 0, b.size.width, b.size.height - 22)];
    self.pageScroll.hasVerticalScroller = YES;
    self.pageScroll.hasHorizontalScroller = YES;
    self.pageScroll.autohidesScrollers = YES;
    self.pageScroll.drawsBackground = NO;
    self.page = [[EWPageView alloc] initWithFrame:NSMakeRect(0, 0, 100, 100)];
    self.page.owner = self;
    self.pageScroll.documentView = self.page;
    [self.split addSubview:self.pageScroll];

    self.codesScroll = [[NSScrollView alloc] initWithFrame:NSMakeRect(0, 0, b.size.width, 200)];
    self.codesScroll.hasVerticalScroller = YES;
    self.codes = [[NSTextView alloc] initWithFrame:NSMakeRect(0, 0, b.size.width, 200)];
    self.codes.font = [NSFont userFixedPitchFontOfSize:12];
    self.codes.richText = NO;
    self.codes.allowsUndo = YES;
    if ([self.codes respondsToSelector:@selector(setAutomaticQuoteSubstitutionEnabled:)]) {
        self.codes.automaticQuoteSubstitutionEnabled = NO;
        self.codes.automaticDashSubstitutionEnabled = NO;
        self.codes.automaticTextReplacementEnabled = NO;
    }
    self.codes.continuousSpellCheckingEnabled = NO;
    self.codes.delegate = self;
    self.codes.autoresizingMask = NSViewWidthSizable;
    self.codes.textContainer.widthTracksTextView = YES;
    self.codesScroll.documentView = self.codes;
    [self.split addSubview:self.codesScroll];

    tb = [[NSToolbar alloc] initWithIdentifier:@"EdWordToolbar"];
    tb.delegate = self;
    tb.displayMode = NSToolbarDisplayModeIconOnly;
    tb.allowsUserCustomization = YES;
    win.toolbar = tb;
    tb.visible = d.toolbar;

    [self addWindowController:wc];
    if (imported) { self.fileURL = nil; self.fileType = @"public.rtf"; }
    [win cascadeTopLeftFromPoint:NSMakePoint(sf.origin.x + 20, NSMaxY(sf) - 20)];
    [self setView:VIEW_DOC force:YES];
    [self.page resize];
    [win makeFirstResponder:self.page];
    [self updateInfo];
    [self updateToolbar];
}

- (void)windowWillClose:(NSNotification *)n
{
    (void)n;
    panels_doc_closed(self);
}

- (void)windowDidResize:(NSNotification *)n { (void)n; [self.page resize]; [self updateRulers]; }

- (void)windowDidBecomeKey:(NSNotification *)n { (void)n; [self updateToolbar]; }

/* ---- views and zoom ------------------------------------------------------------- */

- (void)setView:(int)view force:(BOOL)force
{
    if (view == d.view && !force) return;
    if (d.view != VIEW_DOC && d.cv.dirty) [self applyCodes];
    d.view = view;
    d.focus = view == VIEW_CODES;
    self.codesScroll.hidden = view == VIEW_DOC;
    self.pageScroll.hidden = view == VIEW_CODES;
    [self.split adjustSubviews];
    if (view == VIEW_SPLIT) [self.split setPosition:self.split.bounds.size.height * 0.6 ofDividerAtIndex:0];
    if (view != VIEW_DOC) [self rebuildCodes];
    [self.win makeFirstResponder:view == VIEW_CODES ? (NSView *)self.codes : (NSView *)self.page];
    [self.page resize];
    [self updateInfo];
}

- (void)setView:(int)view { [self setView:view force:NO]; }

- (void)setZoom:(int)zoom
{
    NSRect vis = self.pageScroll.contentView.bounds;
    double rel = vis.origin.y / MAX(1, self.page.frame.size.height);
    if (zoom < zooms[0]) zoom = zooms[0];
    if (zoom > zooms[NZOOM - 1]) zoom = zooms[NZOOM - 1];
    if (zoom == d.zoom) return;
    d.zoom = zoom;
    if (d.view == VIEW_CODES) [self setView:VIEW_DOC];
    [self.page resize];
    [self.page scrollPoint:NSMakePoint(0, rel * self.page.frame.size.height)];
    [self.page showCaret];
    [self updateInfo];
    [self updateToolbar];
    [self updateRulers];
}

- (CGFloat)splitView:(NSSplitView *)s constrainMinCoordinate:(CGFloat)p ofSubviewAt:(NSInteger)i
{
    (void)s; (void)p; (void)i;
    return 80;
}

/* ---- the codes view --------------------------------------------------------------- */

static void mark_codes(NSTextStorage *ts)
{
    NSString *s = ts.string;
    NSUInteger i = 0, n = s.length;
    NSColor *grey = [NSColor secondaryLabelColor];
    [ts beginEditing];
    [ts removeAttribute:NSForegroundColorAttributeName range:NSMakeRange(0, n)];
    [ts addAttribute:NSForegroundColorAttributeName value:[NSColor textColor] range:NSMakeRange(0, n)];
    while (i < n) {
        unichar c = [s characterAtIndex:i];
        if (c == '[' && i + 1 < n && [s characterAtIndex:i + 1] == '[') { i += 2; continue; }
        if (c == '[') {
            NSUInteger e = i + 1;
            while (e < n && [s characterAtIndex:e] != ']' && [s characterAtIndex:e] != '\n') e++;
            if (e < n && [s characterAtIndex:e] == ']') {
                [ts addAttribute:NSForegroundColorAttributeName value:grey range:NSMakeRange(i, e - i + 1)];
                i = e + 1;
                continue;
            }
        }
        i++;
    }
    [ts endEditing];
}

- (void)rebuildCodes
{
    NSRange sel = self.codes.selectedRange;
    NSString *s;
    if (d.view == VIEW_DOC) { d.codes_stale = 1; return; }
    codes_build(&d);
    d.cv.dirty = 0;
    d.codes_stale = 0;
    s = st2ns((unsigned char *)d.cv.t, d.cv.len);
    rebuilding = YES;
    self.codes.string = s;
    mark_codes(self.codes.textStorage);
    if (NSMaxRange(sel) <= s.length) self.codes.selectedRange = sel;
    rebuilding = NO;
}

- (void)textDidChange:(NSNotification *)n
{
    (void)n;
    if (rebuilding) return;
    d.cv.dirty = 1;
    [codesTimer invalidate];
    codesTimer = [NSTimer scheduledTimerWithTimeInterval:0.8 target:self selector:@selector(codesPause:)
                                                userInfo:nil repeats:NO];
    [self noteDirty];
}

- (void)codesPause:(NSTimer *)t
{
    (void)t;
    if (d.cv.dirty) [self applyCodes];
    mark_codes(self.codes.textStorage);
}

- (void)applyCodes
{
    NSString *s = self.codes.string;
    long n = (long)s.length;
    char *buf;
    [codesTimer invalidate];
    codesTimer = nil;
    if (!d.cv.dirty) return;
    buf = malloc(n + 2);
    if (!buf) return;
    n = ns2st(s, buf, (int)n + 1);
    free(d.cv.t);
    d.cv.t = buf;
    d.cv.len = n;
    d.cv.cap = n + 1;
    codes_apply(&d);
    d.cv.dirty = 0;
    undo_commit(&d, 0);
    doc_layout(&d);
    doc_sync_ins(&d);
    d.dirty = 1;
    [self.page resize];
    [self noteDirty];
    [self updateInfo];
    [self updateToolbar];
}

- (BOOL)codesFocus
{
    return d.view != VIEW_DOC && self.win.firstResponder == self.codes;
}

- (void)insertCode:(NSString *)code
{
    [self.codes insertText:code replacementRange:self.codes.selectedRange];
}

/* ---- after changes --------------------------------------------------------------- */

- (void)keepLines
{
    free(oldLines);
    oldLines = malloc((d.nlines + 1) * sizeof(LINE));
    if (!oldLines) { oldN = -1; return; }
    memcpy(oldLines, d.lines, d.nlines * sizeof(LINE));
    oldN = d.nlines;
    oldLen = d.len;
    oldCaret = d.nlines ? [self.page caretRect] : NSZeroRect;
}

static int same_line(const LINE *a, const LINE *b, long shift)
{
    LINE t = *a;
    t.start += shift;
    return !memcmp(&t, b, sizeof(LINE));
}

/* redraw only the lines an edit changed */
- (void)redrawChanged
{
    long a = 0, eo, en, shift = d.len - oldLen;
    if (oldN < 1 || !d.nlines) { [self.page setNeedsDisplay:YES]; return; }
    while (a < oldN && a < d.nlines && same_line(&oldLines[a], &d.lines[a], 0)) a++;
    eo = oldN - 1; en = d.nlines - 1;
    while (eo >= a && en >= a && same_line(&oldLines[eo], &d.lines[en], shift)) { eo--; en--; }
    if (a <= en) [self.page redrawLines:a to:en];
    if (a <= eo) {
        long i;
        for (i = a; i <= eo; i++) {
            LINE *ln = &oldLines[i];
            NSRect r = [self.page pageRect:ln->page];
            double s = [self.page scale];
            [self.page setNeedsDisplayInRect:NSMakeRect(r.origin.x, r.origin.y + ln->py * s - 2, r.size.width,
                                                        (ln->lh + ln->desc) * s + 4)];
        }
    }
}

- (void)edited:(long)from typing:(BOOL)typing
{
    int oldpages = d.npages;
    if (oldN < 0) [self keepLines];
    undo_commit(&d, typing);
    doc_layout(&d);
    d.want_x = -1;
    d.selobj = -1;
    if (d.npages != oldpages) [self.page resize];
    else {
        [self redrawChanged];
        {   /* the chart of a table being edited */
            int t = [self caretTable];
            if (t >= 0 && d.a.tbls[t].chart && d.cbox[t].page >= 0) {
                NSRect pr = [self.page pageRect:d.cbox[t].page];
                double sc = [self.page scale];
                [self.page setNeedsDisplayInRect:NSMakeRect(pr.origin.x + d.cbox[t].x * sc, pr.origin.y + d.cbox[t].y * sc,
                                                            d.cbox[t].w * sc, d.cbox[t].h * sc)];
            }
        }
        /* where the caret was (Tab adding a table row moves it away) */
        if (!NSIsEmptyRect(oldCaret)) [self.page setNeedsDisplayInRect:oldCaret];
    }
    oldN = -1;
    [self keepLines];
    (void)from;
    [self.page showCaret];
    if (d.view != VIEW_DOC) [self rebuildCodes];
    [self noteDirty];
    [self updateInfo];
    [self updateToolbar];
    [self updateRulers];
}

- (int)caretTable
{
    long e;
    if (d.len <= 0) return -1;
    e = doc_para_end(&d, d.cursor < d.len ? d.cursor : d.len - 1);
    return PARA_OF(&d, &d.text[e < d.len ? e : d.len - 1])->tbl - 1;
}

- (void)changed
{
    undo_commit(&d, 0);
    doc_layout(&d);
    d.want_x = -1;
    if (d.cursor > d.len) d.cursor = d.len;
    doc_sync_ins(&d);
    ew_img_cache_clear();
    [self.page resize];
    [self keepLines];
    if (d.view != VIEW_DOC) [self rebuildCodes];
    [self noteDirty];
    [self updateInfo];
    [self updateToolbar];
}

- (void)moved
{
    if (d.anchor < 0) doc_sync_ins(&d);
    [self.page setNeedsDisplay:YES];
    [self.page showCaret];
    [self updateInfo];
    [self updateToolbar];
    [self updateRulers];
    [self keepLines];
}

- (void)selectFrom:(long)a to:(long)b
{
    d.anchor = a;
    d.cursor = b;
    if (d.anchor == d.cursor) d.anchor = -1;
    d.selobj = -1;
    [self moved];
}

- (void)updateInfo
{
    static const int jn[4] = { S_J_LEFT, S_J_CENTRE, S_J_RIGHT, S_J_FULL };
    CELL *c = &d.ins;
    PARA *p = doc_para_at(&d, d.cursor);
    LINE *ln = &d.lines[doc_find_line(&d, d.cursor)];
    NSMutableString *s = [NSMutableString string];
    [s appendFormat:@"%s  %d pt  ", fonts[c->font < nfonts ? c->font : 0].name, c->size];
    if (c->attr & A_BOLD) [s appendString:@"B"];
    if (c->attr & A_ITALIC) [s appendString:@"I"];
    if (c->attr & A_UNDER) [s appendString:@"U"];
    [s appendFormat:@"   %@", LS(jn[p->just & 3])];
    if (p->list) [s appendFormat:@"  %@", LS(p->list == LIST_BULLET ? S_I_BULLET : S_I_NUMBER)];
    if (p->toc) [s appendFormat:@"  H%d", p->toc];
    if (doc_in_table(&d, d.cursor, NULL, NULL)) [s appendFormat:@"  %@", LS(S_I_TABLE)];
    [s appendFormat:@"   %@ %d/%d   %d%%", LS(S_I_PAGE), ln->page + 1, doc_npages(&d), d.zoom];
    if (d.view == VIEW_CODES) [s appendString:LS(S_I_QUICK)];
    self.info.stringValue = s;
}

/* ---- the toolbar ------------------------------------------------------------------ */

- (NSArray *)toolbarDefaultItemIdentifiers:(NSToolbar *)t
{
    (void)t;
    return @[TB_UNDO, TB_REDO, NSToolbarSpaceItemIdentifier, TB_STYLE, TB_ALIGN, TB_LIST, TB_TABLE,
             NSToolbarSpaceItemIdentifier, TB_FONT, TB_SIZE, TB_COLOUR, NSToolbarFlexibleSpaceItemIdentifier,
             TB_ZOOM, TB_SPELL, TB_CODES, TB_RULER, TB_GUIDES];
}

- (NSArray *)toolbarAllowedItemIdentifiers:(NSToolbar *)t
{
    return [[self toolbarDefaultItemIdentifiers:t] arrayByAddingObjectsFromArray:
            @[NSToolbarFlexibleSpaceItemIdentifier, NSToolbarSpaceItemIdentifier]];
}

- (NSToolbarItem *)button:(NSString *)ident symbol:(NSString *)sym title:(NSString *)title tag:(int)tag
{
    NSToolbarItem *it = [[NSToolbarItem alloc] initWithItemIdentifier:ident];
    NSImage *img = symbol(sym, title);
    it.label = title;
    it.paletteLabel = title;
    it.toolTip = title;
    if (img) it.image = img;
    it.target = self;
    it.action = @selector(toolbarTap:);
    it.tag = tag;
    return it;
}

- (NSSegmentedControl *)segments:(NSArray *)syms labels:(NSArray *)labels any:(BOOL)any action:(SEL)a
{
    NSSegmentedControl *s = [[NSSegmentedControl alloc] initWithFrame:NSMakeRect(0, 0, 30 * syms.count, 24)];
    NSUInteger i;
    s.segmentCount = (NSInteger)syms.count;
    s.trackingMode = any ? NSSegmentSwitchTrackingSelectAny : NSSegmentSwitchTrackingSelectOne;
    for (i = 0; i < syms.count; i++) {
        NSImage *img = symbol(syms[i], labels[i]);
        if (img) [s setImage:img forSegment:(NSInteger)i];
        else [s setLabel:labels[i] forSegment:(NSInteger)i];
        [s setWidth:30 forSegment:(NSInteger)i];
        [s setToolTip:labels[i] forSegment:(NSInteger)i];
    }
    s.target = self;
    s.action = a;
    [s sizeToFit];
    return s;
}

- (NSToolbarItem *)viewItem:(NSString *)ident view:(NSView *)v label:(NSString *)l
{
    NSToolbarItem *it = [[NSToolbarItem alloc] initWithItemIdentifier:ident];
    it.view = v;
    it.label = l;
    it.paletteLabel = l;
    return it;
}

- (NSPopUpButton *)pop:(NSArray *)items action:(SEL)a width:(double)w
{
    NSPopUpButton *p = [[NSPopUpButton alloc] initWithFrame:NSMakeRect(0, 0, w, 24) pullsDown:NO];
    [p addItemsWithTitles:items];
    p.target = self;
    p.action = a;
    return p;
}

- (NSToolbarItem *)toolbar:(NSToolbar *)t itemForItemIdentifier:(NSString *)ident willBeInsertedIntoToolbar:(BOOL)f
{
    NSMutableArray *a = [NSMutableArray array];
    int i;
    (void)t; (void)f;
    if ([ident isEqual:TB_UNDO]) return [self button:ident symbol:@"arrow.uturn.backward" title:LSX(M_UNDO) tag:C_UNDO];
    if ([ident isEqual:TB_REDO]) return [self button:ident symbol:@"arrow.uturn.forward" title:LSX(M_REDO) tag:C_REDO];
    if ([ident isEqual:TB_TABLE]) return [self button:ident symbol:@"tablecells" title:LSX(M_TBLINS) tag:C_TBLINS];
    if ([ident isEqual:TB_SPELL]) return [self button:ident symbol:@"textformat.abc.dottedunderline" title:LSX(M_SPELL) tag:C_SPELL];
    if ([ident isEqual:TB_CODES]) return [self button:ident symbol:@"chevron.left.forwardslash.chevron.right" title:LSX(M_SPLIT) tag:C_SPLIT];
    if ([ident isEqual:TB_RULER]) return [self button:ident symbol:@"ruler" title:LSX(M_RULERS) tag:C_RULERS];
    if ([ident isEqual:TB_GUIDES]) return [self button:ident symbol:@"rectangle.dashed" title:LSX(M_GUIDES) tag:C_GUIDES];
    if ([ident isEqual:TB_STYLE]) {
        styleSeg = [self segments:@[@"bold", @"italic", @"underline"] labels:@[@"B", @"I", @"U"] any:YES action:@selector(styleTap:)];
        return [self viewItem:ident view:styleSeg label:LSX(MT_STYLE)];
    }
    if ([ident isEqual:TB_ALIGN]) {
        alignSeg = [self segments:@[@"text.alignleft", @"text.aligncenter", @"text.alignright", @"text.justify"]
                           labels:@[LSX(M_JLEFT), LSX(M_JCENTRE), LSX(M_JRIGHT), LSX(M_JFULL)] any:NO action:@selector(alignTap:)];
        return [self viewItem:ident view:alignSeg label:LSX(MT_PARA)];
    }
    if ([ident isEqual:TB_LIST]) {
        listSeg = [self segments:@[@"list.bullet", @"list.number"] labels:@[LSX(M_BULLETS), LSX(M_NUMBERS)] any:YES
                          action:@selector(listTap:)];
        return [self viewItem:ident view:listSeg label:LSX(M_BULLETS)];
    }
    if ([ident isEqual:TB_FONT]) {
        for (i = 0; i < nfonts; i++) [a addObject:[NSString stringWithUTF8String:fonts[i].name]];
        fontPop = [self pop:a action:@selector(fontTap:) width:150];
        return [self viewItem:ident view:fontPop label:LSX(MT_FONT)];
    }
    if ([ident isEqual:TB_SIZE]) {
        for (i = 0; i < NSIZES; i++) [a addObject:[NSString stringWithFormat:@"%d", sizes[i]]];
        [a addObject:LSX(M_SIZEOTHER)];
        sizePop = [self pop:a action:@selector(sizeTap:) width:70];
        return [self viewItem:ident view:sizePop label:LSX(S_SIZE)];
    }
    if ([ident isEqual:TB_COLOUR]) {
        for (i = 0; i < NCOLORS; i++) [a addObject:LS(S_COL_BLACK + i)];
        colourPop = [self pop:a action:@selector(colourTap:) width:100];
        for (i = 0; i < NCOLORS; i++) {
            NSImage *sw = [[NSImage alloc] initWithSize:NSMakeSize(12, 12)];
            [sw lockFocus];
            [ew_color(i) set];
            NSRectFill(NSMakeRect(0, 0, 12, 12));
            [sw unlockFocus];
            [colourPop itemAtIndex:i].image = sw;
        }
        return [self viewItem:ident view:colourPop label:LSX(S_TEXT_COLOUR)];
    }
    if ([ident isEqual:TB_ZOOM]) {
        for (i = 0; i < NZOOM; i++) [a addObject:[NSString stringWithFormat:@"%d%%", zooms[i]]];
        zoomPop = [self pop:a action:@selector(zoomTap:) width:80];
        return [self viewItem:ident view:zoomPop label:@"Zoom"];
    }
    return nil;
}

- (void)toolbarTap:(id)sender { [self command:(int)[sender tag]]; }
- (void)styleTap:(NSSegmentedControl *)s
{
    static const int cmd[3] = { C_BOLD, C_ITALIC, C_UNDER };
    [self command:cmd[s.selectedSegment]];
}
- (void)alignTap:(NSSegmentedControl *)s { [self command:C_JLEFT + (int)s.selectedSegment]; }
- (void)listTap:(NSSegmentedControl *)s { [self command:s.selectedSegment == 0 ? C_BULLETS : C_NUMBERS]; }
- (void)fontTap:(NSPopUpButton *)p { [self command:C_FONT0 + (int)p.indexOfSelectedItem]; }
- (void)sizeTap:(NSPopUpButton *)p
{
    NSInteger i = p.indexOfSelectedItem;
    [self command:i < NSIZES ? C_SIZE0 + (int)i : C_SIZEOTHER];
}
- (void)colourTap:(NSPopUpButton *)p { [self command:C_COLOR0 + (int)p.indexOfSelectedItem]; }
- (void)zoomTap:(NSPopUpButton *)p { [self setZoom:zooms[p.indexOfSelectedItem]]; }

- (BOOL)validateToolbarItem:(NSToolbarItem *)it
{
    BOOL dummy;
    if (it.tag) return [self validate:(int)it.tag checked:&dummy];
    return YES;
}

- (void)updateToolbar
{
    PARA *p = doc_para_at(&d, d.cursor);
    int i;
    [styleSeg setSelected:(d.ins.attr & A_BOLD) != 0 forSegment:0];
    [styleSeg setSelected:(d.ins.attr & A_ITALIC) != 0 forSegment:1];
    [styleSeg setSelected:(d.ins.attr & A_UNDER) != 0 forSegment:2];
    alignSeg.selectedSegment = p->just & 3;
    [listSeg setSelected:p->list == LIST_BULLET forSegment:0];
    [listSeg setSelected:p->list == LIST_NUMBER forSegment:1];
    if (d.ins.font < fontPop.numberOfItems) [fontPop selectItemAtIndex:d.ins.font];
    [sizePop selectItemAtIndex:NSIZES];
    for (i = 0; i < NSIZES; i++) if (sizes[i] == d.ins.size) [sizePop selectItemAtIndex:i];
    if (d.ins.size && sizePop && i == NSIZES) [sizePop itemAtIndex:NSIZES].title = [NSString stringWithFormat:@"%d", d.ins.size];
    [colourPop selectItemAtIndex:d.ins.color < NCOLORS ? d.ins.color : 0];
    for (i = 0; i < NZOOM; i++) if (zooms[i] == d.zoom) [zoomPop selectItemAtIndex:i];
    [self.win.toolbar validateVisibleItems];
}

/* ---- commands --------------------------------------------------------------------- */

- (void)applyStyle:(int)what value:(int)val
{
    long a, b;
    if ([self codesFocus]) {
        NSString *code = @"";
        switch (what) {
        case AP_FONT:   code = [NSString stringWithFormat:@"[Font %s]", fonts[val < nfonts ? val : 0].name]; break;
        case AP_SIZE:   code = [NSString stringWithFormat:@"[Size %d]", val]; break;
        case AP_COLOR:  code = [NSString stringWithFormat:@"[Colour %s]", color_name[val % NCOLORS]]; break;
        case AP_PLAIN:  code = @"[/B][/I][/U]"; break;
        case AP_TOGGLE: code = val == A_BOLD ? @"[B]" : val == A_ITALIC ? @"[I]" : @"[U]"; break;
        }
        [self insertCode:code];
        return;
    }
    if (d.cv.dirty) [self applyCodes];
    if (doc_sel(&d, &a, &b)) {
        int on = doc_apply(&d, a, b, what, val);
        switch (what) {
        case AP_FONT:   d.ins.font = (unsigned char)val; break;
        case AP_SIZE:   d.ins.size = (unsigned char)val; break;
        case AP_PLAIN:  d.ins.attr = 0; break;
        case AP_TOGGLE: if (on) d.ins.attr |= val; else d.ins.attr &= ~val; break;
        case AP_COLOR:  d.ins.color = (unsigned char)val; break;
        }
        [self keepLines];
        [self edited:doc_para_start(&d, a) typing:NO];
    } else {
        switch (what) {
        case AP_FONT:   d.ins.font = (unsigned char)val; break;
        case AP_SIZE:   d.ins.size = (unsigned char)val; break;
        case AP_PLAIN:  d.ins.attr = 0; break;
        case AP_TOGGLE: d.ins.attr ^= val; break;
        case AP_COLOR:  d.ins.color = (unsigned char)val; break;
        }
        if (d.len == 0) { doc_layout(&d); [self.page setNeedsDisplay:YES]; }
        [self updateInfo];
        [self updateToolbar];
    }
}

- (void)applyPara:(int)field value:(int)val
{
    long a, b;
    if ([self codesFocus]) {
        static NSString *jn[4] = { @"[Left]", @"[Centre]", @"[Right]", @"[Justify]" };
        NSString *code = @"";
        switch (field) {
        case PF_JUST: code = jn[val & 3]; break;
        case PF_SPACING: code = val == LS_DOUBLE ? @"[Spacing 2]" : val == LS_ONEHALF ? @"[Spacing 1.5]" : @"[Spacing 1]"; break;
        case PF_LIST: code = val == LIST_BULLET ? @"[Bullet]" : val == LIST_NUMBER ? @"[Number]" : @"[/List]"; break;
        case PF_TOC: code = val ? [NSString stringWithFormat:@"[Heading %d]", val] : @"[/Heading]"; break;
        }
        [self insertCode:code];
        return;
    }
    if (d.cv.dirty) [self applyCodes];
    if (!doc_sel(&d, &a, &b)) a = b = d.cursor;
    doc_para_set(&d, a, b, field, val);
    if (d.len == 0) d.insp = d.a.paras[d.ins.para];
    [self keepLines];
    [self edited:doc_para_start(&d, a) typing:NO];
}

- (void)typeString:(const char *)s
{
    long from = d.cursor;
    if ([self codesFocus]) { [self insertCode:st2ns((const unsigned char *)s, (long)strlen(s))]; return; }
    [self keepLines];
    if (doc_type_str(&d, s)) [self edited:from typing:NO];
}

- (void)insertBreak:(unsigned char)ch
{
    CELL c = d.ins;
    long from = d.cursor;
    if ([self codesFocus]) { [self insertCode:ch == CH_PAGE ? @"[Page]\n" : @"[Column]\n"]; return; }
    if (doc_in_table(&d, d.cursor, NULL, NULL)) { ew_alert(AL_NOT_IN_TABLE_BRK); return; }
    c.ch = ch;
    [self keepLines];
    if (doc_type(&d, &c, 1)) [self edited:from typing:NO];
}

- (void)table:(int)cmd
{
    long pos = d.cursor;
    int ok = 0;
    if (cmd != C_TBLINS && !doc_in_table(&d, pos, NULL, NULL)) { ew_alert(AL_NOT_IN_TABLE); return; }
    switch (cmd) {
    case C_TBLINS: {
        static int rows = 3, cols = 3, border = 1;
        if (!dlg_table(1, &rows, &cols, NULL, &border)) return;
        doc_delete_sel(&d);
        ok = tbl_insert(&d, d.cursor, rows, cols, border);
        break;
    }
    case C_TBLPROP: {
        int t = doc_in_table(&d, pos, NULL, NULL), rows = tbl_rows(&d, pos), cols, border;
        short w[MAX_COLS];
        TBL *T = &d.a.tbls[t - 1];
        cols = T->ncols;
        border = T->border;
        memcpy(w, T->w, sizeof w);
        if (!dlg_table(0, &rows, &cols, w, &border)) return;
        ok = tbl_resize(&d, pos, rows, cols, w, border);
        break;
    }
    case C_ROWINS: ok = tbl_row_insert(&d, pos, 1); break;
    case C_ROWDEL: ok = tbl_row_delete(&d, pos); break;
    case C_COLINS: ok = tbl_col_insert(&d, pos, 1); break;
    case C_COLDEL: ok = tbl_col_delete(&d, pos); break;
    case C_TBLDEL: ok = tbl_delete(&d, pos); break;
    }
    if (ok) [self changed];
    else if (cmd == C_COLINS) ew_alert(AL_MAX_COLS);
}

- (void)para:(int)cmd
{
    PARA *p = doc_para_at(&d, d.cursor);
    switch (cmd) {
    case C_JLEFT:   [self applyPara:PF_JUST value:J_LEFT]; break;
    case C_JCENTRE: [self applyPara:PF_JUST value:J_CENTRE]; break;
    case C_JRIGHT:  [self applyPara:PF_JUST value:J_RIGHT]; break;
    case C_JFULL:   [self applyPara:PF_JUST value:J_FULL]; break;
    case C_SP1:     [self applyPara:PF_SPACING value:LS_SINGLE]; break;
    case C_SP15:    [self applyPara:PF_SPACING value:LS_ONEHALF]; break;
    case C_SP2:     [self applyPara:PF_SPACING value:LS_DOUBLE]; break;
    case C_BULLETS: [self applyPara:PF_LIST value:p->list == LIST_BULLET ? LIST_NONE : LIST_BULLET]; break;
    case C_NUMBERS: [self applyPara:PF_LIST value:p->list == LIST_NUMBER ? LIST_NONE : LIST_NUMBER]; break;
    case C_HEAD1:   [self applyPara:PF_TOC value:1]; break;
    case C_HEAD2:   [self applyPara:PF_TOC value:2]; break;
    case C_HEAD3:   [self applyPara:PF_TOC value:3]; break;
    case C_BODY:    [self applyPara:PF_TOC value:0]; break;
    case C_HANGING: {
        int hang = p->first >= 0, left = hang ? (p->left > 0 ? p->left : 36) : 0;
        if ([self codesFocus]) { [self insertCode:hang ? @"[Indent 36 -36]" : @"[Indent 0 0]"]; break; }
        [self applyPara:PF_LEFT value:left];
        [self applyPara:PF_FIRST value:hang ? -left : 0];
        break;
    }
    case C_INDENTS: {
        int l = p->left, f = p->first;
        if (!dlg_indents(&l, &f)) break;
        if ([self codesFocus]) { [self insertCode:[NSString stringWithFormat:@"[Indent %d %d]", l, f]]; break; }
        [self applyPara:PF_LEFT value:l];
        [self applyPara:PF_FIRST value:f];
        break;
    }
    }
}

- (void)undo:(BOOL)redo
{
    if (d.cv.dirty) [self applyCodes];
    if (redo ? redo_do(&d) : undo_do(&d)) {
        doc_layout(&d);
        doc_sync_ins(&d);
        d.selobj = -1;
        ew_img_cache_clear();
        if (d.view != VIEW_DOC) [self rebuildCodes];
        [self.page resize];
        [self.page showCaret];
        [self keepLines];
        if (![self isDocumentEdited]) [self updateChangeCount:NSChangeDone];
        [self updateInfo];
        [self updateToolbar];
    }
}

- (void)objProps
{
    OBJ o;
    int r, i = d.selobj;
    if (i < 0 || i >= d.a.nobj) return;
    o = d.a.objs[i];
    r = dlg_objprops(&d, &o);
    if (r == 0) return;
    if (r == 2) { [self command:C_OBJDEL]; return; }
    d.a.objs[i] = o;
    d.dirty = 1;
    [self changed];
    if (r == 3) draw_edit(self, i);
}

- (void)toc
{
    long a = 0, b;
    int ok;
    if (toc_find(&d, &a, &b)) ok = toc_update(&d);
    else ok = toc_insert(&d, d.cursor, L(S_CONTENTS));
    if (ok) [self changed];
    else ew_alert(AL_NO_TOC_HERE);
}

- (void)exportText
{
    int crlf, cs;
    NSSavePanel *sp;
    if (!dlg_textexport(&crlf, &cs)) return;
    sp = [NSSavePanel savePanel];
    sp.nameFieldStringValue = [[[self baseName] stringByDeletingPathExtension] stringByAppendingPathExtension:@"txt"];
    if ([sp runModal] != NSModalResponseOK) return;
    if (!text_export(&d, sp.URL.path.fileSystemRepresentation, crlf, cs)) ew_alert(AL_CANT_WRITE);
}

/* Markdown, 1st Word Plus, Protext straight to a file */
- (void)exportAs:(int)fmt
{
    static NSString *ext[] = { @"rtf", @"txt", @"md", @"doc", @"pro" };
    NSSavePanel *sp = [NSSavePanel savePanel];
    sp.nameFieldStringValue = [[[self baseName] stringByDeletingPathExtension] stringByAppendingPathExtension:ext[fmt]];
    if ([sp runModal] != NSModalResponseOK) return;
    if (d.cv.dirty) [self applyCodes];
    if (!doc_write(&d, sp.URL.path.fileSystemRepresentation, fmt, 0, CS_UTF8)) ew_alert(AL_CANT_WRITE);
}

/* Previous page, Next page, Go to page: the caret to the page's first
 * line and that page's top into view */
- (void)gotoPage:(int)page
{
    NSRect r;
    int np = doc_npages(&d);
    if (!d.nlines) return;
    if (d.view == VIEW_CODES) [self setView:VIEW_DOC];
    if (page < 0) page = 0;
    if (page >= np) page = np - 1;
    d.anchor = -1;
    d.cursor = d.lines[d.pgline[page]].start;
    d.want_x = -1;
    r = [self.page pageRect:page];
    [self.page scrollPoint:NSMakePoint(self.pageScroll.contentView.bounds.origin.x, MAX(0, r.origin.y - 8))];
    [self moved];
}

/* ---- rulers ---------------------------------------------------------------------- */

static NSImage *marker_image(int up)
{
    NSImage *i = [[NSImage alloc] initWithSize:NSMakeSize(9, 6)];
    NSBezierPath *p = [NSBezierPath bezierPath];
    [i lockFocus];
    if (up) { [p moveToPoint:NSMakePoint(4.5, 6)]; [p lineToPoint:NSMakePoint(0, 0)]; [p lineToPoint:NSMakePoint(9, 0)]; }
    else { [p moveToPoint:NSMakePoint(4.5, 0)]; [p lineToPoint:NSMakePoint(0, 6)]; [p lineToPoint:NSMakePoint(9, 6)]; }
    [p closePath];
    [[NSColor colorWithCalibratedWhite:0.25 alpha:1] set];
    [p fill];
    [i unlockFocus];
    return i;
}

/* the rulers measure in centimetres on the page, whatever the zoom */
static NSString *ruler_units(int zoom)
{
    static BOOL done;
    int i;
    if (!done) {
        done = YES;
        for (i = 0; i < NZOOM; i++)
            [NSRulerView registerUnitWithName:[NSString stringWithFormat:@"EdWord%d", zooms[i]]
                                 abbreviation:@"cm" unitToPointsConversionFactor:72.0 / 2.54 * zooms[i] / 100.0
                                  stepUpCycle:@[@2.0] stepDownCycle:@[@0.5, @0.2]];
    }
    return [NSString stringWithFormat:@"EdWord%d", zoom];
}

- (void)showRulers
{
    self.pageScroll.hasHorizontalRuler = YES;
    self.pageScroll.hasVerticalRuler = YES;
    self.pageScroll.horizontalRulerView.clientView = self.page;
    self.pageScroll.verticalRulerView.clientView = self.page;
    self.pageScroll.horizontalRulerView.reservedThicknessForMarkers = 8;
    self.pageScroll.verticalRulerView.reservedThicknessForMarkers = 0;
    self.pageScroll.rulersVisible = pref_rulers != 0;
    [self updateRulers];
    [self.page setNeedsDisplay:YES];
}

- (void)updateRulers
{
    NSRulerView *h = self.pageScroll.horizontalRulerView, *v = self.pageScroll.verticalRulerView;
    LINE *ln;
    PARA *p;
    PGFMT f;
    NSRect pr;
    double s, colx, colw;
    NSMutableArray *mk;
    if (!pref_rulers || !h || !d.nlines) return;
    if (!h.clientView) h.clientView = self.page;
    if (!v.clientView) v.clientView = self.page;
    ln = &d.lines[doc_find_line(&d, d.cursor)];
    p = doc_para_at(&d, d.cursor);
    page_fmt(&d, ln->page, &f);
    pr = [self.page pageRect:ln->page];
    s = [self.page scale];
    h.measurementUnits = ruler_units(d.zoom);
    v.measurementUnits = ruler_units(d.zoom);
    h.originOffset = pr.origin.x;
    v.originOffset = pr.origin.y;
    mk = [NSMutableArray array];
    colx = ln->x - ln->tx0;
    colw = (pt2px_x(PS(&d).w - f.ml - f.mr) - (f.cols > 1 ? pt2px_x(f.colgap) * (f.cols - 1) : 0)) / (f.cols > 1 ? f.cols : 1);
    if (!(ln->flags & LF_CELL)) {
        [mk addObject:[[NSRulerMarker alloc] initWithRulerView:h markerLocation:pr.origin.x + (colx + pt2px_x(p->left + p->first)) * s
                                                          image:marker_image(0) imageOrigin:NSMakePoint(4.5, 0)]];
        [mk addObject:[[NSRulerMarker alloc] initWithRulerView:h markerLocation:pr.origin.x + (colx + pt2px_x(p->left)) * s
                                                          image:marker_image(1) imageOrigin:NSMakePoint(4.5, 0)]];
        [mk addObject:[[NSRulerMarker alloc] initWithRulerView:h markerLocation:pr.origin.x + (colx + colw) * s
                                                          image:marker_image(1) imageOrigin:NSMakePoint(4.5, 0)]];
    } else {
        int t = doc_in_table(&d, d.cursor, NULL, NULL), c;
        if (t > 0 && t <= d.a.ntbl) {
            TBL *T = &d.a.tbls[t - 1];
            double cx = pt2px_x(f.ml);
            for (c = 0; c <= T->ncols; c++) {
                [mk addObject:[[NSRulerMarker alloc] initWithRulerView:h markerLocation:pr.origin.x + cx * s
                                                                  image:marker_image(1) imageOrigin:NSMakePoint(4.5, 0)]];
                if (c < T->ncols) cx += pt2px_x(T->w[c]);
            }
        }
    }
    for (NSRulerMarker *m in mk) m.movable = NO;
    if (h.clientView) h.markers = mk;
    [h setNeedsDisplay:YES];
    [v setNeedsDisplay:YES];
}

- (void)exportPDF
{
    NSSavePanel *sp = [NSSavePanel savePanel];
    sp.nameFieldStringValue = [[[self baseName] stringByDeletingPathExtension] stringByAppendingPathExtension:@"pdf"];
    if ([sp runModal] != NSModalResponseOK) return;
    if (![self exportPDFTo:sp.URL]) ew_alert(AL_CANT_PDF);
}

- (BOOL)exportPDFTo:(NSURL *)url
{
    if (d.cv.dirty) [self applyCodes];
    return [ew_print_op(self, url) runOperation];
}

/* the Import panel: a loaded document in at the caret */
- (BOOL)insertLoaded:(DOC *)t
{
    long from = d.cursor, a, b;
    if (d.cv.dirty) [self applyCodes];
    if (doc_sel(&d, &a, &b)) from = a;
    [self keepLines];
    if (!t->len || !doc_insert_doc(&d, t, 0, t->len)) return NO;
    [self edited:from typing:NO];
    return YES;
}

/* the Import panel: a file read into a new, untitled window */
EWDocument *ew_open_import(const char *path, int type, int cs, int join)
{
    NSError *err = nil;
    EWDocument *doc = [[NSDocumentController sharedDocumentController] openUntitledDocumentAndDisplay:NO error:&err];
    DOC *d;
    if (!doc) return nil;
    d = [doc doc];
    doc_free(d);
    doc_init(d);
    d->zoom = 100;
    d->toolbar = pref_toolbar;
    d->cv.cols = 80;
    if (!doc_read(d, path, type, cs, join)) { [doc close]; return nil; }
    doc_layout(d);
    undo_reset(d);
    d->dirty = 1;
    [doc makeWindowControllers];
    [doc showWindows];
    [doc updateChangeCount:NSChangeDone];
    return doc;
}

- (void)insertFile
{
    NSOpenPanel *op = [NSOpenPanel openPanel];
    DOC t;
    long from = d.cursor;
    if ([op runModal] != NSModalResponseOK) return;
    doc_init(&t);
    t.ins = d.ins;
    if (!rtf_import(&t, op.URL.path.fileSystemRepresentation)) { ew_alert(AL_CANT_READ); doc_free(&t); return; }
    [self keepLines];
    if (t.len && doc_insert_doc(&d, &t, 0, t.len)) [self edited:from typing:NO];
    doc_free(&t);
}

- (void)insertDate:(int)style
{
    char s[48];
    fmt_now(s, style);
    [self typeString:s];
}

- (int)caretPage { return d.nlines ? d.lines[doc_find_line(&d, d.cursor)].page : 0; }

- (void)command:(int)cmd
{
    long a, b;
    if (app_command(cmd)) return;
    if (cmd >= C_FONT0 && cmd < C_FONT0 + MAX_FONTS) { [self applyStyle:AP_FONT value:cmd - C_FONT0]; return; }
    if (cmd >= C_SIZE0 && cmd < C_SIZE0 + NSIZES) { [self applyStyle:AP_SIZE value:sizes[cmd - C_SIZE0]]; return; }
    if (cmd >= C_COLOR0 && cmd < C_COLOR0 + NCOLORS) { [self applyStyle:AP_COLOR value:cmd - C_COLOR0]; return; }
    if (cmd >= C_JLEFT && cmd <= C_BODY) { [self para:cmd]; return; }
    if (cmd >= C_TBLINS && cmd <= C_TBLDEL) { if (d.cv.dirty) [self applyCodes]; [self table:cmd]; return; }
    switch (cmd) {
    case C_UNDO: [self undo:NO]; return;
    case C_REDO: [self undo:YES]; return;
    case C_PLAIN:  [self applyStyle:AP_PLAIN value:0]; return;
    case C_BOLD:   [self applyStyle:AP_TOGGLE value:A_BOLD]; return;
    case C_ITALIC: [self applyStyle:AP_TOGGLE value:A_ITALIC]; return;
    case C_UNDER:  [self applyStyle:AP_TOGGLE value:A_UNDER]; return;
    case C_SIZEOTHER: { int sz = d.ins.size; if (dlg_size(&sz)) [self applyStyle:AP_SIZE value:sz]; return; }
    case C_FONTSEL: {
        NSFontManager *fm = [NSFontManager sharedFontManager];
        [fm setSelectedFont:ew_font(d.ins.font, d.ins.size, d.ins.attr) isMultiple:NO];
        [fm orderFrontFontPanel:self];
        return;
    }
    case C_INSDATE: [self insertDate:pref_date]; return;
    case C_INSTIME: [self insertDate:DT_TIME]; return;
    case C_PGBRK: [self insertBreak:CH_PAGE]; return;
    case C_COLBRK: [self insertBreak:CH_COLBRK]; return;
    case C_VDOC: [self setView:VIEW_DOC]; return;
    case C_SPLIT: [self setView:d.view == VIEW_SPLIT ? VIEW_DOC : VIEW_SPLIT]; return;
    case C_VCODES: [self setView:d.view == VIEW_CODES ? VIEW_DOC : VIEW_CODES]; return;
    case C_ZOOMIN: case C_ZOOMOUT: {
        int i;
        for (i = 0; i < NZOOM; i++) if (zooms[i] == d.zoom) break;
        if (i == NZOOM) i = 2;
        i += cmd == C_ZOOMIN ? 1 : -1;
        if (i >= 0 && i < NZOOM) [self setZoom:zooms[i]];
        return;
    }
    case C_ZOOM100: [self setZoom:100]; return;
    case C_TOOLBAR:
        d.toolbar = !d.toolbar;
        self.win.toolbar.visible = d.toolbar;
        return;
    case C_FIND: find_open(self); return;
    case C_FINDNEXT: find_again(self); return;
    case C_SPELL: spell_run(self); return;
    case C_THES: thes_run(self); return;
    case C_SUMMARY: dlg_summary(&d, [self baseName]); return;
    }
    if (d.cv.dirty) [self applyCodes];
    switch (cmd) {
    case C_EXPTXT: [self exportText]; break;
    case C_EXPMD: [self exportAs:FT_MD]; break;
    case C_EXPFWP: [self exportAs:FT_FWP]; break;
    case C_EXPPT: [self exportAs:FT_PT]; break;
    case C_PREVPAGE: [self gotoPage:[self caretPage] - 1]; break;
    case C_NEXTPAGE: [self gotoPage:[self caretPage] + 1]; break;
    case C_GOTOPAGE: { int pg = [self caretPage]; if (dlg_goto(&d, &pg)) [self gotoPage:pg]; break; }
    case C_PDF: [self exportPDF]; break;
    case C_PAGE: if (dlg_page(&d, [self caretPage])) [self changed]; break;
    case C_WRAPCOL: if (dlg_wrapcol(&d)) [self changed]; break;
    case C_HEADFOOT: if (dlg_headfoot(&d)) [self changed]; break;
    case C_PAGENUM: if (dlg_pagenum(&d)) [self changed]; break;
    case C_REPAG: doc_repaginate(&d); [self changed]; break;
    case C_INSFILE: [self insertFile]; break;
    case C_TOC: [self toc]; break;
    case C_PICTURE: {
        NSOpenPanel *op = [NSOpenPanel openPanel];
        op.allowedFileTypes = @[@"img", @"IMG", @"png", @"jpg", @"jpeg", @"tif", @"tiff", @"gif", @"bmp"];
        if ([op runModal] == NSModalResponseOK) insert_picture(self, op.URL);
        break;
    }
    case C_DRAWING: {
        OBJ o;
        int i;
        if (d.a.nobj >= MAX_OBJS) { ew_alert(AL_TOO_MANY_OBJS); break; }
        memset(&o, 0, sizeof o);
        o.type = OB_DRAW; o.img = -1; o.wrap = WR_BOX;
        o.page = (short)[self caretPage];
        o.w = 170; o.h = 113;
        o.x = (short)((PS(&d).w - o.w) / 2);
        o.y = (short)px2pt_y(d.lines[doc_find_line(&d, d.cursor)].py + d.lines[doc_find_line(&d, d.cursor)].lh);
        if (o.y + o.h > PS(&d).h - PS(&d).mb) o.y = (short)(PS(&d).h - PS(&d).mb - o.h);
        i = doc_obj_add(&d, &o);
        if (i < 0) break;
        d.selobj = i;
        d.anchor = -1;
        [self changed];
        draw_edit(self, i);
        break;
    }
    case C_OBJPROP: [self objProps]; break;
    case C_OBJEDIT: if (d.selobj >= 0) draw_edit(self, d.selobj); break;
    default:
        if (cmd >= C_CH0 && cmd <= C_CPLAST) {        /* the caret's table's chart */
            int t = [self caretTable];
            TBL *T;
            if (t < 0) { ew_alert(AL_CHART_TABLE); break; }
            T = &d.a.tbls[t];
            if (cmd < C_CP0) T->chart = (unsigned char)(cmd - C_CH0);
            else { T->cpos = (unsigned char)(cmd - C_CP0); if (!T->chart) T->chart = CH_BAR; }
            d.dirty = 1;
            [self changed];
        }
        break;
    case C_OBJDEL:
        if (d.selobj >= 0 && d.selobj < d.a.nobj) {
            doc_obj_delete(&d, d.selobj);
            d.selobj = -1;
            [self changed];
        }
        break;
    }
    (void)a; (void)b;
}

- (BOOL)validate:(int)cmd checked:(BOOL *)ck
{
    PARA *p = doc_para_at(&d, d.cursor);
    int intbl = doc_in_table(&d, d.cursor, NULL, NULL) != 0, i = app_validate(cmd, ck);
    if (i >= 0) return i;
    *ck = NO;
    if (cmd >= C_FONT0 && cmd < C_FONT0 + MAX_FONTS) { *ck = cmd - C_FONT0 == d.ins.font; return YES; }
    if (cmd >= C_SIZE0 && cmd < C_SIZE0 + NSIZES) { *ck = sizes[cmd - C_SIZE0] == d.ins.size; return YES; }
    if (cmd >= C_COLOR0 && cmd < C_COLOR0 + NCOLORS) { *ck = cmd - C_COLOR0 == d.ins.color; return YES; }
    switch (cmd) {
    case C_UNDO: return undo_can(&d) || d.ch_lo >= 0;
    case C_REDO: return redo_can(&d);
    case C_BOLD: *ck = (d.ins.attr & A_BOLD) != 0; break;
    case C_ITALIC: *ck = (d.ins.attr & A_ITALIC) != 0; break;
    case C_UNDER: *ck = (d.ins.attr & A_UNDER) != 0; break;
    case C_JLEFT: *ck = p->just == J_LEFT; break;
    case C_JCENTRE: *ck = p->just == J_CENTRE; break;
    case C_JRIGHT: *ck = p->just == J_RIGHT; break;
    case C_JFULL: *ck = p->just == J_FULL; break;
    case C_SP1: *ck = p->spacing == LS_SINGLE; break;
    case C_SP15: *ck = p->spacing == LS_ONEHALF; break;
    case C_SP2: *ck = p->spacing == LS_DOUBLE; break;
    case C_HANGING: *ck = p->first < 0; break;
    case C_BULLETS: *ck = p->list == LIST_BULLET; break;
    case C_NUMBERS: *ck = p->list == LIST_NUMBER; break;
    case C_HEAD1: case C_HEAD2: case C_HEAD3: *ck = p->toc == cmd - C_HEAD1 + 1; break;
    case C_BODY: *ck = p->toc == 0; break;
    case C_TBLINS: return !intbl;
    case C_TBLPROP: case C_ROWINS: case C_ROWDEL: case C_COLINS: case C_COLDEL: case C_TBLDEL: return intbl;
    case C_VDOC: *ck = d.view == VIEW_DOC; break;
    case C_SPLIT: *ck = d.view == VIEW_SPLIT; break;
    case C_VCODES: *ck = d.view == VIEW_CODES; break;
    case C_TOOLBAR: *ck = d.toolbar; break;
    case C_PREVPAGE: return [self caretPage] > 0;
    case C_NEXTPAGE: return [self caretPage] < doc_npages(&d) - 1;
    case C_ZOOMIN: return d.zoom < zooms[NZOOM - 1];
    case C_ZOOMOUT: return d.zoom > zooms[0];
    case C_OBJPROP: case C_OBJDEL: return d.selobj >= 0;
    case C_OBJEDIT: return d.selobj >= 0 && d.selobj < d.a.nobj && d.a.objs[d.selobj].type == OB_DRAW;
    default:
        if (cmd >= C_CH0 && cmd <= C_CPLAST) {
            int t = [self caretTable];
            if (t < 0) return NO;
            if (cmd < C_CP0) *ck = d.a.tbls[t].chart == cmd - C_CH0;
            else *ck = d.a.tbls[t].chart && d.a.tbls[t].cpos == cmd - C_CP0;
        }
        break;
    }
    (void)i;
    return YES;
}

- (void)ewCommand:(id)sender { [self command:(int)[sender tag]]; }

- (BOOL)validateMenuItem:(NSMenuItem *)m
{
    if (m.action == @selector(ewCommand:)) {
        BOOL ck, ok = [self validate:(int)m.tag checked:&ck];
        m.state = ck ? NSControlStateValueOn : NSControlStateValueOff;
        return ok;
    }
    return [super validateMenuItem:m];
}
@end
