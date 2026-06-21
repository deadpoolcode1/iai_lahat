/*
 * uart_bridge.c  -  Transparent UART1 <-> UART2 byte bridge for the MPC555,
 *                   built with the Wind River Diab compiler (dcc).
 *
 * Target :  -tPPC555EH:simple
 * Build  :  dcc -tPPC555EH:simple -c -Xeieio -X40 -x200 -g uart_bridge.c
 *
 * Function:
 *   Forwards every byte received on UART1 out of UART2, and every byte
 *   received on UART2 out of UART1, as-is (8 data bits, no parity, 1 stop).
 *   On the MPC555 the two UARTs are the two SCI channels of the QSMCM:
 *       UART1 = SCI1 (SCC1Rx / SC1SR / SC1DR)
 *       UART2 = SCI2 (SCC2Rx / SC2SR / SC2DR)
 *
 *   This is a polled, non-blocking pump: each loop iteration moves at most
 *   one byte per direction, only when the source has a byte AND the
 *   destination transmitter is free. No data is lost as long as the loop
 *   drains a channel before the next byte arrives (see "Limitations").
 *
 * ---------------------------------------------------------------------------
 * VERIFY REGISTER ADDRESSES
 *   The QSMCM base and per-SCI offsets below are the typical MPC555 User's
 *   Manual values. Your project already has reg555.h / reg555ds.h -- prefer
 *   including those and deleting the local definitions if there is any
 *   mismatch. SC2* (SCI2) offsets in particular are worth double-checking.
 *   The SCI *bit* positions (TDRE/RDRF/TE/RE) are stable across the QSM
 *   family and are the values used here.
 * ---------------------------------------------------------------------------
 *
 * C89 style for compatibility with the 2006-era Diab toolchain.
 */

/* ------------------------------------------------------------------ types */
typedef unsigned char  u8;
typedef unsigned short u16;
typedef unsigned long  u32;

#define REG16(addr)  (*(volatile u16 *)(addr))

/* ---------------------------------------------------- clock / baud config */
/* SCI baud:  baud = f_sys / (32 * SCBR),  where SCBR is the 13-bit field of
 * SCCxR0 (range 1..8191), integer only -- there is NO fractional divider.
 *
 * Set SYS_CLOCK_HZ to the REAL running system clock (the PLL output, set in
 * the USIU by crt0/board init from the crystal + MODCK pins). The
 * MPC555LFMZP40 is rated for 40 MHz MAX, so the SCI can reach at most
 * 40e6/32 = 1.25 Mbaud -- but only rates whose divisor lands near an integer
 * are accurate.
 *
 * Well-known rates: set BAUD_RATE to any of
 *   1200 2400 4800 9600 19200 38400 57600 115200 230400 460800 921600
 *
 * IMPORTANT: at SYS_CLOCK_HZ = 40 MHz the integer divider only yields
 * acceptable accuracy up to 115200 (<=1.4% error). 230400/460800/921600 land
 * between divisor steps (errors of +8.5% / -9.6% / +35.6%) and will NOT
 * communicate. The compile-time guard below rejects any rate that cannot be
 * produced within ~2% at the current SYS_CLOCK_HZ, so you find out at build
 * time. Those high rates only become reachable with a higher f_sys, which
 * this part does not support -- they are listed for use on faster clocks. */
#define SYS_CLOCK_HZ   40000000UL     /* <-- match the REAL f_sys (PLL out) */
#define BAUD_RATE      625000UL       /* SCBR=2, exact at 40 MHz (0% error) */

/* ----------------------------------------------- STM32 control behaviour */
/* PROG_EN ("PortFb7") is parked at a fixed level BEFORE the bridge starts.
 *   1 = drive PROG_EN logic HIGH,  0 = drive PROG_EN logic LOW.
 * Pick whichever level puts your STM32 into the mode you want it to boot
 * into when NRST is released. */
#define PROG_EN_LEVEL  1

/* ------------------------------------------------ handshake / entry gate */
/* After resetting the STM32, main_uart_bridge() waits for this exact byte
 * sequence on UART1. If the full sequence arrives within HANDSHAKE_TIMEOUT_MS,
 * it enters the (infinite) bridge. If not, it RETURNS to the caller.
 *
 * Bytes consumed while matching the handshake are NOT forwarded -- they are
 * the gate key, not payload. Fill HANDSHAKE_KEY with the real sequence the
 * STM32 emits on boot. */
