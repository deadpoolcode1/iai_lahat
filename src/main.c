/* MPC555 UART bridge — power-up "zzz" command guard.
 *
 * Behaviour required by the SOW companion task:
 *   1. On reset, initialise QSCI1 (host) and QSCI2 (target).
 *   2. For up to 5 seconds, watch QSCI1 for the ASCII string "zzz".
 *   3. If "zzz" arrives in that window, enter the bridge state and stay
 *      there until the next power cycle (no exit).
 *   4. If the window closes without "zzz", park the core. In the larger
 *      product the JTAG flashing flow takes over here; that flow lives in
 *      a separate firmware build, not this one.
 *
 * Bridge state forwards bytes byte-for-byte in both directions:
 *   QSCI1.RX -> QSCI2.TX     (host -> target)
 *   QSCI2.RX -> QSCI1.TX     (target -> host)
 *
 * Polled, no interrupts. The MPC555 SCIs are double-buffered so polling at
 * the 9600-baud line rate is comfortably fast enough.
 */

#include <stdbool.h>
#include <stdint.h>

#include "sci.h"
#include "timebase.h"

/* Tune these for the actual board. The MPC555 PLL is configured by the
 * USIU but boards often boot at a known external-clock-derived f_sys; pick
 * the one your target is set up for. 40 MHz is the most common default. */
#ifndef F_SYS_HZ
#define F_SYS_HZ        40000000u
#endif

#ifndef BAUD_HOST
#define BAUD_HOST       9600u
#endif

#ifndef BAUD_TARGET
#define BAUD_TARGET     9600u
#endif

#define WINDOW_SECONDS  5u

static const char TRIGGER[] = "zzz";
#define TRIGGER_LEN     3u

/* Runs forever; only a power cycle exits. */
static void bridge_loop(void)
{
    for (;;) {
        if (sci_rx_ready(SCI_CH_HOST)) {
            uint8_t b = sci_read_byte(SCI_CH_HOST);
            sci_write_byte(SCI_CH_TARGET, b);
        }
        if (sci_rx_ready(SCI_CH_TARGET)) {
            uint8_t b = sci_read_byte(SCI_CH_TARGET);
            sci_write_byte(SCI_CH_HOST, b);
        }
    }
}

/* Returns true if "zzz" arrives on the host port within WINDOW_SECONDS. */
static bool wait_for_trigger(void)
{
    const uint32_t window_ticks = TB_HZ(F_SYS_HZ) * WINDOW_SECONDS;
    const uint32_t t0 = tb_now();
    uint32_t matched = 0;

    while ((tb_now() - t0) < window_ticks) {
        if (!sci_rx_ready(SCI_CH_HOST)) {
            continue;
        }
        uint8_t b = sci_read_byte(SCI_CH_HOST);
        if (b == (uint8_t)TRIGGER[matched]) {
            matched++;
            if (matched == TRIGGER_LEN) {
                return true;
            }
        } else {
            /* Restart, but if the byte itself is the first char of the
             * trigger we count it — keeps "zzzz" / "azzz" working. */
            matched = (b == (uint8_t)TRIGGER[0]) ? 1u : 0u;
        }
    }
    return false;
}

int main(void)
{
    tb_init();
    sci_init(SCI_CH_HOST,   BAUD_HOST,   F_SYS_HZ);
    sci_init(SCI_CH_TARGET, BAUD_TARGET, F_SYS_HZ);

    if (wait_for_trigger()) {
        bridge_loop();   /* never returns */
    }

    /* Trigger window closed without "zzz". The JTAG flow would take over
     * in the full product; here we just idle until power cycle. */
    for (;;) {
    }
}
