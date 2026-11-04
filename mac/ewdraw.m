/* Copyright (c) 2026 Francis C Bullen. SPDX-License-Identifier: MIT (see LICENSE) */
/*
 * ewdraw.m - fonts, metrics and page drawing for the macOS front end
 *
 * Pages are drawn in layout units with the page's top left corner at the
 * origin, in a flipped context; the caller sets the scale. The same code
 * draws the editing view, printed pages and the PDF.
 */
#import "ewmac.h"

/* ---- strings -------------------------------------------------------------- */

NSString *LS(int id)
{
    const char *s = L(id);
    NSString *r = s ? [NSString stringWithUTF8String:s] : nil;
    return r ? r : @"";
}

NSString *LSX(int id)
{
    NSString *s = LS(id);
    if ([s hasSuffix:@"..."]) s = [s substringToIndex:s.length - 3];
    if ([s hasSuffix:@":"]) s = [s substringToIndex:s.length - 1];
    return s;
}

NSString *st2ns(const unsigned char *s, long n)
{
    unichar *u = malloc((n + 1) * sizeof(unichar));
    long i;
    NSString *r;
    for (i = 0; i < n; i++) u[i] = (unichar)st_to_uni(s[i]);
    r = [NSString stringWithCharacters:u length:(NSUInteger)n];
    free(u);
    return r;
}

NSString *cells2ns(DOC *d, long a, long b)
{
    unichar *u;
    long i, k = 0;
    NSString *r;
    if (b <= a) return @"";
    u = malloc((b - a + 1) * sizeof(unichar));
    for (i = a; i < b; i++) {
        unsigned char c = d->text[i].ch;
        u[k++] = c == CH_CELL ? '\t' : ISBRK(c) ? '\n' : (unichar)st_to_uni(c);
    }
    r = [NSString stringWithCharacters:u length:(NSUInteger)k];
    free(u);
    return r;
}

unsigned char ch2st(unichar c)
{
    if (c == '\r') return '\n';
    return uni_to_st(c);
}

int ns2st(NSString *s, char *out, int max)
{
    NSUInteger i, n = s.length;
    int k = 0;
    for (i = 0; i < n && k < max - 1; i++) {
        unsigned char c = ch2st([s characterAtIndex:i]);
        if (c) out[k++] = (char)c;
    }
    out[k] = 0;
    return k;
}

/* ---- fonts ---------------------------------------------------------------- */

double units_per_pt(void) { return 25400.0 / 72.0 / dev_pixw; }

void fonts_init(void)
{
    /* font 0 plays the part of the Atari's system font: monospaced */
    static const char *want[] = {
        "Menlo", "Helvetica", "Times New Roman", "Courier New", "Georgia", "Palatino", "Avenir Next",
        "Gill Sans", "Optima", "Futura", "Baskerville", "Hoefler Text", "American Typewriter",
        "Verdana", "Arial", "Charter", "Didot", "Trebuchet MS", NULL };
    int i;
    dev_pixw = dev_pixh = 88;
    nfonts = 0;
    for (i = 0; want[i] && nfonts < MAX_FONTS; i++) {
        NSString *n = [NSString stringWithUTF8String:want[i]];
        if (![NSFont fontWithName:n size:12] && i > 0) continue;
        strncpy(fonts[nfonts].name, want[i], sizeof fonts[0].name - 1);
        fonts[nfonts].id = (short)nfonts;
        fonts[nfonts].outline = 1;
        nfonts++;
    }
}

int font_index(NSString *family)
{
    char n[34];
    int i;
    strncpy(n, family.UTF8String, sizeof n - 1);
    n[sizeof n - 1] = 0;
    for (i = 0; i < nfonts; i++) if (!strcasecmp(fonts[i].name, n)) return i;
    if (nfonts >= MAX_FONTS) return -1;
    strcpy(fonts[nfonts].name, n);
    fonts[nfonts].id = (short)nfonts;
    fonts[nfonts].outline = 1;
    return nfonts++;
}

