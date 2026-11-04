/* Copyright (c) 2026 Francis C Bullen. SPDX-License-Identifier: MIT (see LICENSE) */
/*
 * main.m - EdWord for macOS: the application, its menus and preferences
 */
#import "ewmac.h"

int pref_date = DT_LONG, pref_live = 1, pref_toolbar = 1, pref_paper = P_A4, pref_land = 0;
int pref_rulers = 0, pref_guides = 1;
static int pref_lang = -1;
int pref_spell = -1;

/* ---- preferences ------------------------------------------------------------- */

void prefs_load(void)
{
    NSUserDefaults *u = [NSUserDefaults standardUserDefaults];
    if ([u objectForKey:@"date"]) pref_date = (int)[u integerForKey:@"date"];
    if ([u objectForKey:@"liveSpell"]) pref_live = [u boolForKey:@"liveSpell"];
    if ([u objectForKey:@"toolbar"]) pref_toolbar = [u boolForKey:@"toolbar"];
    if ([u objectForKey:@"paper"]) pref_paper = (int)[u integerForKey:@"paper"];
    if ([u objectForKey:@"landscape"]) pref_land = [u boolForKey:@"landscape"];
    if ([u objectForKey:@"language"]) pref_lang = (int)[u integerForKey:@"language"];
    if ([u objectForKey:@"rulers"]) pref_rulers = [u boolForKey:@"rulers"];
    if ([u objectForKey:@"guides"]) pref_guides = [u boolForKey:@"guides"];
    if ([u objectForKey:@"dictionary"]) pref_spell = (int)[u integerForKey:@"dictionary"];
    if (pref_spell >= NLANG) pref_spell = -1;
    if (pref_paper < 0 || pref_paper >= NPAPER) pref_paper = P_A4;
}

void prefs_save(void)
{
    NSUserDefaults *u = [NSUserDefaults standardUserDefaults];
    [u setInteger:pref_date forKey:@"date"];
    [u setBool:pref_live forKey:@"liveSpell"];
    [u setBool:pref_toolbar forKey:@"toolbar"];
    [u setInteger:pref_paper forKey:@"paper"];
    [u setBool:pref_land forKey:@"landscape"];
    [u setBool:pref_rulers forKey:@"rulers"];
    [u setBool:pref_guides forKey:@"guides"];
    if (pref_lang >= 0) [u setInteger:pref_lang forKey:@"language"];
    [u setInteger:spell_lang forKey:@"dictionary"];
}

/* the first of the user's languages that EdWord knows */
static int system_language(void)
{
    static NSString *const codes[NLANG] = { @"en", @"de", @"es", @"it", @"fr" };
    for (NSString *l in [NSLocale preferredLanguages]) {
        int i;
        for (i = 0; i < NLANG; i++) if ([l hasPrefix:codes[i]]) return i;
    }
    return LANG_EN;
}

void set_language(int lang)
{
    static const char *mn[12], *dn[7];
    int i;
    ui_lang = lang >= 0 && lang < NLANG ? lang : 0;
    for (i = 0; i < 12; i++) mn[i] = L(S_MONTH1 + i);
    for (i = 0; i < 7; i++) dn[i] = L(S_DAY0 + i);
    date_months = mn;
    date_days = dn;
    if (lang != pref_lang && pref_lang >= 0) pref_lang = ui_lang;
}

/* ---- menus --------------------------------------------------------------------- */

static NSMenuItem *item(NSMenu *m, NSString *title, SEL action, NSString *key, NSUInteger mods, int tag)
{
    NSMenuItem *i = [m addItemWithTitle:title action:action keyEquivalent:key ? key : @""];
    if (key) i.keyEquivalentModifierMask = mods;
    i.tag = tag;
    return i;
}

static NSMenuItem *cmd(NSMenu *m, int text, int tag, NSString *key, NSUInteger mods)
{
    return item(m, LS(text), @selector(ewCommand:), key, mods, tag);
}

static NSMenu *submenu(NSMenu *bar, int title)
{
    NSMenuItem *top = [bar addItemWithTitle:LSX(title) action:NULL keyEquivalent:@""];
    NSMenu *m = [[NSMenu alloc] initWithTitle:LSX(title)];
    top.submenu = m;
    return m;
}

