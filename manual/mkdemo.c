/* Copyright (c) 2026 Francis C Bullen. SPDX-License-Identifier: MIT (see LICENSE) */
/* mkdemo.c - build the sample document used for the manual's screenshots
 * usage: mkdemo out.rtf   (built on a PC with the portable core) */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "wp.h"

static METRICS cache[16];
static int ckey[16], nc;

const METRICS *get_metrics(int font, int size, int attr)
{
    int k = (font << 16) | (size << 8) | (attr & 3), i, c;
    for (i = 0; i < nc; i++) if (ckey[i] == k) return &cache[i];
    if (nc == 16) nc = 0;
    ckey[nc] = k;
    for (c = 0; c < 256; c++) cache[nc].w[c] = (short)(c < 32 ? 0 : 8);
    cache[nc].asc = 13; cache[nc].desc = 3;
    return &cache[nc++];
}

static const char *codes =
    "[Centre][Size 20][B]Harbour Light Rowing Club[/B][Size 10]\n"
    "[Left]\n"
    "[Heading 1][Size 14][B]Welcome[/B][Size 10]\n"
    "[/Heading][Justify]The club meets every Saturday morning at the old boat house by the "
    "harbour wall. New members are always welcome, whether you have rowed before or not. "
    "This newsletter tells you what happens over the next season and who to ask when you "
    "want to join a crew. Please recieve it with our thanks for a wonderfull year.\n"
    "\n"
    "[Heading 2][B]What to bring[/B]\n"
    "[/Heading][Left][Bullet]Warm clothes in layers\n"
    "A water bottle\n"
    "Shoes that can get wet\n"
    "[/List]\n"
    "[Heading 2][B]Your first morning[/B]\n"
    "[/Heading][Number]Meet the coach at the boat house at nine.\n"
    "Learn to carry the boat and the oars.\n"
    "Row in the harbour with an experienced crew.\n"
    "[/List]\n"
    "[Heading 1][Size 14][B]The season[/B][Size 10]\n"
    "[/Heading][Table 130 100 130 Border][Row][B]Month[Cell]Event[Cell]Place[/B]\n"
    "[Row]April[Cell]Open day[Cell]Boat house\n"
    "[Row]June[Cell]Harbour race[Cell]Harbour wall\n"
    "[Row]September[Cell]Long row[Cell]Up the river\n"
    "[/Table][Left]\n"
    "[Indent 54 -54]Note:[Tab]The long row starts early. Crews leave at seven and are back "
    "in time for lunch at the club.\n"
    "[Indent 0 0][Spacing 2]This paragraph is double spaced, so there is room to write "
    "notes between its lines when it is printed.\n"
    "[Spacing 1][Page]\n"
    "[Heading 1][Size 14][B]News from the boat house[/B][Size 10]\n"
    "[/Heading][Justify]The boat house roof was mended over the winter and the old racks "
    "have been replaced. The new racks hold eight boats, so every crew now has a place for "
    "its boat and the oars no longer lean against the wall. Thanks go to everyone who gave "
    "a weekend to the work.\n"
    "The club has bought a second coaching launch. It will follow the beginners' crews on "
    "Saturday mornings and carry the first aid kit. Two members have passed their powerboat "
    "certificates and will take turns to drive it.\n"
    "[Column]"
    "The social evening in March raised enough money for a new set of oars. The quiz was won "
    "by the veterans' crew for the third year running, and the cake stall sold out within an "
    "hour. The next social evening is planned for the autumn.\n"
    "Junior rowing starts again in May for anyone aged twelve and over. Juniors row with a "
    "coach on Sunday afternoons, and parents are welcome to come and watch from the harbour "
    "wall.\n"
    "[B]Boats out on Saturdays[/B]\n"
    "[Table 70 60 60 Border][Row][B]Month[Cell]Members[Cell]Visitors[/B]\n"
    "[Row]April[Cell]18[Cell]12\n"
    "[Row]May[Cell]22[Cell]9\n"
    "[Row]June[Cell]25[Cell]15\n"
    "[Row]July[Cell]21[Cell]19\n"
    "[/Table]\n";

int main(int argc, char **argv)
{
    DOC d;
    PGFMT f;
    OBJ o;
    SHAPE *s;
    long a, b;
    if (argc < 2) { fprintf(stderr, "usage: mkdemo out.rtf\n"); return 1; }
    nfonts = 2;
    strcpy(fonts[0].name, "Swiss"); fonts[0].id = 1;
    strcpy(fonts[1].name, "Dutch"); fonts[1].id = 2;
    dev_pixw = dev_pixh = 278;
    doc_init(&d);
    d.ins.size = 10;
    d.cv.cols = 70;
    d.cv.t = strdup(codes);
    d.cv.len = d.cv.cap = (long)strlen(codes);
    d.cv.cap++;
    d.cv.dirty = 1;
    codes_apply(&d);
    doc_layout(&d);
    /* the contents after the title */
    find_next(&d, 0, "Welcome", 0, &b);
    a = find_next(&d, 0, "Welcome", 0, &b);
    toc_insert(&d, a, "Contents");
    doc_layout(&d);
    /* page 2: two columns */
    page_fmt(&d, 1, &f);
    f.cols = 2; f.colgap = 18;
    page_ovr_set(&d, 1, 0, &f);
    /* a drawing on page 2 that the text flows around */
    memset(&o, 0, sizeof o);
    o.type = OB_DRAW; o.page = 1; o.img = -1;
    o.x = 360; o.y = 130; o.w = 140; o.h = 110; o.wrap = WR_SHAPE;
    s = &o.sh[o.nsh++];
    s->type = SH_ELLIPSE; s->x1 = 4; s->y1 = 4; s->x2 = 136; s->y2 = 106; s->fill = FL_GREY; s->lw = 2;
    s = &o.sh[o.nsh++];
    s->type = SH_TRI; s->x1 = 40; s->y1 = 20; s->x2 = 100; s->y2 = 80; s->fill = FL_WHITE; s->lw = 2;
    s = &o.sh[o.nsh++];
    s->type = SH_LINE; s->x1 = 20; s->y1 = 90; s->x2 = 120; s->y2 = 90; s->lw = 3;
    doc_obj_add(&d, &o);
    strcpy(HF(&d).text[1], "Harbour Light Rowing Club - page {page} of {pages}");
    HF(&d).just[1] = J_CENTRE;
    HF(&d).size = 9;
    HF(&d).first = 1;
    doc_layout(&d);
    if (!rtf_export(&d, argv[1])) { fprintf(stderr, "cannot write %s\n", argv[1]); return 1; }
    printf("%s: %ld characters, %d pages\n", argv[1], d.len, doc_npages(&d));
    return 0;
}