#define HANDSHAKE_LEN          24
#define HANDSHAKE_TIMEOUT_MS   1000UL        /* <-- "x milliseconds" */
#define STM32_BOOT_TIME_MS     100UL        /* <-- "x milliseconds" */

/* main_uart_bridge() return codes. NOTE: on success it never returns (the
 * bridge loop is infinite); it only returns on the timeout path. */
#define UART_BRIDGE_NO_SYNC   (-1)          /* handshake not seen in time   */

/* ---------------------------------------------------- millisecond timing */
/* The timeout is measured with the PowerPC Time Base (a free-running 64-bit
 * up-counter). We only read the low 32 bits; unsigned subtraction is correct
 * across wrap for any interval shorter than 2^32 ticks (always true here).
 *
 * ----  VERIFY (silicon + toolchain) --------------------------------------
 *  (a) TB_HZ: the Time Base does NOT tick at f_sys. On the MPC555 it is
 *      driven by a divided system clock (commonly f_sys/4); confirm the
 *      actual divisor for your USIU configuration in the manual. Get this
 *      wrong and "milliseconds" are simply scaled wrong.
 *  (b) tb_ticks(): reads TBL via SPR 268. The asm-macro form and the
 *      mfspr-vs-mftb choice are Diab/RCPU specific -- see tb_ticks() below.
 * ------------------------------------------------------------------------- */
#define TB_DIVISOR     16UL                  /* <-- VERIFY: f_sys / this = TB rate */
#define TB_HZ          (SYS_CLOCK_HZ / TB_DIVISOR)
#define MS_TO_TB(ms)   ((TB_HZ / 1000UL) * (u32)(ms))   /* ms must keep result < 2^32 */

/* SCBR rounded to nearest; actual baud that divisor produces. */
#define SCBR_FOR(b)     (((SYS_CLOCK_HZ) + 16UL * (b)) / (32UL * (b)))
#define BAUD_ACTUAL(b)  ((SYS_CLOCK_HZ) / (32UL * SCBR_FOR(b)))

/* Reject a BAUD_RATE that cannot be produced within ~2% at SYS_CLOCK_HZ. */
#if (100UL * BAUD_ACTUAL(BAUD_RATE) > 102UL * (BAUD_RATE)) || \
    (100UL * BAUD_ACTUAL(BAUD_RATE) <  98UL * (BAUD_RATE))
#error "BAUD_RATE not achievable within 2% at SYS_CLOCK_HZ (raise f_sys or pick a lower rate)"
#endif

/* ------------------------------------------------------- USIU / watchdog */
#define USIU_BASE      0x002FC000UL
#define SWSR           REG16(USIU_BASE + 0x00EUL)  /* Software Service Reg. */
#define TBSCR          REG16(USIU_BASE + 0x200UL)  /* Timebase STatus/Ctrl */
#define TBSCR_TBE      0x0001u                     /* bit 15: TB+DEC enable */

static void wdt_service(void)
{
    SWSR = 0x556C;
    SWSR = 0xAA39;
}

static void tb_enable(void)
{
    TBSCR |= (u16)TBSCR_TBE;
}

/* ---------------------------------------------------------- QSMCM / SCIs */
#define QSMCM_BASE     0x00305000UL   /* <-- VERIFY against reg555ds.h */

/* SCI1 (UART1) */
#define SCC1R0_ADDR    (QSMCM_BASE + 0x08UL)   /* control 0 (baud)   */
#define SCC1R1_ADDR    (QSMCM_BASE + 0x0AUL)   /* control 1 (enables)*/
#define SC1SR_ADDR     (QSMCM_BASE + 0x0CUL)   /* status             */
#define SC1DR_ADDR     (QSMCM_BASE + 0x0EUL)   /* data               */

/* SCI2 (UART2) -- VERIFY these offsets */
#define SCC2R0_ADDR    (QSMCM_BASE + 0x20UL)
#define SCC2R1_ADDR    (QSMCM_BASE + 0x22UL)
#define SC2SR_ADDR     (QSMCM_BASE + 0x24UL)
#define SC2DR_ADDR     (QSMCM_BASE + 0x26UL)

/* SCSR (status) bit flags -- stable across the QSM family */
#define SCSR_TDRE      0x0100   /* Transmit Data Register Empty */
#define SCSR_TC        0x0080   /* Transmit Complete            */
#define SCSR_RDRF      0x0040   /* Receive Data Register Full   */
#define SCSR_OR        0x0008   /* Overrun                      */
#define SCSR_NF        0x0004   /* Noise                        */
#define SCSR_FE        0x0002   /* Framing Error                */
#define SCSR_PF        0x0001   /* Parity                       */