NSFont *ew_font(int font, double size, int attr)
{
    static NSMutableDictionary *cache;
    NSString *key, *name;
    NSFont *f;
    if (!cache) cache = [NSMutableDictionary dictionary];
    if (font < 0 || font >= nfonts) font = 0;
    if (size < 1) size = 1;
    key = [NSString stringWithFormat:@"%d/%.2f/%d", font, size, attr & 3];
    f = cache[key];
    if (f) return f;
    name = [NSString stringWithUTF8String:fonts[font].name];
    f = [[NSFontManager sharedFontManager] fontWithFamily:name traits:0 weight:5 size:size];
    if (!f) f = [NSFont fontWithName:name size:size];
    if (!f) f = [NSFont userFontOfSize:size];
    if (attr & A_BOLD) f = [[NSFontManager sharedFontManager] convertFont:f toHaveTrait:NSBoldFontMask];
    if (attr & A_ITALIC) {
        NSFont *it = [[NSFontManager sharedFontManager] convertFont:f toHaveTrait:NSItalicFontMask];
        if (it) f = it;
    }
    cache[key] = f;
    return f;
}

static NSDictionary *font_attrs(NSFont *f, NSColor *c)
{
    return @{ NSFontAttributeName: f, NSForegroundColorAttributeName: c, NSLigatureAttributeName: @0,
              NSKernAttributeName: @0 };
}

/* the core asks for each character's width at a point size */
const METRICS *get_metrics(int font, int size, int attr)
{
    typedef struct { int key; METRICS m; } MC;
    static MC **cache;
    static int ncache, capc;
    int key = (font << 16) | (size << 8) | (attr & 3), i, c;
    NSFont *f;
    NSDictionary *a;
    MC *e;
    for (i = 0; i < ncache; i++) if (cache[i]->key == key) return &cache[i]->m;
    if (ncache == capc) {
        capc = capc ? capc * 2 : 64;
        cache = realloc(cache, capc * sizeof(MC *));
    }
    e = calloc(1, sizeof(MC));     /* never freed: the core keeps pointers */
    e->key = key;
    f = ew_font(font, size * units_per_pt(), attr);
    a = font_attrs(f, [NSColor blackColor]);
    for (c = 32; c < 256; c++) {
        unichar u = (unichar)st_to_uni((unsigned char)c);
        NSString *s = [NSString stringWithCharacters:&u length:1];
        e->m.w[c] = (short)lround([s sizeWithAttributes:a].width);
    }
    e->m.asc = (short)ceil(f.ascender);
    e->m.desc = (short)ceil(-f.descender);
    cache[ncache++] = e;
    return &e->m;
}

NSColor *ew_color(int c)
{
    if (c < 0 || c >= NCOLORS) c = 0;
    return [NSColor colorWithCalibratedRed:color_rgb[c][0] / 255.0 green:color_rgb[c][1] / 255.0
                                      blue:color_rgb[c][2] / 255.0 alpha:1];
}

/* ---- spelling marks --------------------------------------------------------- */

int word_misspelt(DOC *d, long a, long b)
{
    char w[WORDMAX];
    long i;
    if (b - a < 2 || b - a >= WORDMAX) return 0;
    if (!spell_main_loaded() && !spell_nuser) return 0;
    for (i = a; i < b; i++) w[i - a] = (char)d->text[i].ch;
    w[b - a] = 0;
    return !spell_check(w);
}

static int wch(unsigned char c) { return st_isalpha(c) || (c >= '0' && c <= '9'); }

