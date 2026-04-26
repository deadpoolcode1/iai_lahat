#ifndef TIMEBASE_H
#define TIMEBASE_H

#include <stdint.h>

/* PowerPC Time Base — 64-bit counter, source-clocked at f_sys/4 on
 * MPC555 RCPU. We only need the low 32 bits since 5 s @ 10 MHz fits in
 * ~50e6 ticks.
 *
 * The TB is disabled out of reset; tb_init() turns it on via the USIU
 * TBSCR.TBE bit.
 */
void tb_init(void);
uint32_t tb_now(void);

/* Tick rate of the time base, derived from the system clock. */
#define TB_HZ(f_sys)   ((f_sys) / 4u)

#endif
