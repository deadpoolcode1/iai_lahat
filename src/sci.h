#ifndef SCI_H
#define SCI_H

#include <stdbool.h>
#include <stdint.h>

/* Two channels: QSCI1 (host) and QSCI2 (target). */
typedef enum {
    SCI_CH_HOST   = 0,
    SCI_CH_TARGET = 1,
} sci_channel_t;

void sci_init(sci_channel_t ch, uint32_t baud, uint32_t f_sys_hz);
bool sci_rx_ready(sci_channel_t ch);
bool sci_tx_ready(sci_channel_t ch);
uint8_t sci_read_byte(sci_channel_t ch);
void sci_write_byte(sci_channel_t ch, uint8_t b);

#endif