static void live_marks(DOC *d, long li, double ox, double base, long caret)
{
    LWALK w;
    long ws = -1, wx = 0;
    double u = units_per_pt();
    lw_init(&w, d, li);
    for (;;) {
        int at_end = w.k >= w.end || ISEND(d->text[w.k].ch);
        unsigned char c = at_end ? ' ' : d->text[w.k].ch;
        int in = wch(c) || (c == '\'' && ws >= 0 && w.k + 1 < w.end && wch(d->text[w.k + 1].ch));
        if (in && ws < 0) { ws = w.k; wx = w.x; }
        if (!in && ws >= 0) {
            if (caret != w.k && word_misspelt(d, ws, w.k)) {
                NSBezierPath *p = [NSBezierPath bezierPath];
                double x, x1 = ox + wx, x2 = ox + w.x, y = base + 2 * u, st = 1.5 * u;
                int up = 0;
                [p moveToPoint:NSMakePoint(x1, y)];
                for (x = x1 + st; x <= x2; x += st) { [p lineToPoint:NSMakePoint(x, y + (up ? 0 : st))]; up = !up; }
                p.lineWidth = 0.7 * u;
                [[NSColor systemRedColor] set];
                [p stroke];
            }
            ws = -1;
        }
        if (at_end) break;
        lw_step(&w);
    }
}

/* ---- pictures and drawings ------------------------------------------------------ */

static NSMutableDictionary *imgcache;

void ew_img_cache_clear(void) { [imgcache removeAllObjects]; }

static NSImage *img_of(const IMG *im)
{
    NSValue *key = [NSValue valueWithPointer:im->bits];
    NSImage *i;
    NSBitmapImageRep *r;
    long x, y;
    if (!imgcache) imgcache = [NSMutableDictionary dictionary];
    i = imgcache[key];
    if (i) return i;
    if (im->rgb) {                              /* in colour */
        r = [[NSBitmapImageRep alloc] initWithBitmapDataPlanes:NULL pixelsWide:im->w pixelsHigh:im->h
                                                 bitsPerSample:8 samplesPerPixel:4 hasAlpha:YES isPlanar:NO
                                                colorSpaceName:NSCalibratedRGBColorSpace bytesPerRow:im->w * 4
                                                  bitsPerPixel:32];
        for (y = 0; y < im->h; y++) {
            unsigned char *row = r.bitmapData + y * r.bytesPerRow;
            const unsigned char *src = im->rgb + y * im->w * 3;
            for (x = 0; x < im->w; x++) {
                row[4 * x] = src[3 * x]; row[4 * x + 1] = src[3 * x + 1]; row[4 * x + 2] = src[3 * x + 2]; row[4 * x + 3] = 255;
            }
        }
        i = [[NSImage alloc] initWithSize:NSMakeSize(im->w, im->h)];
        [i addRepresentation:r];
        imgcache[key] = i;
        return i;
    }
    r = [[NSBitmapImageRep alloc] initWithBitmapDataPlanes:NULL pixelsWide:im->w pixelsHigh:im->h
                                             bitsPerSample:8 samplesPerPixel:2 hasAlpha:YES isPlanar:NO
                                            colorSpaceName:NSCalibratedWhiteColorSpace bytesPerRow:im->w * 2
                                              bitsPerPixel:16];
    for (y = 0; y < im->h; y++) {
        unsigned char *row = r.bitmapData + y * r.bytesPerRow;
        for (x = 0; x < im->w; x++) {
            int ink = (im->bits[y * im->rb + (x >> 3)] >> (7 - (x & 7))) & 1;
            row[2 * x] = ink ? 0 : 255;
            row[2 * x + 1] = ink ? 255 : 0;      /* white is transparent */
        }
    }
    i = [[NSImage alloc] initWithSize:NSMakeSize(im->w, im->h)];
    [i addRepresentation:r];
    imgcache[key] = i;
    return i;
}

/* a picture for the clipboard */
NSData *ew_img_tiff(DOC *d, int obj)
{
    const OBJ *o = &d->a.objs[obj];
    if (o->type != OB_IMG || o->img < 0 || o->img >= d->nimg) return nil;
    return [img_of(&d->imgs[o->img]) TIFFRepresentation];
}

