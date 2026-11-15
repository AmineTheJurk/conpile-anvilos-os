#ifndef ANVIL_TIMER_H
#define ANVIL_TIMER_H

#include "types.h"

void     timer_init(uint32_t freq_hz);
uint32_t timer_ticks(void);
uint32_t timer_uptime_ms(void);
void     timer_sleep_ms(uint32_t ms);

#endif
