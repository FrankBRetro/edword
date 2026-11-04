/* Copyright (c) 2026 Francis C Bullen. SPDX-License-Identifier: MIT (see LICENSE) */
/*
 * undo.c - multi-level undo and redo
 *
 * Every change to the text goes through doc_insert(), doc_delete() or
 * doc_touch(), which keep the range changed since the last step
 * (d->ch_lo, d->ch_hi). undo_commit() turns that range into a step: the
 * old cells come from a shadow copy of the text as it was, so a step
 * holds only what changed. Everything else that undo restores
 * (paragraph formats, tables, page set-up, pictures...) lives in d->a and
 * is compared with its own shadow; a step keeps copies only when it
 * changed. Typing merges into one step until a space or a pause in
 * typing (a call with typing = 0).
 */
#include <stdlib.h>
#include <string.h>
#include "wp.h"

#define MAXSTEPS 100
#define MAXBYTES (192L * 1024L)

static void rec_free(UNDO *u, UREC *r)
{
    u->bytes -= (r->nold + r->nnew) * (long)sizeof(CELL) + (r->aux0 ? 2L * sizeof(DOCAUX) : 0);
    free(r->cold); free(r->cnew); free(r->aux0); free(r->aux1);
    memset(r, 0, sizeof *r);
}

void undo_free(DOC *d)
{
    UNDO *u = &d->u;
    int i;
    for (i = 0; i < u->n; i++) rec_free(u, &u->r[i]);
    free(u->r);
    free(u->shadow);
    free(u->saux);
    memset(u, 0, sizeof *u);
}

/* d->a compared and copied as far as it is used */
static int aux_differs(const DOCAUX *a, const DOCAUX *b)
{
    if (memcmp(&a->ps, &b->ps, sizeof a->ps) || memcmp(&a->hf, &b->hf, sizeof a->hf) ||
        a->npara != b->npara || a->ntbl != b->ntbl || a->novr != b->novr || a->nobj != b->nobj) return 1;
    return memcmp(a->paras, b->paras, a->npara * sizeof(PARA)) ||
           (a->ntbl && memcmp(a->tbls, b->tbls, a->ntbl * sizeof(TBL))) ||
           (a->novr && memcmp(a->ovr, b->ovr, a->novr * sizeof(PGOVR))) ||
           (a->nobj && memcmp(a->objs, b->objs, a->nobj * sizeof(OBJ)));
}

static void aux_copy(DOCAUX *d, const DOCAUX *s)
{
    d->ps = s->ps; d->hf = s->hf;
    d->npara = s->npara; d->ntbl = s->ntbl; d->novr = s->novr; d->nobj = s->nobj;
    memcpy(d->paras, s->paras, s->npara * sizeof(PARA));
    if (s->ntbl) memcpy(d->tbls, s->tbls, s->ntbl * sizeof(TBL));
    if (s->novr) memcpy(d->ovr, s->ovr, s->novr * sizeof(PGOVR));
    if (s->nobj) memcpy(d->objs, s->objs, s->nobj * sizeof(OBJ));
}

/* the shadow after cells [lo, lo+nold) became nnew new ones */
static int shadow_patch(DOC *d, long lo, long nold, long nnew)
{
    UNDO *u = &d->u;
    if (u->slen - nold + nnew != d->len || lo + nold > u->slen) return 0;
    if (d->len > u->scap) return 0;
    if (nold != nnew && u->slen - lo - nold > 0)
        memmove(u->shadow + lo + nnew, u->shadow + lo + nold, (u->slen - lo - nold) * sizeof(CELL));
    if (nnew) memcpy(u->shadow + lo, d->text + lo, nnew * sizeof(CELL));
    u->slen = d->len;
    aux_copy(u->saux, &d->a);
    return 1;
}

static int shadow_sync(DOC *d)
{
    UNDO *u = &d->u;
    if (d->len > u->scap) {
        long c = d->len + 1024;
        CELL *n = (CELL *)realloc(u->shadow, c * sizeof(CELL));
        if (!n) return 0;
        u->shadow = n; u->scap = c;
    }
    if (d->len) memcpy(u->shadow, d->text, d->len * sizeof(CELL));
    u->slen = d->len;
    if (!u->saux) u->saux = (DOCAUX *)malloc(sizeof(DOCAUX));
    if (!u->saux) return 0;
    memcpy(u->saux, &d->a, sizeof(DOCAUX));
    return 1;
}

/* start afresh: the document as it is now is the oldest state */
void undo_reset(DOC *d)
{
    undo_free(d);
    d->ch_lo = -1; d->ch_hi = -1;
    d->u.ready = shadow_sync(d);
}

static void drop_oldest(UNDO *u)
{
    rec_free(u, &u->r[0]);
    memmove(&u->r[0], &u->r[1], (u->n - 1) * sizeof(UREC));
    u->n--;
    if (u->pos > 0) u->pos--;
}