#define CMD NSEventModifierFlagCommand
#define SHF NSEventModifierFlagShift
#define OPT NSEventModifierFlagOption
#define CTL NSEventModifierFlagControl

static NSString *fkey(unichar c) { return [NSString stringWithCharacters:&c length:1]; }

/* The menus follow the macOS conventions: the application menu, File,
 * Edit, Insert, Format, View, Tools, Window and Help, with the standard
 * shortcuts. */
void menus_build(void)
{
    NSMenu *bar = [[NSMenu alloc] initWithTitle:@""], *m, *s;
    NSMenuItem *top;
    NSString *app = @"EdWord";
    char ret[2] = { '\r', 0 };
    NSString *rk = [NSString stringWithUTF8String:ret];
    int i;

    /* the application menu */
    top = [bar addItemWithTitle:app action:NULL keyEquivalent:@""];
    m = [[NSMenu alloc] initWithTitle:app];
    top.submenu = m;
    item(m, LSX(M_ABOUT), @selector(ewCommand:), nil, 0, C_ABOUT);
    [m addItem:[NSMenuItem separatorItem]];
    item(m, LS(M_PREFS), @selector(ewCommand:), @",", CMD, C_PREFS);
    [m addItem:[NSMenuItem separatorItem]];
    s = [[NSMenu alloc] initWithTitle:LS(S_MAC_SERVICES)];
    [m addItemWithTitle:LS(S_MAC_SERVICES) action:NULL keyEquivalent:@""].submenu = s;
    [NSApp setServicesMenu:s];
    [m addItem:[NSMenuItem separatorItem]];
    item(m, [NSString stringWithFormat:@"%@ %@", LS(S_MAC_HIDE), app], @selector(hide:), @"h", CMD, 0);
    item(m, LS(S_MAC_HIDEOTHERS), @selector(hideOtherApplications:), @"h", CMD | OPT, 0);
    item(m, LS(S_MAC_SHOWALL), @selector(unhideAllApplications:), nil, 0, 0);
    [m addItem:[NSMenuItem separatorItem]];
    item(m, [NSString stringWithFormat:@"%@ %@", LSX(M_QUIT), app], @selector(terminate:), @"q", CMD, 0);

    /* File */
    m = submenu(bar, MT_FILE);
    item(m, LS(M_NEW), @selector(newDocument:), @"n", CMD, 0);
    item(m, LS(M_OPEN), @selector(openDocument:), @"o", CMD, 0);
    s = [[NSMenu alloc] initWithTitle:LS(S_MAC_RECENT)];
    [s addItemWithTitle:LS(S_MAC_CLEAR) action:@selector(clearRecentDocuments:) keyEquivalent:@""];
    [m addItemWithTitle:LS(S_MAC_RECENT) action:NULL keyEquivalent:@""].submenu = s;
    [m addItem:[NSMenuItem separatorItem]];
    item(m, LS(M_CLOSE), @selector(performClose:), @"w", CMD, 0);
    item(m, LS(M_SAVEDOC), @selector(saveDocument:), @"s", CMD, 0);
    item(m, LS(M_SAVEAS), @selector(saveDocumentAs:), @"s", CMD | SHF, 0);
    item(m, LS(S_MAC_REVERT), @selector(revertDocumentToSaved:), nil, 0, 0);
    [m addItem:[NSMenuItem separatorItem]];
    cmd(m, M_IMPORT, C_IMPORT, @"i", CMD | SHF);
    cmd(m, M_EXPORT, C_EXPORT, @"e", CMD | SHF);
    [m addItem:[NSMenuItem separatorItem]];
    cmd(m, M_PAGE, C_PAGE, @"p", CMD | SHF);
    cmd(m, M_HEADFOOT, C_HEADFOOT, nil, 0);
    item(m, LS(S_MAC_PRINT), @selector(printDocument:), @"p", CMD, 0);

    /* Edit */
    m = submenu(bar, MT_EDIT);
    item(m, LS(M_UNDO), @selector(undo:), @"z", CMD, 0);
    item(m, LS(M_REDO), @selector(redo:), @"z", CMD | SHF, 0);
    [m addItem:[NSMenuItem separatorItem]];
    item(m, LS(M_CUT), @selector(cut:), @"x", CMD, 0);
    item(m, LS(M_COPY), @selector(copy:), @"c", CMD, 0);
    item(m, LS(M_PASTE), @selector(paste:), @"v", CMD, 0);
    item(m, LS(M_SELALL), @selector(selectAll:), @"a", CMD, 0);
    [m addItem:[NSMenuItem separatorItem]];
    cmd(m, M_FIND, C_FIND, @"f", CMD);
    cmd(m, M_FINDNEXT, C_FINDNEXT, @"g", CMD);

    /* Insert */
    m = submenu(bar, MT_INSERT);
    cmd(m, M_DATE, C_INSDATE, @"d", CMD | OPT);
    cmd(m, M_TIME, C_INSTIME, nil, 0);
    [m addItem:[NSMenuItem separatorItem]];
    cmd(m, M_PGBRK, C_PGBRK, rk, CMD);
    cmd(m, M_COLBRK, C_COLBRK, rk, CMD | SHF);
    cmd(m, M_PAGENUM, C_PAGENUM, nil, 0);
    [m addItem:[NSMenuItem separatorItem]];
    cmd(m, M_TBLINS, C_TBLINS, nil, 0);
    cmd(m, M_TBLPROP, C_TBLPROP, nil, 0);
    cmd(m, M_ROWINS, C_ROWINS, nil, 0);
    cmd(m, M_ROWDEL, C_ROWDEL, nil, 0);
    cmd(m, M_COLINS, C_COLINS, nil, 0);
    cmd(m, M_COLDEL, C_COLDEL, nil, 0);
    cmd(m, M_TBLDEL, C_TBLDEL, nil, 0);
    s = [[NSMenu alloc] initWithTitle:LSX(M_CHART)];
    [m addItemWithTitle:LSX(M_CHART) action:NULL keyEquivalent:@""].submenu = s;
    ew_add_chart_items(s);
    [m addItem:[NSMenuItem separatorItem]];
    item(m, LS(S_MAC_PICTURE), @selector(ewCommand:), nil, 0, C_PICTURE);
    cmd(m, M_DRAWING, C_DRAWING, nil, 0);
    [m addItem:[NSMenuItem separatorItem]];
    cmd(m, M_TOC, C_TOC, nil, 0);
    [m addItem:[NSMenuItem separatorItem]];
    cmd(m, M_INSFILE, C_INSFILE, nil, 0);

    /* Format: Font, Colour and Text, then the paragraph formats */
    top = [bar addItemWithTitle:LS(S_MAC_FORMAT) action:NULL keyEquivalent:@""];
    m = [[NSMenu alloc] initWithTitle:LS(S_MAC_FORMAT)];
    top.submenu = m;
    s = [[NSMenu alloc] initWithTitle:LSX(MT_FONT)];
    [m addItemWithTitle:LSX(MT_FONT) action:NULL keyEquivalent:@""].submenu = s;
    item(s, LS(S_MAC_SHOWFONTS), @selector(ewCommand:), @"t", CMD, C_FONTSEL);
    [s addItem:[NSMenuItem separatorItem]];
    cmd(s, M_PLAIN, C_PLAIN, nil, 0);
    cmd(s, M_BOLD, C_BOLD, @"b", CMD);
    cmd(s, M_ITALIC, C_ITALIC, @"i", CMD);
    cmd(s, M_UNDER, C_UNDER, @"u", CMD);
    [s addItem:[NSMenuItem separatorItem]];
    cmd(s, M_SIZEOTHER, C_SIZEOTHER, nil, 0);
    for (i = 0; i < NSIZES; i++)
        item(s, [NSString stringWithFormat:@"%d %@", sizes[i], LS(S_POINT)], @selector(ewCommand:), nil, 0, C_SIZE0 + i);
    [s addItem:[NSMenuItem separatorItem]];
    for (i = 0; i < nfonts; i++)
        item(s, [NSString stringWithUTF8String:fonts[i].name], @selector(ewCommand:), nil, 0, C_FONT0 + i);
    s = [[NSMenu alloc] initWithTitle:LS(S_MAC_COLOUR)];
    [m addItemWithTitle:LS(S_MAC_COLOUR) action:NULL keyEquivalent:@""].submenu = s;
    for (i = 0; i < NCOLORS; i++) item(s, LS(S_COL_BLACK + i), @selector(ewCommand:), nil, 0, C_COLOR0 + i);
    s = [[NSMenu alloc] initWithTitle:LS(S_MAC_TEXT)];
    [m addItemWithTitle:LS(S_MAC_TEXT) action:NULL keyEquivalent:@""].submenu = s;
    cmd(s, M_JLEFT, C_JLEFT, @"{", CMD);
    cmd(s, M_JCENTRE, C_JCENTRE, @"|", CMD);
    cmd(s, M_JFULL, C_JFULL, @"|", CMD | OPT);
    cmd(s, M_JRIGHT, C_JRIGHT, @"}", CMD);
    [s addItem:[NSMenuItem separatorItem]];
    cmd(s, M_SP1, C_SP1, nil, 0);
    cmd(s, M_SP15, C_SP15, nil, 0);
    cmd(s, M_SP2, C_SP2, nil, 0);
    [m addItem:[NSMenuItem separatorItem]];
    cmd(m, M_INDENTS, C_INDENTS, nil, 0);
    cmd(m, M_HANGING, C_HANGING, nil, 0);
    cmd(m, M_BULLETS, C_BULLETS, @"l", CMD | OPT);
    cmd(m, M_NUMBERS, C_NUMBERS, @"n", CMD | OPT);
    [m addItem:[NSMenuItem separatorItem]];
    cmd(m, M_HEAD1, C_HEAD1, @"1", CMD | OPT);
    cmd(m, M_HEAD2, C_HEAD2, @"2", CMD | OPT);
    cmd(m, M_HEAD3, C_HEAD3, @"3", CMD | OPT);
    cmd(m, M_BODY, C_BODY, @"0", CMD | OPT);
    [m addItem:[NSMenuItem separatorItem]];
    cmd(m, M_WRAPCOL, C_WRAPCOL, nil, 0);

    /* View */
    top = [bar addItemWithTitle:LS(S_MAC_VIEW) action:NULL keyEquivalent:@""];
    m = [[NSMenu alloc] initWithTitle:LS(S_MAC_VIEW)];
    top.submenu = m;
    cmd(m, M_VDOC, C_VDOC, @"1", CMD);
    cmd(m, M_SPLIT, C_SPLIT, @"2", CMD);
    cmd(m, M_VCODES, C_VCODES, @"3", CMD);
    [m addItem:[NSMenuItem separatorItem]];
    cmd(m, M_RULERS, C_RULERS, @"r", CMD);
    cmd(m, M_GUIDES, C_GUIDES, nil, 0);
    [m addItem:[NSMenuItem separatorItem]];
    item(m, LS(S_MAC_ACTUAL), @selector(ewCommand:), @"0", CMD, C_ZOOM100);
    cmd(m, M_ZOOMIN, C_ZOOMIN, @"+", CMD);
    cmd(m, M_ZOOMOUT, C_ZOOMOUT, @"-", CMD);
    [m addItem:[NSMenuItem separatorItem]];
    cmd(m, M_PREVPAGE, C_PREVPAGE, fkey(NSUpArrowFunctionKey), CMD | OPT);
    cmd(m, M_NEXTPAGE, C_NEXTPAGE, fkey(NSDownArrowFunctionKey), CMD | OPT);
    cmd(m, M_GOTOPAGE, C_GOTOPAGE, @"g", CMD | OPT);
    cmd(m, M_REPAG, C_REPAG, @"r", CMD | OPT);
    [m addItem:[NSMenuItem separatorItem]];
    cmd(m, M_TOOLBAR, C_TOOLBAR, @"t", CMD | OPT);

    /* Tools */
    m = submenu(bar, MT_TOOLS);
    cmd(m, M_SPELL, C_SPELL, @";", CMD);
    cmd(m, M_LIVESPELL, C_LIVESPELL, nil, 0);
    cmd(m, M_THES, C_THES, @"t", CMD | CTL);
    cmd(m, M_SUMMARY, C_SUMMARY, nil, 0);
    [m addItem:[NSMenuItem separatorItem]];
    cmd(m, M_UDLOAD, C_UDLOAD, nil, 0);
    cmd(m, M_UDNEW, C_UDNEW, nil, 0);
    [m addItem:[NSMenuItem separatorItem]];
    for (i = 0; i < MAX_UDICT; i++) item(m, LS(S_NO_UDICT), @selector(ewCommand:), nil, 0, C_UD0 + i);

    /* Window */
    m = submenu(bar, MT_WINDOW);
    item(m, LS(S_MAC_MINIMIZE), @selector(performMiniaturize:), @"m", CMD, 0);
    item(m, LS(S_MAC_ZOOMWIN), @selector(performZoom:), nil, 0, 0);
    [m addItem:[NSMenuItem separatorItem]];
    cmd(m, M_TILEV, C_TILEV, nil, 0);
    cmd(m, M_TILEH, C_TILEH, nil, 0);
    cmd(m, M_CASCADE, C_CASCADE, nil, 0);
    [m addItem:[NSMenuItem separatorItem]];
    item(m, LS(S_MAC_FRONT), @selector(arrangeInFront:), nil, 0, 0);
    [NSApp setWindowsMenu:m];

    /* Help */
    top = [bar addItemWithTitle:LS(S_MAC_HELP) action:NULL keyEquivalent:@""];
    m = [[NSMenu alloc] initWithTitle:LS(S_MAC_HELP)];
    top.submenu = m;
    item(m, LS(S_MAC_MANUAL), @selector(ewCommand:), @"?", CMD, C_HELP);
#ifndef GNUSTEP
    [NSApp setHelpMenu:m];
#endif

    [NSApp setMainMenu:bar];
}

