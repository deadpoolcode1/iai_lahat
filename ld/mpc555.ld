/* MPC555 linker script.
 *
 * Memory map (per MPC555 reference manual):
 *   CMF Flash   0x00000000 - 0x0006FFFF  (448 KB)
 *   Internal SRAM (calibration RAM, 26 KB) at 0x003F9800.
 *
 * Reset behaviour: the PowerPC core fetches the first instruction from the
 * system reset exception vector. With MSR[IP]=0 the vector is at 0x00000100,
 * which is where _start is placed below. The very first words of flash hold
 * the hard-reset configuration word + a branch into _start so the chip wakes
 * up cleanly even if MSR[IP] later changes.
 */

OUTPUT_FORMAT("elf32-powerpc", "elf32-powerpc", "elf32-powerpc")
OUTPUT_ARCH(powerpc:common)
ENTRY(_start)

MEMORY
{
    flash (rx)  : ORIGIN = 0x00000000, LENGTH = 448K
    sram  (rwx) : ORIGIN = 0x003F9800, LENGTH = 26K
}

/* Top-of-RAM stack. Leave 16 bytes for the EABI back-chain word. */
PROVIDE(__stack_top = ORIGIN(sram) + LENGTH(sram) - 16);

SECTIONS
{
    /* 0x000..0x0FF: hard-reset configuration / boot vector area. */
    .boot 0x00000000 : {
        KEEP(*(.boot))
    } > flash

    /* 0x100: system-reset exception vector — _start lives here. */
    .vectors 0x00000100 : {
        KEEP(*(.vectors))
        . = ALIGN(4);
    } > flash

    .text : {
        *(.text .text.*)
        *(.rodata .rodata.*)
        *(.sdata2 .sdata2.*)
        *(.sbss2 .sbss2.*)
        . = ALIGN(4);
        __data_load = .;
    } > flash

    .data : AT(__data_load) {
        __data_start = .;
        *(.data .data.*)
        *(.sdata .sdata.*)
        . = ALIGN(4);
        __data_end = .;
    } > sram

    .bss (NOLOAD) : {
        __bss_start = .;
        *(.bss .bss.*)
        *(.sbss .sbss.*)
        *(COMMON)
        . = ALIGN(4);
        __bss_end = .;
    } > sram

    /DISCARD/ : { *(.note.GNU-stack) *(.comment) *(.eh_frame) }
}
