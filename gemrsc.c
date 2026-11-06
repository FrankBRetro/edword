/* Copyright (c) 2026 Francis C Bullen. SPDX-License-Identifier: MIT (see LICENSE) */
/*
 * gemrsc.c - EdWord's resource files: the menu bar, the Print and
 * Preferences dialogs, the font selector, the toolbar and its popups,
 * one EDWORDxx.RSC per language (made by tools/mkrsc.py).
 *
 * The dialogs in the file are laid out on a grid of character rows; the
 * extended object types mark how each object sits in its row, and the
 * rows are spread to EdWord's row height (one and a half characters)
 * when the file is loaded. Without the file, or on a screen too narrow
 * for its dialogs, EdWord builds the same things itself.
 */
#include <stdio.h>
#include <string.h>
#include "gemui.h"
#include "edrsc.h"

static const char *const rsc_file[NLANG] = { "EDWORD.RSC", "EDWORDDE.RSC", "EDWORDES.RSC", "EDWORDIT.RSC", "EDWORDFR.RSC" };
static int   rsc_lang = -1, rsc_loaded;
static short title_obj[RT_COUNT];

/* ext types from mkrsc.py */
enum { X_ROW = 1, X_GROUP = 2, X_BUTTON = 3, X_TITLE = 4 };

#ifdef ED_RSC
static void spread(int tree)
{
    OBJECT *t;
    short rh = (short)(hchar + hchar / 2), i;
    title_obj[tree] = -1;
    if (!rsrc_gaddr(R_TREE, tree, &t) || !t) return;
    for (i = 0;; i++) {
        OBJECT *o = &t[i];
        int ext = (o->ob_type >> 8) & 0xff;
        if (i == 0 || ext == X_GROUP) {
            o->ob_y = (short)(o->ob_y / hchar * rh);
            o->ob_height = (short)(o->ob_height / hchar * rh);
        } else if (ext == X_ROW || ext == X_TITLE)
            o->ob_y = (short)(o->ob_y / hchar * rh + (rh - hchar) / 2);
        else if (ext == X_BUTTON)
            o->ob_y = (short)(o->ob_y / hchar * rh + (rh - hchar) / 2 - 2);
        if (ext == X_TITLE) title_obj[tree] = i;
        o->ob_type &= 0xff;
        if (o->ob_flags & OF_LASTOB) break;
    }
}

static void clear_ext(int tree)
{
    OBJECT *t;
    short i;
    if (!rsrc_gaddr(R_TREE, tree, &t) || !t) return;
    for (i = 0;; i++) {
        t[i].ob_type &= 0xff;
        if (t[i].ob_flags & OF_LASTOB) break;
    }
}

#endif

/* load the file for a language (again, with force: fresh trees) */
int rsc_load(int lang, int force)
{
#ifndef ED_RSC
    /* built without resource files ("make RSC=1" uses them): EdWord's own
     * menus and dialogs, as everywhere a file isn't found */
    (void)lang; (void)force;
    return 0;
#else
    char path[300];
    int i;
    if (lang < 0 || lang >= NLANG) lang = 0;
    if (rsc_loaded && lang == rsc_lang && !force) return 1;
    if (rsc_loaded) { rsrc_free(); rsc_loaded = 0; }
    rsc_lang = lang;
    sprintf(path, "%s%s", progdir, rsc_file[lang]);
    if (!rsrc_load(path)) {
        sprintf(path, "%s%s", progdir, rsc_file[0]);   /* English, if this language's is missing */
        if (!rsrc_load(path)) { edlog("no resource file %s", path); return 0; }
    }
    rsc_loaded = 1;
    for (i = 0; i < RT_COUNT; i++) {
        if (i == RT_PRINT || i == RT_PREFS || i == RT_PREFS2) spread(i);
        else clear_ext(i);
    }
    edlog("resource file %s", path);
    return 1;
#endif
}

void rsc_free(void)
{
    if (rsc_loaded) rsrc_free();
    rsc_loaded = 0;
    rsc_lang = -1;
}

OBJECT *rsc_tree(int tree)
{
    OBJECT *t = NULL;
    if (!rsc_loaded || tree < 0 || tree >= RT_COUNT) return NULL;
    if (!rsrc_gaddr(R_TREE, tree, &t)) return NULL;
    return t;
}

short rsc_title(int tree) { return tree >= 0 && tree < RT_COUNT && rsc_loaded ? title_obj[tree] : -1; }

/* the text of a popup's entry, without the two leading spaces */
const char *rsc_entry(int tree, short obj)
{
    OBJECT *t = rsc_tree(tree);
    const char *s;
    if (!t) return "";
    s = t[obj].ob_spec.free_string;
    while (*s == ' ') s++;
    return s;
}