/* ---- application commands ----------------------------------------------------------- */

void tile_windows(int how)
{
    NSMutableArray *w = [NSMutableArray array];
    NSRect sf = [NSScreen mainScreen].visibleFrame;
    NSUInteger i, n;
    for (NSDocument *d in [[NSDocumentController sharedDocumentController] documents])
        for (NSWindowController *wc in d.windowControllers)
            if ([wc.window isVisible] && ![wc.window isMiniaturized]) [w addObject:wc.window];
    n = w.count;
    for (i = 0; i < n; i++) {
        NSWindow *win = w[i];
        NSRect r;
        if (how == 0) r = NSMakeRect(sf.origin.x + i * sf.size.width / n, sf.origin.y, sf.size.width / n, sf.size.height);
        else if (how == 1) r = NSMakeRect(sf.origin.x, NSMaxY(sf) - (i + 1) * sf.size.height / n, sf.size.width, sf.size.height / n);
        else r = NSMakeRect(sf.origin.x + i * 26, sf.origin.y, sf.size.width * 0.7, sf.size.height - i * 26);
        [win setFrame:r display:YES animate:YES];
        [win orderFront:nil];
    }
}

static void about(void)
{
    NSMutableDictionary *o = [NSMutableDictionary dictionary];
    NSString *line = ED_ABOUT[0] ? [NSString stringWithUTF8String:ED_ABOUT] : LS(S_ABOUT_LINE1);
    NSString *credits = [NSString stringWithFormat:@"%@\n%@\n%@\n%@", line, LS(S_ABOUT_LINE2), LS(S_ABOUT_LINE3),
                         LS(S_ABOUT_THES)];
    o[@"ApplicationName"] = @"EdWord";
    o[@"ApplicationVersion"] = @ED_VERSION;
    o[@"Version"] = @ED_VERSION;
    o[@"Credits"] = [[NSAttributedString alloc] initWithString:credits
        attributes:@{ NSFontAttributeName: [NSFont systemFontOfSize:11] }];
    [NSApp orderFrontStandardAboutPanelWithOptions:o];
}

