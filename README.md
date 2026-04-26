# MPC555 UART Bridge

Bare-metal firmware for the Freescale/NXP **MPC555** that runs as a
**UART bridge between QSCI1 (host) and QSCI2 (target)**, gated by a
5-second power-up handshake.

This is the MPC555-only carve-out of the `MPC555 ↔ MAX10 JTAG Flashing`
SOW. The JTAG bit-banging flow described in that SOW is **out of scope
for this build** — only the bridge mode is implemented here.

## Behaviour

```
power-up
    │
    ▼
init QSCI1, QSCI2, time base
    │
    ▼
watch QSCI1 for "zzz" — 5 s timeout
    │
    ├── "zzz" received  ──▶  bridge mode forever (until power cycle)
    │                          QSCI1.RX → QSCI2.TX
    │                          QSCI2.RX → QSCI1.TX
    │
    └── timeout         ──▶  idle (JTAG flow takes over in full product)
```

The trigger detector tolerates noise: `zzzz`, `azzz`, etc. are matched
correctly. Once bridge mode is entered there is no exit short of a
power cycle.

Defaults: `9600 8N1` on both ports, `F_SYS_HZ = 40 MHz`. All three are
overridable at the compiler invocation — see [CLAUDE.md](CLAUDE.md).

## Hardware assumptions

- MPC555 with PLL configured for the system clock declared in
  `F_SYS_HZ` (40 MHz by default).
- QSCI1 wired to the **host** RS232 transceiver (e.g. MAX3224 on
  QSMSM7 / QSMSM9 as called out in the SOW).
- QSCI2 wired to the **target** UART.
- Internal SRAM at `0x003F9800` (26 KB). Code runs from CMF flash at
  `0x00000000` (448 KB).

## Build

Install the toolchain (Ubuntu / Debian):

```sh
sudo apt-get install gcc-powerpc-linux-gnu binutils-powerpc-linux-gnu
```

Then build:

```sh
make
```

Outputs land in `build/`:

| File             | Purpose                                                    |
| ---------------- | ---------------------------------------------------------- |
| `lahat.elf`      | Linked ELF, includes debug info — feed to GDB.             |
| `lahat.bin`      | Raw flat binary, byte 0 = address `0x00000000`.            |
| `lahat.srec`     | Motorola S-record — what most MPC555 flash tools want.     |
| `lahat.map`      | Linker map (sections, symbols, sizes).                     |

`make size` shows section sizes. `make clean` wipes `build/`.

## Layout

```
src/
  start.S         reset vector, .data copy, .bss zero, jump to main
  main.c          5-second "zzz" window + bridge loop
  sci.{c,h}       QSCI1 / QSCI2 polled driver
  timebase.{c,h}  PowerPC TB-based timing
  mpc555.h        register definitions (USIU + QSMCM_A)
ld/mpc555.ld      memory map + section placement
Makefile          freestanding cross-compile rules
CLAUDE.md         repo guide for Claude Code
```

## Flashing

Out of scope for this repo — bring your preferred MPC555 BDM/Nexus
debugger (Lauterbach, P&E, iSystem, etc.) and have it program
`build/lahat.srec` into CMF flash from address `0x00000000`.

## License

Internal IAI project. All rights reserved.
