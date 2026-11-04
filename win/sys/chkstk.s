/* Copyright (c) 2026 Francis C Bullen. SPDX-License-Identifier: MIT (see LICENSE) */
/* chkstk.s - stack probing for large stack frames (32-bit x86): the
 * compiler calls __alloca with the frame size in eax; each page is
 * touched in turn so the stack's guard page moves down with it */
        .text
        .globl  __alloca
        .globl  ___chkstk
__alloca:
___chkstk:
        pushl   %ecx
        leal    8(%esp), %ecx
        cmpl    $0x1000, %eax
        jb      2f
1:      subl    $0x1000, %ecx
        orl     $0, (%ecx)
        subl    $0x1000, %eax
        cmpl    $0x1000, %eax
        ja      1b
2:      subl    %eax, %ecx
        orl     $0, (%ecx)
        movl    %esp, %eax
        movl    %ecx, %esp
        movl    (%eax), %ecx
        movl    4(%eax), %eax
        pushl   %eax
        ret
