/* SPDX-License-Identifier: BSD-2-Clause */
/* Original Linux userspace entry, only for the temporary hardware readout. */
.section .text.start,"ax",%progbits
.global _start
.type _start,%function
_start:
    mov x29, xzr
    bl diag_main
    mov x8, #93
    svc #0
1:  b 1b
.section .note.GNU-stack,"",%progbits