/* SCCxR1 (control 1) bit flags. Enabling TX+RX is the classic 0x000C. */
#define SCCR1_M        0x0200   /* 0 = 8 data bits, 1 = 9            */
#define SCCR1_PE       0x0400   /* parity enable                    */
#define SCCR1_TE       0x0008   /* transmitter enable (bit 3)       */
#define SCCR1_RE       0x0004   /* receiver enable    (bit 2)       */

/* ---------------------------------------------------- QSMCM port (pins) */
/* The four UART pins are the dedicated SCI pins, which sit at these bit
 * positions of the QSMCM port (PORTQS):
 *      bit 7  = TXD1   (UART1 TX)     bit 9  = RXD1  (UART1 RX)
 *      bit 8  = TXD2   (UART2 TX)     bit 10 = RXD2  (UART2 RX)
 * Before the SCIs can use them, PQSPAR must assign each pin to its SCI
 * (peripheral) function rather than GPIO, and DDRQS must make the TX pins
 * outputs / the RX pins inputs.
 *
 * VERIFY the register offsets and the PQSPAR polarity against reg555ds.h.
 * On the MPC555 these are 16-bit registers (the classic 8-bit QSM port is
 * widened to cover bits 8-10). */
#define PQSPAR_ADDR    (QSMCM_BASE + 0x16UL)   /* <-- VERIFY: pin assignment */
#define DDRQS_ADDR     (QSMCM_BASE + 0x18UL)   /* <-- VERIFY: data direction */

#define QS_TXD1        (1u << 7)
#define QS_TXD2        (1u << 8)
#define QS_RXD1        (1u << 9)
#define QS_RXD2        (1u << 10)
#define QS_TX_MASK     (QS_TXD1 | QS_TXD2)
#define QS_RX_MASK     (QS_RXD1 | QS_RXD2)
#define QS_SCI_MASK    (QS_TX_MASK | QS_RX_MASK)

/* Assign the four SCI pins to their UART function and set directions.
 * If your silicon connects the SCI pins automatically when TE/RE are set,
 * this is harmless belt-and-suspenders; if it does not, it is required. */
static void qsmcm_pins_init(void)
{
    /* PQSPAR bit = 1 selects the peripheral (SCI) function. If your manual
     * uses the opposite polarity for these bits, invert this line. */
    REG16(PQSPAR_ADDR) |= (u16)QS_SCI_MASK;

    /* TX pins as outputs, RX pins as inputs. */
    REG16(DDRQS_ADDR) = (u16)((REG16(DDRQS_ADDR) | QS_TX_MASK) & ~QS_RX_MASK);
}

/* ============================================================ STM32 pins
 * Two GPIOs to the STM32, controlled around bridge bring-up:
 *
 *   PROG_EN  <- net "PortFb7"     program/boot-mode select on the STM32.
 *               Driven to PROG_EN_LEVEL (above) BEFORE the bridge inits,
 *               so it is already valid + latched when the STM32 next boots.
 *
 *   NRST     <- net "D_Actionb_8" the STM32 reset, ACTIVE LOW.
 *               Pulsed low->high ONCE after the bridge is ready, so the
 *               STM32 restarts and samples PROG_EN on the way out of reset.
 *
 * ----  ADDRESSES BELOW ARE FROM MPC555UM Rev.15, SECTION 15 (MIOS1)  -----
 * Net->pin mapping confirmed from the schematic; register addresses are the
 * manual's authoritative values. MIOS1 base = 0x306000.
 *
 *   PROG_EN  = "PortFb7" = MPIO32B7, bit 7 of the MIOS 16-bit parallel port
 *              (MPIOSM, submodule 32). Table 15-36 / 15.13. Plain port bit:
 *              write direction + data. (Bit Dn drives MPIO32Bn; D7 = 0x0080.)
 *              Schematic: PortFb_n -> MPIO32B_n straight across.
 *
 *   NRST     = "D_Actionb_8" = MDA30, an MDASM channel (MIOS Double Action
 *              Submodule). Schematic maps D_Actionb(9:0) onto the ten real
 *              MDASM channels descending -- 9->31, 8->30, 7->29, 6->28,
 *              5->27, 4->15, 3->14, 2->13, 1->12, 0->11 -- so D_Actionb_8 is
 *              MDA30. Full SCR map (Table 15-16) for reference:
 *                  MDA11=0x30605E  MDA12=0x306066  MDA13=0x30606E
 *                  MDA14=0x306076  MDA15=0x30607E
 *                  MDA27=0x3060DE  MDA28=0x3060E6  MDA29=0x3060EE
 *                  MDA30=0x3060F6  MDA31=0x3060FE
 *              An MDASM is NOT a port bit -- it is an output-compare cell.
 *              We drive a level by forcing its output flip-flop (FORCA/FORCB)
 *              while in OCB mode; see stm32_nrst_*() below. */