void ew_draw_shape(const SHAPE *s, double ox, double oy)
{
    long xy[2 * 64];
    double x0 = ox + pt2px_x(s->x1), y0 = oy + pt2px_y(s->y1), x1 = ox + pt2px_x(s->x2), y1 = oy + pt2px_y(s->y2);
    int n, k;
    NSBezierPath *p = [NSBezierPath bezierPath];
    NSColor *c = ew_color(s->color);
    double lw = s->lw * units_per_pt();
    if (s->type == SH_LINE) {
        if (!s->lw) return;
        [p moveToPoint:NSMakePoint(x0, y0)];
        [p lineToPoint:NSMakePoint(x1, y1)];
        p.lineWidth = lw;
        [c set];
        [p stroke];
        return;
    }
    n = shape_pts(s->type, (long)(x0 * 16), (long)(y0 * 16), (long)(x1 * 16), (long)(y1 * 16), xy);
    for (k = 0; k < n; k++) {
        NSPoint pt = NSMakePoint(xy[2 * k] / 16.0, xy[2 * k + 1] / 16.0);
        if (k == 0) [p moveToPoint:pt]; else [p lineToPoint:pt];
    }
    [p closePath];
    if (s->fill == FL_WHITE) { [[NSColor whiteColor] set]; [p fill]; }
    else if (s->fill == FL_GREY) { [[c blendedColorWithFraction:0.55 ofColor:[NSColor whiteColor]] set]; [p fill]; }
    else if (s->fill == FL_SOLID) { [c set]; [p fill]; }
    if (s->lw) { p.lineWidth = lw; p.lineJoinStyle = NSRoundLineJoinStyle; [c set]; [p stroke]; }
}

void ew_draw_obj(DOC *d, const OBJ *o)
{
    NSRect r = NSMakeRect(pt2px_x(o->x), pt2px_y(o->y), pt2px_x(o->w), pt2px_y(o->h));
    int k;
    if (o->type == OB_IMG && o->img >= 0 && o->img < d->nimg) {
        NSImage *i = img_of(&d->imgs[o->img]);
        [i drawInRect:r fromRect:NSZeroRect operation:NSCompositingOperationSourceOver fraction:1
       respectFlipped:YES hints:nil];
    }
    if (o->type == OB_DRAW) {
        for (k = 0; k < o->nsh; k++) ew_draw_shape(&o->sh[k], r.origin.x, r.origin.y);
        if (!o->nsh) {
            NSBezierPath *p = [NSBezierPath bezierPathWithRect:r];
            CGFloat dash[2] = { 4, 4 };
            [p setLineDash:dash count:2 phase:0];
            p.lineWidth = units_per_pt() * 0.5;
            [[NSColor grayColor] set];
            [p stroke];
        }
    }
    if (o->border) {
        NSBezierPath *p = [NSBezierPath bezierPathWithRect:r];
        p.lineWidth = units_per_pt() * 0.75;
        [[NSColor blackColor] set];
        [p stroke];
    }
}

/* ---- text ---------------------------------------------------------------- */

static void text_at(const char *s, int n, double x, double base, NSFont *f, NSColor *c)
{
    NSString *str = st2ns((const unsigned char *)s, n);
    [str drawAtPoint:NSMakePoint(x, base - f.ascender) withAttributes:font_attrs(f, c)];
}

static void hline(double x1, double x2, double y, double w, NSColor *c)
{
    [c set];
    NSRectFill(NSMakeRect(x1, y, x2 - x1, w));
}

