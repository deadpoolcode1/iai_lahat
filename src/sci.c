#include "sci.h"
#include "mpc555.h"

/* Per-channel register block. */
typedef struct {
    uint32_t cr0;   /* SCCRn0 — baud (SCBR in low 13 bits) */
    uint32_t cr1;   /* SCCRn1 — enables */
    uint32_t sr;    /* status               */
    uint32_t dr;    /* data                 */
} sci_regs_t;

static const sci_regs_t SCI_REGS[2] = {
    {  /* SCI_CH_HOST   = QSCI1 */
        .cr0 = QSCI1CR0_ADDR,
        .cr1 = QSCI1CR1_ADDR,
        .sr  = QSCI1SR_ADDR,
        .dr  = QSCI1DR_ADDR,
    },
    {  /* SCI_CH_TARGET = QSCI2 */
        .cr0 = QSCI2CR0_ADDR,
        .cr1 = QSCI2CR1_ADDR,
        .sr  = QSCI2SR_ADDR,
        .dr  = QSCI2DR_ADDR,
    },
};

void sci_init(sci_channel_t ch, uint32_t baud, uint32_t f_sys_hz)
{
    const sci_regs_t *r = &SCI_REGS[ch];

    /* MPC555 SCI baud-rate formula: baud = f_sys / (32 * SCBR), so
     * SCBR = f_sys / (32 * baud). SCBR is a 13-bit field [12:0].
     * Round to nearest. */
    uint32_t scbr = (f_sys_hz + (16u * baud)) / (32u * baud);
    if (scbr == 0)        scbr = 1;
    if (scbr > 0x1FFFu)   scbr = 0x1FFFu;

    /* CR0: SCBR only — 8-N-1, no parity. */
    mmio_write16(r->cr0, (uint16_t)scbr);

    /* CR1: enable transmitter and receiver, 8-bit data, no interrupts. */
    mmio_write16(r->cr1, SCCR1_TE | SCCR1_RE);
}

bool sci_rx_ready(sci_channel_t ch)
{
    return (mmio_read16(SCI_REGS[ch].sr) & SCSR_RDRF) != 0;
}

bool sci_tx_ready(sci_channel_t ch)
{
    return (mmio_read16(SCI_REGS[ch].sr) & SCSR_TDRE) != 0;
}

uint8_t sci_read_byte(sci_channel_t ch)
{
    /* Reading the data register clears RDRF. */
    return (uint8_t)(mmio_read16(SCI_REGS[ch].dr) & 0x00FFu);
}

void sci_write_byte(sci_channel_t ch, uint8_t b)
{
    while (!sci_tx_ready(ch)) {
        /* spin */
    }
    mmio_write16(SCI_REGS[ch].dr, b);
}