/* PROG_EN ("PortFb7") = MPIO32B7  (MPIOSM parallel port, submodule 32). */
#define PROGEN_DATA    REG16(0x00306100UL)   /* MPIOSMDR  (data)      */
#define PROGEN_DDR     REG16(0x00306102UL)   /* MPIOSMDDR (direction) */
#define PROGEN_BIT     (1u << 7)             /* D7 -> MPIO32B7        */

/* NRST = MDA30.  Confirmed from schematic: D_Actionb(9:0) -> MDA(31..27,15..11)
 * descending, so D_Actionb_8 = MDA30. MDASM30 Status/Control reg = 0x3060F6. */
#define NRST_SCR       REG16(0x003060F6UL)   /* MDASM30 SCR (D_Actionb_8 = MDA30) */

/* MDASMSCR fields (MPC555UM Table 15-17, 16-bit, MSB=bit0). */
#define MDASM_MOD_DIS  0x0000u   /* MOD=0000 disable (enter before mode change) */
#define MDASM_MOD_OCB  0x0004u   /* MOD=0100 output compare, flag on B          */
#define MDASM_WOR_OD   0x4000u   /* WOR=1 open-drain output buffer              */
#define MDASM_FORCA    0x0400u   /* force flip-flop SET   -> pin high / release */
#define MDASM_FORCB    0x0200u   /* force flip-flop RESET -> pin low  / assert  */
/* EDPOL left 0: channel-A compare sets the pin, channel-B compare clears it. */

/* NRST low pulse width. The STM32 only needs a few hundred ns of NRST low,
 * but give it margin. This is a coarse, clock-dependent busy-wait -- tune
 * the count for your f_sys if you care about the exact width. */
#define NRST_LOW_LOOPS 20000UL

/* Crude busy-wait. volatile so the compiler can't optimize it away. */
static void delay_loops(u32 n)
{
    volatile u32 i;
    for (i = 0; i < n; ++i) {
        /* spin */
    }
}

/* Read the low 32 bits of the PowerPC Time Base (free-running up-counter).
 *
 * ----  VERIFY for your Diab version / RCPU  --------------------------------
 * This is a Diab "asm string function": the value left in r3 is the return
 * value. It reads TBL (Time Base Lower) from SPR 268. On the MPC5xx RCPU the
 * time base is read with mfspr (the later `mftb` mnemonic may not exist); if
 * your headers/core expect mftb, swap the instruction. If your Diab build
 * provides a builtin (e.g. __mfspr), you can use that instead and delete this.
 * The rest of the timeout logic does not care HOW the tick count is obtained
 * -- only that this returns a monotonically increasing count at TB_HZ. */
asm volatile u32 tb_ticks(void)
{
    mftb   r3
}

static void delay_ms(u32 timeout_ms)
{
    u32 start    = tb_ticks();
    u32 deadline = MS_TO_TB(timeout_ms);   /* elapsed-tick budget */

    while ((tb_ticks() - start) < deadline) {
        wdt_service();                     /* don't let the WDT reset us */
    }
}

/* Park PROG_EN ("PortFb7") at PROG_EN_LEVEL and make it an output.
 * Call this BEFORE the UART bridge is initialized. */
static void stm32_progen_init(void)
{
    PROGEN_DDR |= (u16)PROGEN_BIT;            /* drive it (output) */
#if PROG_EN_LEVEL
    PROGEN_DATA |= (u16)PROGEN_BIT;           /* logic high */
#else
    PROGEN_DATA &= (u16)~PROGEN_BIT;          /* logic low  */
#endif
}

/* Configure the NRST MDASM channel: open-drain output-compare, output left
 * RELEASED (high via the STM32 pull-up). Call early, before bring-up, so NRST
 * is in a known de-asserted state. Open-drain (WOR=1) is deliberate: NRST is
 * bidirectional with an internal pull-up, so we only ever pull it low and
 * release to Hi-Z, never fight the STM32. The manual requires passing through
 * DIS before selecting a new mode. */