/* line li; ox is the x of the line's own left edge, base its baseline */
static void draw_line(DOC *d, long li, double ox, double base)
{
    LWALK w;
    LINE *ln = &d->lines[li];
    char buf[132];
    double u = units_per_pt();

    if (ln->mark && ln->len) {
        CELL st = d->text[ln->start];
        const METRICS *m = get_metrics(st.font, st.size, st.attr);
        NSColor *c = ew_color(st.color);
        if (ln->mark < 0) {
            double r = m->asc / 6.0 + u, mx = ox + ln->markx - ln->x;
            [c set];
            [[NSBezierPath bezierPathWithOvalInRect:NSMakeRect(mx + u, base - m->asc / 3.0 - r, 2 * r, 2 * r)] fill];
        } else {
            doc_marker(d, li, buf);
            text_at(buf, (int)strlen(buf), ox + doc_marker_x(d, li) - ln->x, base,
                    ew_font(st.font, st.size * u, st.attr), c);
        }
    }
    lw_init(&w, d, li);
    while (w.k < w.end) {
        CELL *c = &d->text[w.k], st;
        long sx;
        int n = 0;
        if (ISEND(c->ch)) break;
        if (c->ch == '\t' && w.k - ln->start == ln->tabk) {      /* dot leader */
            double x1 = ox + w.x, x2 = ox + w.x + ln->tabw - 4 * u, step = 6 * u, xx;
            [ew_color(c->color) set];
            for (xx = (floor((x1 - ox) / step) + 1) * step + ox; xx < x2; xx += step)
                NSRectFill(NSMakeRect(xx, base - u, u * 0.9, u * 0.9));
        }
        if (c->ch == ' ' || c->ch == '\t') { lw_step(&w); continue; }
        st = *c;
        sx = w.x;
        while (w.k < w.end && n < 128) {
            c = &d->text[w.k];
            if (ISEND(c->ch) || c->ch == ' ' || c->ch == '\t' || c->font != st.font || c->size != st.size ||
                ((c->attr ^ st.attr) & (A_BOLD | A_ITALIC)) || c->color != st.color)
                break;
            buf[n++] = (char)c->ch;
            lw_step(&w);
        }
        text_at(buf, n, ox + sx, base, ew_font(st.font, st.size * u, st.attr), ew_color(st.color));
    }
    /* underlines run unbroken under spaces */
    lw_init(&w, d, li);
    while (w.k < w.end) {
        CELL *c = &d->text[w.k];
        long ux;
        int col;
        double uy, t;
        if (ISEND(c->ch)) break;
        if (!(c->attr & A_UNDER)) { lw_step(&w); continue; }
        ux = w.x;
        col = c->color;
        uy = get_metrics(c->font, c->size, c->attr)->desc / 2.0;
        t = c->size * u / 16.0;
        while (w.k < w.end && !ISEND(d->text[w.k].ch) && (d->text[w.k].attr & A_UNDER) && d->text[w.k].color == col)
            lw_step(&w);
        hline(ox + ux, ox + w.x, base + uy, t, ew_color(col));
    }
}

static void draw_row(DOC *d, TROW *t, int dotted)
{
    const TBL *T = &d->a.tbls[t->tbl];
    double x = t->x, y0 = t->y, y1 = t->y + t->h, k = 0, u = units_per_pt();
    int i;
    NSBezierPath *p = [NSBezierPath bezierPath];
    for (i = 0; i < T->ncols; i++) k += pt2px_x(T->w[i]);
    [p moveToPoint:NSMakePoint(x, y0)]; [p lineToPoint:NSMakePoint(x + k, y0)];
    [p moveToPoint:NSMakePoint(x, y1)]; [p lineToPoint:NSMakePoint(x + k, y1)];
    [p moveToPoint:NSMakePoint(x, y0)]; [p lineToPoint:NSMakePoint(x, y1)];
    for (i = 0, k = 0; i < T->ncols; i++) {
        k += pt2px_x(T->w[i]);
        [p moveToPoint:NSMakePoint(x + k, y0)]; [p lineToPoint:NSMakePoint(x + k, y1)];
    }
    p.lineWidth = 0.6 * u;
    if (dotted) {
        CGFloat dash[2] = { u, 2 * u };
        [p setLineDash:dash count:2 phase:0];
        [[NSColor colorWithCalibratedWhite:0.6 alpha:1] set];
    } else [[NSColor blackColor] set];
    [p stroke];
}

