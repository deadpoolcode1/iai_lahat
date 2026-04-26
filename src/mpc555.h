#ifndef MPC555_H
#define MPC555_H

/* MPC555 register definitions used by this project.
 *
 * Sources: Freescale "MPC555 / MPC556 User's Manual" (MPC555UM Rev. 3),
 * chapters on the USIU (System Integration Unit) and the QSMCM (Queued
 * Serial Multi-Channel Module). Addresses are absolute on the IMB3 bus.
 *
 * The driver only touches QSMCM_A (which provides QSCI1, QSCI2 and the
 * QSPI). MPC555 dev boards typically wire the MAX3224 RS232 transceiver to
 * QSCI1 (host port) and QSCI2 (target port) — that is the assumption here.
 */

#include <stdint.h>

/* --------------------------------------------------------------------- */
/* USIU — used to enable the Time Base for the 5 second power-up window. */
/* --------------------------------------------------------------------- */

#define USIU_BASE           0x002FC000u

#define TBSCR_ADDR          (USIU_BASE + 0x0200u)   /* 16-bit  */
#define TBSCR_TBE           0x0001u                 /* Time Base Enable */

/* --------------------------------------------------------------------- */
/* QSMCM_A — Queued Serial Multi-Channel Module.                         */
/* --------------------------------------------------------------------- */

#define QSMCM_BASE          0x00305000u

/* Module configuration. */
#define QSMCMMCR_ADDR       (QSMCM_BASE + 0x0000u)  /* 16-bit  */
#define QTEST_ADDR          (QSMCM_BASE + 0x0002u)
#define QDSCI_IL_ADDR       (QSMCM_BASE + 0x0004u)
#define QSPI_IL_ADDR        (QSMCM_BASE + 0x0006u)

/* QSCI1 — host-side SCI. */
#define QSCI1CR0_ADDR       (QSMCM_BASE + 0x0008u)  /* 16-bit, SCBR    */
#define QSCI1CR1_ADDR       (QSMCM_BASE + 0x000Au)  /* 16-bit, enables */
#define QSCI1SR_ADDR        (QSMCM_BASE + 0x000Cu)  /* 16-bit, flags   */
#define QSCI1DR_ADDR        (QSMCM_BASE + 0x000Eu)  /* 16-bit, data    */

/* QSCI2 — target-side SCI (MPC555 dual-SCI). */
#define QSCI2CR0_ADDR       (QSMCM_BASE + 0x0020u)
#define QSCI2CR1_ADDR       (QSMCM_BASE + 0x0022u)
#define QSCI2SR_ADDR        (QSMCM_BASE + 0x0024u)
#define QSCI2DR_ADDR        (QSMCM_BASE + 0x0026u)

/* SCCR1 (control register 1) bits — same layout for both SCIs. */
#define SCCR1_LOOPS         0x4000u
#define SCCR1_WOMS          0x2000u
#define SCCR1_ILT           0x1000u
#define SCCR1_PT            0x0800u
#define SCCR1_PE            0x0400u
#define SCCR1_M             0x0200u
#define SCCR1_WAKE          0x0100u
#define SCCR1_TIE           0x0080u
#define SCCR1_TCIE          0x0040u
#define SCCR1_RIE           0x0020u
#define SCCR1_ILIE          0x0010u
#define SCCR1_TE            0x0008u
#define SCCR1_RE            0x0004u
#define SCCR1_RWU           0x0002u
#define SCCR1_SBK           0x0001u

/* SCSR (status register) bits. */
#define SCSR_TDRE           0x0100u
#define SCSR_TC             0x0080u
#define SCSR_RDRF           0x0040u
#define SCSR_IDLE           0x0020u
#define SCSR_OR             0x0010u
#define SCSR_NF             0x0008u
#define SCSR_FE             0x0004u
#define SCSR_PF             0x0002u
#define SCSR_RAF            0x0001u

/* --------------------------------------------------------------------- */
/* Memory-mapped I/O helpers. The MPC555 core is big-endian; powerpc-eabi /
 * powerpc-linux-gnu toolchains emit big-endian loads/stores, so plain
 * volatile pointer access is correct.                                    */
/* --------------------------------------------------------------------- */

static inline uint16_t mmio_read16(uint32_t addr)
{
    return *(volatile uint16_t *)addr;
}

static inline void mmio_write16(uint32_t addr, uint16_t val)
{
    *(volatile uint16_t *)addr = val;
}

#endif /* MPC555_H */