/* Help > EdWord Manual: the manual in the UI language, from the bundle */
static void open_manual(void)
{
    static const char *code[NLANG] = { "EN", "DE", "ES", "IT", "FR" };
    NSString *p = [[NSBundle mainBundle] pathForResource:[NSString stringWithFormat:@"MANUAL%s", code[ui_lang]] ofType:@"PDF"];
    if (!p) p = [[NSBundle mainBundle] pathForResource:@"MANUALEN" ofType:@"PDF"];
    if (p) [[NSWorkspace sharedWorkspace] openURL:[NSURL fileURLWithPath:p]];
}

static void refresh_all(void)
{
    for (NSDocument *d in [[NSDocumentController sharedDocumentController] documents])
        if ([d isKindOfClass:[EWDocument class]]) [((EWDocument *)d).page setNeedsDisplay:YES];
}

int app_command(int c)
{
    NSOpenPanel *op;
    NSSavePanel *sp;
    switch (c) {
    case C_ABOUT: about(); return 1;
    case C_PREFS: {
        int lang = ui_lang;
        if (dlg_prefs()) {
            if (lang != ui_lang) { pref_lang = ui_lang; prefs_save(); menus_build(); }
            if (pref_live) spell_ensure(0);
            refresh_all();
        }
        return 1;
    }
    case C_IMPORT: import_open(); return 1;
    case C_EXPORT: export_open(); return 1;
    case C_HELP: open_manual(); return 1;
    case C_RULERS: case C_GUIDES:
        if (c == C_RULERS) pref_rulers = !pref_rulers; else pref_guides = !pref_guides;
        prefs_save();
        for (NSDocument *d in [[NSDocumentController sharedDocumentController] documents])
            if ([d isKindOfClass:[EWDocument class]]) [(EWDocument *)d showRulers];
        return 1;
    case C_TILEV: tile_windows(0); return 1;
    case C_TILEH: tile_windows(1); return 1;
    case C_CASCADE: tile_windows(2); return 1;
    case C_LIVESPELL:
        pref_live = !pref_live;
        if (pref_live) spell_ensure(0);
        prefs_save();
        refresh_all();
        return 1;
    case C_UDLOAD:
        op = [NSOpenPanel openPanel];
        if ([op runModal] == NSModalResponseOK && spell_user_load(op.URL.path.fileSystemRepresentation, 0) < 0)
            ew_alert(AL_UD_LOAD);
        return 1;
    case C_UDNEW:
        sp = [NSSavePanel savePanel];
        sp.nameFieldStringValue = @"PERSONAL.UDC";
        if ([sp runModal] == NSModalResponseOK && spell_user_load(sp.URL.path.fileSystemRepresentation, 1) < 0)
            ew_alert(AL_UD_NEW);
        return 1;
    }
    if (c >= C_UD0 && c < C_UD0 + MAX_UDICT) { if (c - C_UD0 < spell_nuser) spell_active = c - C_UD0; return 1; }
    return 0;
}