static void stm32_nrst_init(void)
{
    NRST_SCR = MDASM_MOD_DIS;                      /* disable before mode change */
    NRST_SCR = (u16)(MDASM_WOR_OD | MDASM_MOD_OCB);/* open-drain, EDPOL=0, OCB    */
    NRST_SCR |= (u16)MDASM_FORCA;                  /* set FF -> released (high)   */
}

/* Reset the STM32 once: force NRST low, hold, release. FORCA/FORCB always read
 * back 0, so each |= sets exactly one force bit and preserves WOR/MOD. */
static void stm32_nrst_pulse(void)
{
    NRST_SCR |= (u16)MDASM_FORCB;             /* reset FF -> pin low (assert) */
    delay_loops(NRST_LOW_LOOPS);
    NRST_SCR |= (u16)MDASM_FORCA;             /* set FF -> Hi-Z, pulled high   */
    delay_ms(STM32_BOOT_TIME_MS);
}

/* ----------------------------------------------------------- SCI handle */
typedef struct {
    volatile u16 *cr0;   /* SCCxR0 - baud  */
    volatile u16 *cr1;   /* SCCxR1 - ctrl  */
    volatile u16 *sr;    /* SCxSR  - status*/
    volatile u16 *dr;    /* SCxDR  - data  */
} sci_t;

/* Configure one SCI for 8 data bits, no parity, TX + RX enabled. */
static void sci_init(const sci_t *p, u16 scbr)
{
    *p->cr0 = (u16)(scbr & 0x1FFF);          /* 13-bit baud divisor */
    *p->cr1 = (u16)(SCCR1_TE | SCCR1_RE);    /* 8N1, transmit + receive on */
}

/* Move one byte src -> dst if a byte is waiting and the destination TX is
 * free. Reading the status then the data register clears RDRF. Returns
 * nonzero if a byte was forwarded. */
static int sci_pump(const sci_t *src, const sci_t *dst)
{
    u16 status = *src->sr;

    if (status & SCSR_RDRF) {
        if (*dst->sr & SCSR_TDRE) {
            *dst->dr = (u16)(*src->dr & 0x00FF);   /* read clears RDRF, write clears TDRE */
            return 1;
        }
        /* destination busy: leave the byte in the source RDR, retry next loop */
    }
    return 0;
}

/* Non-blocking single-byte read from one SCI. Returns nonzero and stores the
 * byte in *out if one was waiting (reading SCxDR clears RDRF). Used by the
 * handshake gate -- these bytes are consumed, not forwarded. */
static int sci_try_read(const sci_t *p, u8 *out)
{
    if (*p->sr & SCSR_RDRF) {
        *out = (u8)(*p->dr & 0x00FF);
        return 1;
    }
    return 0;
}

/* Blocking transmit of 'len' bytes out one SCI. Spins on TDRE before each
 * write (write to SCxDR clears TDRE) and services the watchdog while waiting
 * so a slow/low baud send can't trip a reset. Returns once the last byte has
 * been handed to the transmitter (it may still be shifting out). */
static void sci_send(const sci_t *p, const u8 *buf, u16 len)
{
    u16 i;
    for (i = 0; i < len; ++i) {
        while (!(*p->sr & SCSR_TDRE)) {
            wdt_service();
        }
        *p->dr = (u16)buf[i];
    }
}

/* The exact 24-byte sequence the HOST computer must send on UART1 to unlock the
 * bridge. Replace these placeholder bytes with your real key. */
static const u8 HANDSHAKE_KEY[HANDSHAKE_LEN] = {
    0x22, 0xDD, 0x55, 0xAA, 0x01,0x00, 0x05, 0x00, 
    0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x02, 0x00, 
    0x03, 0x00, 0x00, 0x00, 0x55, 0x05, 0xAA, 0x02
};

/* Wait up to timeout_ms for the full key[0..keylen-1] to arrive in order on
 * 'src'. Returns 1 if the whole sequence was matched in time, 0 on timeout.
 *
 * Matching is sequential with restart-on-mismatch: a wrong byte resets the
 * match, but if that byte equals key[0] it starts a fresh match at index 1.
 * (That covers the common single-byte overlap; for a key that overlaps itself
 * in more complex ways you'd want full KMP, but a chosen magic key won't.)
 *
 * The watchdog is serviced inside the loop so the wait itself can't trip a
 * reset. The deadline is a hard wall-clock window starting at entry (i.e.
 * starting right after the STM32 reset is released by the caller). */
