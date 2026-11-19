#include "timer.h"
#include "idt.h"
#include "io.h"
#include "netstack.h"

static volatile uint32_t ticks = 0;
static uint32_t tick_hz = 100;

static void timer_irq(registers_t* regs) {
    UNUSED(regs);
    ticks++;
    /* Poll the NIC receive ring; driver poll paths must remain bounded and nonblocking. */
    netstack_poll();
}

void timer_init(uint32_t freq_hz) {
    if (freq_hz == 0) freq_hz = 100;
    tick_hz = freq_hz;
    uint32_t divisor = 1193180 / freq_hz;
    outb(0x43, 0x36);
    outb(0x40, (uint8_t)(divisor & 0xFF));
    outb(0x40, (uint8_t)((divisor >> 8) & 0xFF));
    irq_register(0, timer_irq);
}

uint32_t timer_ticks(void) { return ticks; }
uint32_t timer_uptime_ms(void) { return (ticks * 1000) / tick_hz; }

void timer_sleep_ms(uint32_t ms) {
    uint32_t target = ticks + (ms * tick_hz) / 1000;
    if (target == ticks) target++;
    while (ticks < target) hlt();
}
