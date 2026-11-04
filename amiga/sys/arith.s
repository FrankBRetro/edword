| Copyright (c) 2026 Francis C Bullen. SPDX-License-Identifier: MIT (see LICENSE)
| arith.s - 32-bit multiply and divide for the 68000, which has only
| 16-bit MULU/DIVU. Called by the code GCC generates.

        .text
        .globl  __mulsi3, __udivsi3, __divsi3, __umodsi3, __modsi3

| a * b = al*bl + ((ah*bl + al*bh) << 16)
__mulsi3:
        move.w  4(sp),d0
        mulu.w  10(sp),d0
        move.w  6(sp),d1
        mulu.w  8(sp),d1
        add.w   d1,d0
        swap    d0
        clr.w   d0
        move.w  6(sp),d1
        mulu.w  10(sp),d1
        add.l   d1,d0
        rts

| unsigned divide: d0 / d1 -> quotient d0, remainder d1; uses d2
udiv:
        cmp.l   #0xffff,d1
        bhi.s   4f
        | 16-bit divisor: two DIVUs, high word then low word
        move.l  d0,d2
        clr.w   d2
        swap    d2
        divu.w  d1,d2
        swap    d0
        move.w  d2,d0
        swap    d0
        move.w  d0,d2
        divu.w  d1,d2
        move.w  d2,d0
        clr.w   d2
        swap    d2
        move.l  d2,d1
        rts
        | 32-bit divisor: the quotient fits 16 bits. Estimate it with both
        | numbers shifted until the divisor fits 16 bits, then correct.
4:      movem.l d3-d4,-(sp)
        move.l  d0,d3
        move.l  d1,d4
5:      lsr.l   #1,d1
        lsr.l   #1,d0
        cmp.l   #0xffff,d1
        bhi.s   5b
        divu.w  d1,d0
        and.l   #0xffff,d0
6:      move.l  d4,d1
        mulu.w  d0,d1
        move.l  d4,d2
        swap    d2
        mulu.w  d0,d2
        swap    d2
        tst.w   d2
        bne.s   7f
        clr.w   d2
        add.l   d2,d1
        bcs.s   7f
        cmp.l   d3,d1
        bls.s   8f
7:      subq.l  #1,d0
        bra.s   6b
8:      move.l  d3,d2
        sub.l   d1,d2
        move.l  d2,d1
        movem.l (sp)+,d3-d4
        rts

__udivsi3:
        move.l  d2,-(sp)
        move.l  8(sp),d0
        move.l  12(sp),d1
        bsr     udiv
        move.l  (sp)+,d2
        rts

__umodsi3:
        move.l  d2,-(sp)
        move.l  8(sp),d0
        move.l  12(sp),d1
        bsr     udiv
        move.l  d1,d0
        move.l  (sp)+,d2
        rts

__divsi3:
        movem.l d2-d3,-(sp)
        move.l  12(sp),d0
        move.l  16(sp),d1
        move.l  d0,d3
        eor.l   d1,d3
        tst.l   d0
        bpl.s   1f
        neg.l   d0
1:      tst.l   d1
        bpl.s   2f
        neg.l   d1
2:      bsr     udiv
        tst.l   d3
        bpl.s   3f
        neg.l   d0
3:      movem.l (sp)+,d2-d3
        rts

__modsi3:
        movem.l d2-d3,-(sp)
        move.l  12(sp),d0
        move.l  16(sp),d1
        move.l  d0,d3
        bpl.s   1f
        neg.l   d0
1:      tst.l   d1
        bpl.s   2f
        neg.l   d1
2:      bsr     udiv
        move.l  d1,d0
        tst.l   d3
        bpl.s   3f
        neg.l   d0
3:      movem.l (sp)+,d2-d3
        rts