static void draw_hf(DOC *d, int page)
{
    char buf[120];
    int i, k, f = default_font();
    const METRICS *m = get_metrics(f, HF(d).size, 0);
    PGFMT g;
    long tw, mx, mt, mb, ph = page_h_px(d), lh = m->asc + m->desc, w, x, base;
    page_fmt(d, page, &g);
    tw = text_w_px(d, page); mx = pt2px_x(g.ml); mt = pt2px_y(g.mt); mb = pt2px_y(g.mb);
    for (i = 0; i < 2; i++) {
        if (!hf_visible(d, i, page)) continue;
        hf_expand(d, i, page, doc_npages(d), pref_date, buf, sizeof buf);
        for (w = 0, k = 0; buf[k]; k++) w += m->w[(unsigned char)buf[k]];
        x = mx + (HF(d).just[i] == J_CENTRE ? (tw - w) / 2 : HF(d).just[i] == J_RIGHT ? tw - w : 0);
        if (i == 0) base = mt > lh ? (mt - lh) / 2 + m->asc : m->asc;
        else base = ph - (mb > lh ? (mb + lh) / 2 - m->asc : lh) + m->asc;
        text_at(buf, (int)strlen(buf), x, base, ew_font(f, HF(d).size * units_per_pt(), 0), [NSColor blackColor]);
    }
}

/* selection highlight for line li */
static void draw_sel(DOC *d, long li, long a, long b)
{
    LINE *ln = &d->lines[li];
    double x1, x2;
    if (ln->start + ln->len <= a && ln->len) return;
    if (ln->start >= b && !(ln->len == 0 && ln->start == a)) return;
    x1 = ln->start >= a ? ln->x + ln->xoff : doc_x_of(d, li, a);
    if (ln->start + ln->len > b) x2 = doc_x_of(d, li, b);
    else {
        x2 = doc_x_of(d, li, ln->start + ln->len);
        if (ln->len && ISEND(d->text[ln->start + ln->len - 1].ch)) x2 += 6 * units_per_pt();
    }
    if (x2 <= x1) return;
    [[NSColor selectedTextBackgroundColor] set];
    NSRectFill(NSMakeRect(x1, ln->py, x2 - x1, ln->asc + ln->desc));
}

/* a table's chart, from the list of polygons, lines and labels chart.c makes */
static void draw_chart(DOC *d, int t)
{
    static CHOP op[CH_MAXOPS];
    const CHBOX *cb = &d->cbox[t];
    NSFont *font = [NSFont systemFontOfSize:CHART_PT * units_per_pt()];
    NSDictionary *at = @{ NSFontAttributeName: font, NSForegroundColorAttributeName: [NSColor blackColor] };
    int n = chart_ops(d, t, cb->w, cb->h, op, CH_MAXOPS), i, k;
    double lw = units_per_pt() * 0.5;
    for (i = 0; i < n; i++) {
        const CHOP *o = &op[i];
        if (o->op == CO_TEXT) {
            NSString *s = st2ns((const unsigned char *)o->text, (long)strlen(o->text));
            NSSize sz = [s sizeWithAttributes:at];
            double x = cb->x + o->xy[0] - (o->align == 1 ? sz.width / 2 : o->align == 2 ? sz.width : 0);
            [s drawAtPoint:NSMakePoint(x, cb->y + o->xy[1] - font.ascender) withAttributes:at];
            continue;
        } else {
            NSBezierPath *p = [NSBezierPath bezierPath];
            for (k = 0; k < o->n; k++) {
                NSPoint pt = NSMakePoint(cb->x + o->xy[2 * k], cb->y + o->xy[2 * k + 1]);
                if (k == 0) [p moveToPoint:pt]; else [p lineToPoint:pt];
            }
            p.lineWidth = lw;
            if (o->op == CO_POLY) {
                NSColor *c = ew_color(chart_color(o->series));
                [p closePath];
                if (o->shade) c = [c blendedColorWithFraction:o->shade == 2 ? 0.35 : 0.18 ofColor:[NSColor blackColor]];
                [c set];
                [p fill];
            }
            [[NSColor blackColor] set];
            [p stroke];
        }
    }
}

