#include "timebase.h"
#include "mpc555.h"

void tb_init(void)
{
    uint16_t v = mmio_read16(TBSCR_ADDR);
    mmio_write16(TBSCR_ADDR, (uint16_t)(v | TBSCR_TBE));
}

uint32_t tb_now(void)
{
    uint32_t tbl;
    /* mftb (Move From Time Base) — short form of `mfspr rD, 268`.
     * Returns TBL (the low 32 bits of the 64-bit time base). */
    __asm__ volatile ("mftb %0" : "=r"(tbl));
    return tbl;
}
