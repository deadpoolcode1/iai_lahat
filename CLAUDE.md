# CLAUDE.md

Repo-level guide for Claude Code working in this directory.

## What this is

Firmware for the Freescale/NXP **MPC555** PowerPC microcontroller that
acts as a **UART bridge between QSCI1 (host) and QSCI2 (target)**.

This is the carve-out for the MPC555 portion of the larger
`SOW_MPC555_MAX10_JTAG_FLASHING` work. Only the bridge mode is
implemented in this build; the JTAG bit-banging flow described in the
SOW lives elsewhere and is intentionally out of scope here.

## Power-up gate

The bridge is **gated by a 5-second "zzz" handshake**:

- On reset, init the SCIs and the PowerPC time base.
- For up to 5 s, watch QSCI1 (host) for the ASCII string `zzz`.
- If `zzz` arrives in that window → enter bridge mode and stay there
  until the next power cycle.
- If the window closes without `zzz` → idle. (In the full product the
  JTAG flow would take over at this point.)

## Layout

```
src/
  start.S         reset vector, .data copy, .bss zero, jump to main
  main.c          5-second window + bridge loop
  sci.{c,h}       QSCI1 / QSCI2 polled driver
  timebase.{c,h}  PowerPC TB-based timing
  mpc555.h        register map (USIU + QSMCM_A)
ld/mpc555.ld      memory map + section placement
Makefile          gcc-powerpc-linux-gnu in freestanding mode
```

## Build

```
make           # produces build/lahat.{elf,bin,srec,map}
make clean
```

Toolchain: Ubuntu `gcc-powerpc-linux-gnu` (32-bit big-endian PowerPC),
used freestanding (`-nostdlib -nostartfiles -ffreestanding`). It is not
an `eabi` toolchain but the bare-metal flags + custom linker script
make it behave like one for our purposes.

## Things worth knowing before editing

- **Clock assumption.** `F_SYS_HZ` defaults to 40 MHz in `main.c`. SCI
  baud divisor and the 5 s window both derive from it. Override on the
  command line (`make CFLAGS_EXTRA=-DF_SYS_HZ=56000000`) for boards
  running a different PLL setting.
- **SCI register offsets** in `mpc555.h` follow the QSMCM layout in the
  MPC555 User Manual. If the target board uses a non-standard QSMCM_A
  base, change `QSMCM_BASE` in that header — that is the single source
  of truth.
- **Endianness.** PowerPC is big-endian. `mmio_read16` / `mmio_write16`
  rely on the toolchain's default big-endian codegen — do not pass
  `-mlittle-endian`.
- **No interrupts.** Both the trigger window and the bridge loop are
  fully polled. Adding interrupt-driven RX would mean wiring up the EIS
  (External Interrupt Source) in the USIU and is a deliberate
  non-goal for the bridge build.
- **Soft float.** Built `-msoft-float`; nothing turns on MSR[FP], so do
  not introduce floating-point math.

## Don'ts

- Don't add JTAG bit-banging code here — wrong build, separate module.
- Don't replace polled SCI with DMA; the QSMCM SCIs do not have a DMA
  request line on MPC555.
- Don't introduce libc — there is no startfile, no heap, no syscalls.