void ew_draw_page(DOC *d, int page, NSRect clip, const DRAWOPT *o)
{
    long li, a, e, r;
    int i;
    double u = units_per_pt();
    if (page < 0 || page >= d->npages) return;
    [[NSColor whiteColor] set];
    NSRectFill(clip);
    a = d->pgline[page];
    e = d->pgline[page + 1];
    if (o && o->sa >= 0)
        for (li = a; li < e; li++) {
            LINE *ln = &d->lines[li];
            if (ln->py > NSMaxY(clip) || ln->py + ln->lh < NSMinY(clip)) continue;
            draw_sel(d, li, o->sa, o->sb);
        }
    for (i = 0; i < d->a.nobj; i++)
        if (d->a.objs[i].page == page) ew_draw_obj(d, &d->a.objs[i]);
    for (r = 0; r < d->nrows; r++) {
        TROW *t = &d->rows[r];
        if (t->page != page || t->tbl < 0 || t->tbl >= d->a.ntbl) continue;
        if (d->a.tbls[t->tbl].border) draw_row(d, t, 0);
        else if (o && o->live) draw_row(d, t, 1);
    }
    for (i = 0; i < d->a.ntbl; i++)
        if (d->a.tbls[i].chart && d->cbox[i].page == page && d->cbox[i].w > 0 &&
            NSIntersectsRect(clip, NSMakeRect(d->cbox[i].x, d->cbox[i].y, d->cbox[i].w, d->cbox[i].h)))
            draw_chart(d, i);
    for (li = a; li < e; li++) {
        LINE *ln = &d->lines[li];
        double base = ln->py + ln->asc;
        if (ln->py > NSMaxY(clip) || ln->py + ln->lh + ln->desc < NSMinY(clip)) continue;
        draw_line(d, li, ln->x, base);
        if (o && o->live && pref_live) live_marks(d, li, ln->x, base, o->sa < 0 ? o->caret : -1);
        if (o && o->live && (ln->flags & LF_FIRST) && ln->len) {
            const PARA *p = PARA_OF(d, &d->text[ln->start]);
            if (p->toc) {
                NSString *t = [NSString stringWithFormat:@"H%d", p->toc];
                [t drawAtPoint:NSMakePoint(4 * u, ln->py)
                withAttributes:@{ NSFontAttributeName: [NSFont boldSystemFontOfSize:7 * u],
                                  NSForegroundColorAttributeName: [NSColor systemBlueColor] }];
            }
        }
    }
    draw_hf(d, page);
    /* the caret */
    if (o && o->caret >= 0 && o->sa < 0) {
        long cl = doc_find_line(d, o->caret);
        LINE *ln = &d->lines[cl];
        if (ln->page == page) {
            [[NSColor blackColor] set];
            NSRectFill(NSMakeRect(doc_x_of(d, cl, o->caret), ln->py, u, ln->asc + ln->desc));
        }
    }
}

/* the chart items of the Insert menu and of a table's context menu */
void ew_add_chart_items(NSMenu *m)
{
    static const int txt[NCHARTS + 4] = { S_CH_NONE, S_CH_PIE, S_CH_BAR, S_CH_PIE3D, S_CH_BAR3D,
                                          S_CH_TOP, S_CH_BOTTOM, S_CH_LEFT, S_CH_RIGHT };
    int i;
    for (i = 0; i < NCHARTS + 4; i++) {
        NSMenuItem *it;
        if (i == NCHARTS) [m addItem:[NSMenuItem separatorItem]];
        it = [m addItemWithTitle:LS(txt[i]) action:NSSelectorFromString(@"ewCommand:") keyEquivalent:@""];
        it.tag = i < NCHARTS ? C_CH0 + i : C_CP0 + i - NCHARTS;
    }
}