static int wait_for_handshake(const sci_t *src, const u8 *key,
                              u16 keylen, u32 timeout_ms)
{
    u32 start    = tb_ticks();
    u32 deadline = MS_TO_TB(timeout_ms);   /* elapsed-tick budget */
    u16 idx      = 0;
    u8  b;

    while ((tb_ticks() - start) < deadline) {
        wdt_service();                     /* don't let the WDT reset us */

        if (sci_try_read(src, &b)) {
            if (b == key[idx]) {
                idx++;
                if (idx >= keylen) {
                    return 1;              /* full sequence matched */
                }
            } else {
                idx = (b == key[0]) ? 1u : 0u;   /* restart match */
            }
        }
    }

    return 0;                              /* timed out */
}

/* ------------------------------------------------------ main_uart_bridge */
/* Bring up both SCIs, reset the STM32, then wait for the handshake on UART1.
 *   - handshake received in time -> enter the infinite bridge (never returns)
 *   - timeout                    -> return UART_BRIDGE_NO_SYNC to the caller
 */
int main_uart_bridge(void)
{
    sci_t uart1;
    sci_t uart2;
    u16   scbr;

    uart1.cr0 = (volatile u16 *)SCC1R0_ADDR;
    uart1.cr1 = (volatile u16 *)SCC1R1_ADDR;
    uart1.sr  = (volatile u16 *)SC1SR_ADDR;
    uart1.dr  = (volatile u16 *)SC1DR_ADDR;

    uart2.cr0 = (volatile u16 *)SCC2R0_ADDR;
    uart2.cr1 = (volatile u16 *)SCC2R1_ADDR;
    uart2.sr  = (volatile u16 *)SC2SR_ADDR;
    uart2.dr  = (volatile u16 *)SC2DR_ADDR;

    /* Divisor is computed and validated at compile time (see BAUD_RATE guard).
     * sci_init masks it to the 13-bit SCBR field. */
    scbr = (u16)SCBR_FOR(BAUD_RATE);

    tb_enable();

    /* (1) PROG_EN parked at its configured level BEFORE the bridge inits,
     *     so it is already valid when the STM32 leaves reset below. NRST is
     *     configured here too and left released (high). */
    //stm32_progen_init(); // NO NEED For prog_en - it complicates the design
    stm32_nrst_init();

    qsmcm_pins_init();        /* route TXD1/2 + RXD1/2 to the SCIs */
    sci_init(&uart1, scbr);
    sci_init(&uart2, scbr);

    /* (2) Bridge is up -> reset the STM32 so it restarts and boots with
     *     PROG_EN already asserted. */
    stm32_nrst_pulse();

    /* (3) Gate: wait for the STM32 to announce itself with the 25-byte key
     *     on UART1 within HANDSHAKE_TIMEOUT_MS. Bail out if it never does. */
    if (!wait_for_handshake(&uart1, HANDSHAKE_KEY,
                            HANDSHAKE_LEN, HANDSHAKE_TIMEOUT_MS)) {
        return UART_BRIDGE_NO_SYNC;   /* no handshake -> return to caller */
    }

    /* (4) Handshake good -> forward the same key out UART2 so the UART2 device
     *     gets the sync it expects, then immediately bridge so its response
     *     flows straight back to UART1. */
    sci_send(&uart2, HANDSHAKE_KEY, HANDSHAKE_LEN);

    /* (5) Bridge forever. */
    for (;;) {
        wdt_service();            /* keep the watchdog from resetting us */
        sci_pump(&uart1, &uart2); /* UART1 -> UART2 */
        sci_pump(&uart2, &uart1); /* UART2 -> UART1 */
    }

    /* not reached */
}

/* ------------------------------------------------------------------ main */
/* Thin standalone entry. In your project you call main_uart_bridge() from
 * your own dispatcher and can delete this wrapper. If the gate times out we
 * just service the watchdog and spin so the part doesn't reset; replace this
 * with whatever the caller should do on UART_BRIDGE_NO_SYNC. */
/*
int main(void)
{
    if (main_uart_bridge() == UART_BRIDGE_NO_SYNC) {
        for (;;) {
            wdt_service();
        }
    }
    return 0;   // not reached on the success path (bridge loops forever) 
}
*/
