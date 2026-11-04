/* Copyright (c) 2026 Francis C Bullen. SPDX-License-Identifier: MIT (see LICENSE) */
/*
 * ewprint.m - printing and PDF export: a view with the pages one below
 * the other at their real size in points, paginated page by page. Text
 * stays text, so the PDF is sharp at any size and searchable.
 */
#import "ewmac.h"

@interface EWPrintView : NSView
@property (assign) DOC *d;
@end

@implementation EWPrintView
- (BOOL)isFlipped { return YES; }
- (double)pw { return page_w_px(self.d) / units_per_pt(); }
- (double)ph { return page_h_px(self.d) / units_per_pt(); }

- (BOOL)knowsPageRange:(NSRangePointer)r
{
    *r = NSMakeRange(1, (NSUInteger)doc_npages(self.d));
    return YES;
}

- (NSRect)rectForPage:(NSInteger)page
{
    return NSMakeRect(0, (page - 1) * [self ph], [self pw], [self ph]);
}

- (void)drawRect:(NSRect)dirty
{
    int p, n = doc_npages(self.d);
    double u = units_per_pt();
    for (p = 0; p < n; p++) {
        NSRect r = NSMakeRect(0, p * [self ph], [self pw], [self ph]);
        NSAffineTransform *t;
        if (!NSIntersectsRect(r, dirty)) continue;
        [NSGraphicsContext saveGraphicsState];
        NSRectClip(r);
        t = [NSAffineTransform transform];
        [t translateXBy:0 yBy:r.origin.y];
        [t scaleBy:1.0 / u];
        [t concat];
        ew_draw_page(self.d, p, NSMakeRect(0, 0, page_w_px(self.d), page_h_px(self.d)), NULL);
        [NSGraphicsContext restoreGraphicsState];
    }
}
@end

NSPrintOperation *ew_print_op(EWDocument *doc, NSURL *pdf)
{
    DOC *d = [doc doc];
    EWPrintView *v;
    NSPrintInfo *pi = [doc.printInfo copy];
    NSPrintOperation *op;
    doc_layout(d);
    v = [[EWPrintView alloc] initWithFrame:NSMakeRect(0, 0, page_w_px(d) / units_per_pt(),
                                                      doc_npages(d) * page_h_px(d) / units_per_pt())];
    v.d = d;
    /* the page size already has the orientation in it */
    pi.orientation = NSPaperOrientationPortrait;
    pi.paperSize = NSMakeSize(PS(d).w, PS(d).h);
    pi.topMargin = pi.bottomMargin = pi.leftMargin = pi.rightMargin = 0;
    pi.horizontalPagination = NSPrintingPaginationModeClip;
    pi.verticalPagination = NSPrintingPaginationModeAutomatic;
    pi.horizontallyCentered = NO;
    pi.verticallyCentered = NO;
    if (pdf) {
        pi.jobDisposition = NSPrintSaveJob;
        pi.dictionary[NSPrintJobSavingURL] = pdf;
    }
    op = [NSPrintOperation printOperationWithView:v printInfo:pi];
    op.jobTitle = doc.displayName;
    if (pdf) { op.showsPrintPanel = NO; op.showsProgressPanel = YES; }
    return op;
}