static NSMenuItem *ud_item(int k)
{
    for (NSMenuItem *top in [NSApp mainMenu].itemArray)
        for (NSMenuItem *i in top.submenu.itemArray)
            if (i.tag == C_UD0 + k && i.action == @selector(ewCommand:)) return i;
    return nil;
}

int app_validate(int c, BOOL *ck)
{
    *ck = NO;
    if (c >= C_UD0 && c < C_UD0 + MAX_UDICT) {
        int k = c - C_UD0;
        NSMenuItem *i = ud_item(k);
        if (k < spell_nuser) i.title = [NSString stringWithUTF8String:spell_upath[k]].lastPathComponent;
        else i.title = LS(S_NO_UDICT);
        *ck = k == spell_active && k < spell_nuser;
        return k < spell_nuser;
    }
    switch (c) {
    case C_ABOUT: case C_PREFS: case C_IMPORT: case C_EXPORT: case C_TILEV: case C_TILEH: case C_CASCADE: case C_UDLOAD: case C_UDNEW:
    case C_HELP:
        return 1;
    case C_RULERS: *ck = pref_rulers; return 1;
    case C_GUIDES: *ck = pref_guides; return 1;
    case C_LIVESPELL: *ck = pref_live; return 1;
    }
    return -1;
}

/* ---- pictures ---------------------------------------------------------------- */

