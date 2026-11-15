#include "mouse.h"
#include "idt.h"
#include "io.h"

static volatile int32_t mouse_x = 320;
static volatile int32_t mouse_y = 240;
static volatile uint8_t mouse_btns = 0;
static volatile uint8_t mouse_clicked = 0;
static volatile bool    mouse_moved = false;
static int32_t max_width = 1024;
static int32_t max_height = 768;

static uint8_t mouse_cycle = 0;
static int8_t  mouse_packet[3];

static void ps2_wait_write(void) {
    uint32_t t = 100000;
    while ((inb(0x64) & 0x02) && --t) {}
}

static void ps2_wait_read(void) {
    uint32_t t = 100000;
    while (!(inb(0x64) & 0x01) && --t) {}
}

static void mouse_write(uint8_t val) {
    ps2_wait_write();
    outb(0x64, 0xD4);
    ps2_wait_write();
    outb(0x60, val);
}

static uint8_t mouse_read(void) {
    ps2_wait_read();
    return inb(0x60);
}

static void mouse_irq(registers_t* regs) {
    UNUSED(regs);
    uint8_t status = inb(0x64);
    if (!(status & 0x01) || !(status & 0x20)) return;

    uint8_t data = inb(0x60);
    if (mouse_cycle == 0) {
        if (!(data & 0x08)) return; /* Always-1 sync bit */
        mouse_packet[0] = (int8_t)data;
        mouse_cycle = 1;
    } else if (mouse_cycle == 1) {
        mouse_packet[1] = (int8_t)data;
        mouse_cycle = 2;
    } else {
        mouse_packet[2] = (int8_t)data;
        mouse_cycle = 0;

        uint8_t flags = (uint8_t)mouse_packet[0];
        if (flags & 0xC0) return; /* Overflow */

        int32_t dx = (int32_t)mouse_packet[1];
        int32_t dy = (int32_t)mouse_packet[2];

        mouse_x += dx;
        mouse_y -= dy;

        if (mouse_x < 0) mouse_x = 0;
        if (mouse_y < 0) mouse_y = 0;
        if (mouse_x >= max_width)  mouse_x = max_width - 1;
        if (mouse_y >= max_height) mouse_y = max_height - 1;

        uint8_t new_btns = flags & 0x07;
        mouse_clicked |= (uint8_t)(new_btns & ~mouse_btns);
        mouse_btns = new_btns;
        mouse_moved = true;
    }
}

void mouse_init(int32_t max_w, int32_t max_h) {
    max_width  = max_w > 0 ? max_w : 1024;
    max_height = max_h > 0 ? max_h : 768;
    mouse_x = max_width / 2;
    mouse_y = max_height / 2;

    /* Enable auxiliary PS/2 port */
    ps2_wait_write();
    outb(0x64, 0xA8);

    /* Enable IRQ12 in controller command byte */
    ps2_wait_write();
    outb(0x64, 0x20);
    ps2_wait_read();
    uint8_t cfg = inb(0x60);
    cfg |= 0x02;
    cfg &= ~0x20;
    ps2_wait_write();
    outb(0x64, 0x60);
    ps2_wait_write();
    outb(0x60, cfg);

    /* Defaults + Enable Packet Streaming */
    mouse_write(0xF6);
    mouse_read();
    mouse_write(0xF4);
    mouse_read();

    irq_register(12, mouse_irq);
}

void mouse_get_state(mouse_state_t* out) {
    out->x = mouse_x;
    out->y = mouse_y;
    out->buttons = mouse_btns;
    out->clicked = mouse_clicked;
    out->moved = mouse_moved;
    mouse_clicked = 0;
    mouse_moved = false;
}
