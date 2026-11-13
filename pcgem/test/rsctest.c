/* Copyright (c) 2026 Francis C Bullen. SPDX-License-Identifier: MIT (see LICENSE) */
/* rsctest.c - load an EDWORD*.RSC with pgrsc.c on the build machine and check every tree */
#include <stdio.h>
#include <string.h>
#include <gem.h>
#include "edrsc.h"
short pg_wchar = 8, pg_hchar = 16, pg_scr_w = 640, pg_scr_h = 480;
int main(int argc, char **argv)
{
    int t, i, bad = 0, nstr = 0, nted = 0, nimg = 0;
    OBJECT *tr;
    if (!rsrc_load(argv[1])) { puts("load failed"); return 1; }
    for (t = 0; t < RT_COUNT; t++) {
        if (!rsrc_gaddr(R_TREE, t, &tr) || !tr) { printf("tree %d missing\n", t); bad++; continue; }
        for (i = 0;; i++) {
            OBJECT *o = &tr[i];
            int ty = o->ob_type & 0xff;
            if (ty < G_BOX || ty > G_CICON) { printf("tree %d obj %d bad type %d\n", t, i, ty); bad++; break; }
            if ((ty == G_STRING || ty == G_BUTTON || ty == G_TITLE)) { if (!o->ob_spec.free_string) { bad++; printf("t%d o%d no string\n",t,i);} else nstr++; }
            if (ty == G_TEXT || ty == G_BOXTEXT || ty == G_FTEXT || ty == G_FBOXTEXT) {
                TEDINFO *te = o->ob_spec.tedinfo;
                if (!te || !te->te_ptext || !te->te_ptmplt || !te->te_pvalid) { bad++; printf("t%d o%d bad ted\n",t,i);} else nted++;
            }
            if (ty == G_IMAGE) { if (!o->ob_spec.bitblk || !o->ob_spec.bitblk->bi_pdata) { bad++; printf("t%d o%d bad img\n",t,i);} else nimg++; }
            if (o->ob_flags & LASTOB) break;
        }
    }
    rsrc_gaddr(R_TREE, RT_MENU, &tr);
    printf("menu root %dx%d, title 1: '%s'\n", tr[0].ob_width, tr[0].ob_height, tr[tr[tr[0].ob_head].ob_head].ob_head > 0 ? tr[tr[tr[tr[0].ob_head].ob_head].ob_head].ob_spec.free_string : "?");
    rsrc_gaddr(R_TREE, RT_ABOUT, &tr);
    printf("about box %dx%d at %d,%d\n", tr[0].ob_width, tr[0].ob_height, tr[0].ob_x, tr[0].ob_y);
    printf("%d trees, %d strings, %d tedinfos, %d images, %d problems\n", RT_COUNT, nstr, nted, nimg, bad);
    return bad != 0;
}
