| Copyright (c) 2026 Francis C Bullen. SPDX-License-Identifier: MIT (see LICENSE)
| start.s - entry point of the EdWord Amiga port. AmigaDOS starts a
| program at its first byte with the argument line in a0/d0.

        .section .text.unlikely,"ax"
        .globl  _start
_start:
        move.l  4.w,SysBase
        move.l  a0,ami_argptr
        move.l  d0,ami_arglen
        jsr     ami_startup
        rts

| int ami_bigstack(int (*fn)(void), struct StackSwapStruct *ss)
| runs fn on the stack described by ss (exec StackSwap, V37)
        .globl  ami_bigstack
ami_bigstack:
        movem.l d2-d7/a2-a6,-(sp)
        move.l  48(sp),a3
        move.l  52(sp),a2
        move.l  SysBase,a6
        move.l  a2,a0
        jsr     -732(a6)
        jsr     (a3)
        move.l  d0,d2
        move.l  SysBase,a6
        move.l  a2,a0
        jsr     -732(a6)
        move.l  d2,d0
        movem.l (sp)+,d2-d7/a2-a6
        rts

| hook entry for C: calls h_SubEntry(hook a0, object a2, message a1)
        .globl  ami_hookentry
ami_hookentry:
        move.l  a1,-(sp)
        move.l  a2,-(sp)
        move.l  a0,-(sp)
        move.l  12(a0),a0
        jsr     (a0)
        lea     12(sp),sp
        rts