/* record what changed since the last step; returns 1 if a step was made */
int undo_commit(DOC *d, int typing)
{
    UNDO *u = &d->u;
    long lo = d->ch_lo, hi = d->ch_hi, nold, nnew, oend;
    int auxch, i;
    UREC *r;

    if (!u->ready) { undo_reset(d); return 0; }
    if (u->full) {
        /* anything may have changed: find the changed part */
        long a = 0, s = 0;
        while (a < u->slen && a < d->len && !memcmp(&u->shadow[a], &d->text[a], sizeof(CELL))) a++;
        while (s < u->slen - a && s < d->len - a &&
               !memcmp(&u->shadow[u->slen - 1 - s], &d->text[d->len - 1 - s], sizeof(CELL))) s++;
        lo = a; hi = d->len - s;
        u->full = 0;
    }
    auxch = aux_differs(u->saux, &d->a);
    if (lo < 0 && !auxch) return 0;
    if (lo < 0) { lo = hi = 0; }
    if (hi < lo) hi = lo;
    if (hi > d->len) hi = d->len;
    /* the old range is as long as the new one plus what the text lost */
    nnew = hi - lo;
    nold = nnew + (u->slen - d->len);
    oend = lo + nold;
    if (nold < 0 || oend > u->slen) { nold = u->slen - lo; if (nold < 0) nold = 0; }
    /* trim cells that did not really change */
    while (nold > 0 && nnew > 0 && !memcmp(&u->shadow[lo], &d->text[lo], sizeof(CELL))) {
        lo++; nold--; nnew--;
    }
    while (nold > 0 && nnew > 0 &&
           !memcmp(&u->shadow[lo + nold - 1], &d->text[lo + nnew - 1], sizeof(CELL))) { nold--; nnew--; }
    d->ch_lo = d->ch_hi = -1;
    if (nold == 0 && nnew == 0 && !auxch) return 0;

    /* the steps after pos can no longer be redone */
    for (i = u->pos; i < u->n; i++) rec_free(u, &u->r[i]);
    u->n = u->pos;

    /* typing one character after the last typed one: merge */
    if (typing && !auxch && u->n > 0 && nold == 0 && nnew == 1) {
        UREC *p = &u->r[u->n - 1];
        unsigned char ch = d->text[lo].ch;
        if (p->typing && p->nold == 0 && p->pos + p->nnew == lo && p->nnew < 200 && ch != ' ' && !ISBRK(ch)) {
            CELL *n = (CELL *)realloc(p->cnew, (p->nnew + 1) * sizeof(CELL));
            if (n) {
                p->cnew = n;
                p->cnew[p->nnew++] = d->text[lo];
                p->cur1 = d->cursor;
                u->bytes += sizeof(CELL);
                u->pos = u->n;
                if (!shadow_patch(d, lo, nold, nnew)) shadow_sync(d);
                return 1;
            }
        }
    }

    while (u->n > 0 && (u->n >= MAXSTEPS || u->bytes > MAXBYTES)) drop_oldest(u);
    if (u->n >= u->cap) {
        int c = u->cap ? u->cap * 2 : 16;
        UREC *nr = (UREC *)realloc(u->r, c * sizeof(UREC));
        if (!nr) { shadow_sync(d); return 0; }
        u->r = nr; u->cap = c;
    }
    r = &u->r[u->n];
    memset(r, 0, sizeof *r);
    r->pos = lo; r->nold = nold; r->nnew = nnew;
    r->typing = typing && nold == 0 && nnew == 1;
    r->cur1 = d->cursor;
    r->cur0 = lo + nold;
    if (nold) r->cold = (CELL *)malloc(nold * sizeof(CELL));
    if (nnew) r->cnew = (CELL *)malloc(nnew * sizeof(CELL));
    if ((nold && !r->cold) || (nnew && !r->cnew)) { free(r->cold); free(r->cnew); shadow_sync(d); return 0; }
    if (nold) memcpy(r->cold, u->shadow + lo, nold * sizeof(CELL));
    if (nnew) memcpy(r->cnew, d->text + lo, nnew * sizeof(CELL));
    if (auxch) {
        r->aux0 = (DOCAUX *)malloc(sizeof(DOCAUX));
        r->aux1 = (DOCAUX *)malloc(sizeof(DOCAUX));
        if (r->aux0 && r->aux1) {
            memcpy(r->aux0, u->saux, sizeof(DOCAUX));
            memcpy(r->aux1, &d->a, sizeof(DOCAUX));
        } else { free(r->aux0); free(r->aux1); r->aux0 = r->aux1 = NULL; }
    }
    u->bytes += (nold + nnew) * (long)sizeof(CELL) + (r->aux0 ? 2L * sizeof(DOCAUX) : 0);
    u->n++;
    u->pos = u->n;
    if (!shadow_patch(d, lo, nold, nnew)) shadow_sync(d);
    return 1;
}

int undo_can(DOC *d) { return d->u.ready && d->u.pos > 0; }
int redo_can(DOC *d) { return d->u.ready && d->u.pos < d->u.n; }

static int swap_in(DOC *d, long pos, long ndel, const CELL *ins, long nins, const DOCAUX *aux)
{
    long sl = d->ch_lo, sh = d->ch_hi;
    if (ndel) doc_delete(d, pos, ndel);
    if (nins && !doc_insert(d, pos, ins, nins)) return 0;
    if (aux) memcpy(&d->a, aux, sizeof(DOCAUX));
    d->ch_lo = sl; d->ch_hi = sh;
    d->anchor = -1;
    d->dirty = 1;
    d->selobj = -1;
    return 1;
}

int undo_do(DOC *d)
{
    UNDO *u = &d->u;
    UREC *r;
    undo_commit(d, 0);
    if (!undo_can(d)) return 0;
    r = &u->r[--u->pos];
    if (!swap_in(d, r->pos, r->nnew, r->cold, r->nold, r->aux0)) { u->pos++; return 0; }
    d->cursor = r->cur0 <= d->len ? r->cur0 : d->len;
    shadow_sync(d);
    d->ch_lo = d->ch_hi = -1;
    return 1;
}

int redo_do(DOC *d)
{
    UNDO *u = &d->u;
    UREC *r;
    undo_commit(d, 0);
    if (!redo_can(d)) return 0;
    r = &u->r[u->pos++];
    if (!swap_in(d, r->pos, r->nold, r->cnew, r->nnew, r->aux1)) { u->pos--; return 0; }
    d->cursor = r->cur1 <= d->len ? r->cur1 : d->len;
    shadow_sync(d);
    d->ch_lo = d->ch_hi = -1;
    return 1;
}