/* any picture macOS can read, in colour (and dithered for black and white) */
static int image_to_img(NSURL *url, IMG *im)
{
    NSImage *src = [[NSImage alloc] initWithContentsOfURL:url];
    NSBitmapImageRep *r;
    long w, h, maxd = 2000;
    int ok;
    if (!src) return 0;
    w = lround(src.size.width); h = lround(src.size.height);
    if (w < 1 || h < 1) return 0;
    if (w > maxd || h > maxd) {
        double k = (double)maxd / MAX(w, h);
        w = lround(w * k); h = lround(h * k);
    }
    r = [[NSBitmapImageRep alloc] initWithBitmapDataPlanes:NULL pixelsWide:w pixelsHigh:h bitsPerSample:8
                                           samplesPerPixel:3 hasAlpha:NO isPlanar:NO
                                            colorSpaceName:NSCalibratedRGBColorSpace bytesPerRow:w * 3 bitsPerPixel:24];
    [NSGraphicsContext saveGraphicsState];
    [NSGraphicsContext setCurrentContext:[NSGraphicsContext graphicsContextWithBitmapImageRep:r]];
    [[NSColor whiteColor] set];
    NSRectFill(NSMakeRect(0, 0, w, h));
    [src drawInRect:NSMakeRect(0, 0, w, h)];
    [NSGraphicsContext restoreGraphicsState];
    ok = img_from_rgb(im, w, h, r.bitmapData, r.bytesPerRow, 3);
    if (ok) strncpy(im->name, url.lastPathComponent.UTF8String, sizeof im->name - 1);
    return ok;
}

