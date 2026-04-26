/* Suppress executable-stack warning from modern binutils. */
    .section .note.GNU-stack, "", @progbits
    .previous

/* MPC555 reset / startup.
 *
 * On power-up the PowerPC core jumps to the system-reset vector at 0x100.
 * We place _start there (see linker script) and from there:
 *   1. Set up r1 (stack pointer) at top of internal SRAM.
 *   2. Initialise r2/r13 small-data anchors so EABI -msdata works.
 *   3. Copy initialised .data from flash to SRAM.
 *   4. Zero .bss.
 *   5. Branch to C main(). main() never returns; if it does we trap.
 *
 * Floating point is not enabled — everything is built -msoft-float so we do
 * not need to set MSR[FP].
 */

    .section .boot, "ax"
    .globl   __reset_branch
__reset_branch:
    /* If MSR[IP]=0 the core comes up at 0x00000100, not here. We still keep
     * a defensive branch so a debugger that drops execution at 0x0 lands on
     * _start. */
    b   _start

    .section .vectors, "ax"
    .globl  _start
    .type   _start,@function
_start:
    /* Stack pointer: top of SRAM (defined in the linker script). EABI wants
     * an 8-byte aligned SP with a NULL back-chain word at the bottom of the
     * frame. */
    lis     1, __stack_top@h
    ori     1, 1, __stack_top@l
    li      0, 0
    stw     0, 0(1)

    /* Small-data anchors. The toolchain emits sdata/sdata2 references
     * relative to r13/r2 even when we are not actively using them, so wire
     * the anchors up to keep the relocations satisfied. */
    lis     2, _SDA2_BASE_@h
    ori     2, 2, _SDA2_BASE_@l
    lis     13, _SDA_BASE_@h
    ori     13, 13, _SDA_BASE_@l

    /* Copy .data from flash (load address) to SRAM (run address). */
    lis     3, __data_load@h
    ori     3, 3, __data_load@l
    lis     4, __data_start@h
    ori     4, 4, __data_start@l
    lis     5, __data_end@h
    ori     5, 5, __data_end@l
1:
    cmplw   4, 5
    bge-    2f
    lwz     6, 0(3)
    stw     6, 0(4)
    addi    3, 3, 4
    addi    4, 4, 4
    b       1b
2:

    /* Zero .bss. */
    lis     4, __bss_start@h
    ori     4, 4, __bss_start@l
    lis     5, __bss_end@h
    ori     5, 5, __bss_end@l
    li      6, 0
3:
    cmplw   4, 5
    bge-    4f
    stw     6, 0(4)
    addi    4, 4, 4
    b       3b
4:

    /* Jump into C. main() should never return. */
    bl      main

    /* Park the core if main() ever returns. */
hang:
    b       hang

    .size   _start, .-_start