int insert_picture(EWDocument *doc, NSURL *url)
{
    DOC *d = [doc doc];
    IMG im;
    OBJ o;
    PGFMT f;
    LINE *ln;
    long w, h, tw;
    int k, i, ok;
    if (d->a.nobj >= MAX_OBJS || d->nimg >= MAX_IMGS) { ew_alert(AL_TOO_MANY_OBJS); return 0; }
    ok = img_load(url.path.fileSystemRepresentation, &im);      /* GEM IMG, PNG, IFF */
    if (!ok) ok = image_to_img(url, &im);                         /* anything else macOS reads */
    if (!ok) { ew_alert(AL_CANT_READ_IMG); return 0; }
    k = doc_img_add(d, &im);
    if (k < 0) { img_free(&im); return 0; }
    memset(&o, 0, sizeof o);
    o.type = OB_IMG;
    o.img = (short)k;
    o.wrap = WR_BOX;
    ln = &d->lines[doc_find_line(d, d->cursor)];
    page_fmt(d, ln->page, &f);
    /* pictures come in at 100 dpi */
    w = im.w * 72 / 100; h = im.h * 72 / 100;
    tw = PS(d).w - f.ml - f.mr;
    if (w > tw) { h = h * tw / w; w = tw; }
    if (h > PS(d).h - f.mt - f.mb) { w = w * (PS(d).h - f.mt - f.mb) / h; h = PS(d).h - f.mt - f.mb; }
    o.page = ln->page;
    o.w = (short)MAX(w, 6); o.h = (short)MAX(h, 6);
    o.x = (short)(f.ml + (tw - o.w) / 2);
    o.y = (short)px2pt_y(ln->py + ln->lh);
    if (o.y + o.h > PS(d).h - f.mb) o.y = (short)(PS(d).h - f.mb - o.h);
    if (o.y < f.mt) o.y = f.mt;
    i = doc_obj_add(d, &o);
    if (i < 0) return 0;
    d->selobj = i;
    d->anchor = -1;
    [doc changed];
    return 1;
}

/* ---- the application delegate -------------------------------------------------------- */

@interface EWApp : NSObject <NSApplicationDelegate>
- (void)ewCommand:(id)sender;
@end

@implementation EWApp
- (void)applicationWillFinishLaunching:(NSNotification *)n
{
    (void)n;
    prefs_load();
    set_language(pref_lang >= 0 ? pref_lang : system_language());
    spell_lang = pref_spell >= 0 ? pref_spell : ui_lang;
    fonts_init();
    menus_build();
}
- (void)applicationDidFinishLaunching:(NSNotification *)n
{
    (void)n;
    if (pref_live) spell_ensure(1);
}
- (BOOL)applicationShouldOpenUntitledFile:(NSApplication *)a { (void)a; return YES; }
- (void)applicationWillTerminate:(NSNotification *)n
{
    (void)n;
    prefs_save();
    spell_free_all();
    thes_close();
    doc_clip_free();
}
- (void)ewCommand:(id)sender { app_command((int)[sender tag]); }
- (BOOL)validateMenuItem:(NSMenuItem *)m
{
    BOOL ck;
    int r;
    if (m.action != @selector(ewCommand:)) return YES;
    r = app_validate((int)m.tag, &ck);
    m.state = ck ? NSControlStateValueOn : NSControlStateValueOff;
    return r > 0;
}
@end

int main(int argc, const char **argv)
{
    (void)argc; (void)argv;
    @autoreleasepool {
        NSApplication *app = [NSApplication sharedApplication];
        static EWApp *delegate;
        delegate = [[EWApp alloc] init];
        app.delegate = delegate;
        [app setActivationPolicy:NSApplicationActivationPolicyRegular];
        [app run];
    }
    return 0;
}
