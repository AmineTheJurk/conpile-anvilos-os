#include "serial.h"
#include "io.h"
#include "string.h"

#define COM1 0x3F8

void serial_init(void) {
    outb(COM1 + 1, 0x00); /* Disable interrupts */
    outb(COM1 + 3, 0x80); /* Enable DLAB */
    outb(COM1 + 0, 0x03); /* 38400 baud */
    outb(COM1 + 1, 0x00);
    outb(COM1 + 3, 0x03); /* 8N1 */
    outb(COM1 + 2, 0xC7); /* Enable FIFO */
    outb(COM1 + 4, 0x0B); /* IRQs enabled, RTS/DSR set */
}

void serial_putc(char c) {
    if (c == '\n') serial_putc('\r');
    uint32_t timeout = 100000;
    while (!(inb(COM1 + 5) & 0x20) && --timeout) {}
    outb(COM1, (uint8_t)c);
}

void serial_puts(const char* s) {
    while (s && *s) serial_putc(*s++);
}

bool serial_has_char(void) {
    return (inb(COM1 + 5) & 0x01) != 0;
}

char serial_getc(void) {
    while (!serial_has_char()) {}
    return (char)inb(COM1);
}

void serial_printf(const char* fmt, ...) {
    char buf[512];
    __builtin_va_list ap;
    __builtin_va_start(ap, fmt);
    size_t pos = 0;
    while (*fmt && pos + 1 < sizeof(buf)) {
        if (*fmt != '%') {
            buf[pos++] = *fmt++;
            continue;
        }
        fmt++;
        bool left = false;
        char pad = ' ';
        int width = 0;
        while (*fmt == '-' || *fmt == '0') {
            if (*fmt == '-') left = true;
            else if (*fmt == '0' && !left) pad = '0';
            fmt++;
        }
        while (*fmt >= '0' && *fmt <= '9') { width = width * 10 + (*fmt - '0'); fmt++; }
        if (left) pad = ' ';
        char tmp[64];
        if (*fmt == 's') {
            const char* s = __builtin_va_arg(ap, const char*);
            if (!s) s = "(null)";
            int len = (int)strlen(s);
            if (!left) {
                for (int i = len; i < width && pos + 1 < sizeof(buf); i++) buf[pos++] = ' ';
            }
            while (*s && pos + 1 < sizeof(buf)) buf[pos++] = *s++;
            if (left) {
                for (int i = len; i < width && pos + 1 < sizeof(buf); i++) buf[pos++] = ' ';
            }
        } else if (*fmt == 'c') {
            buf[pos++] = (char)__builtin_va_arg(ap, int);
        } else if (*fmt == 'd' || *fmt == 'i') {
            int32_t v = __builtin_va_arg(ap, int32_t);
            snprintf(tmp, sizeof(tmp), "%d", v);
            int len = (int)strlen(tmp);
            if (!left) {
                for (int i = len; i < width && pos + 1 < sizeof(buf); i++) buf[pos++] = pad;
            }
            for (int i = 0; tmp[i] && pos + 1 < sizeof(buf); i++) buf[pos++] = tmp[i];
            if (left) {
                for (int i = len; i < width && pos + 1 < sizeof(buf); i++) buf[pos++] = ' ';
            }
        } else if (*fmt == 'u') {
            uint32_t v = __builtin_va_arg(ap, uint32_t);
            snprintf(tmp, sizeof(tmp), "%u", v);
            int len = (int)strlen(tmp);
            if (!left) {
                for (int i = len; i < width && pos + 1 < sizeof(buf); i++) buf[pos++] = pad;
            }
            for (int i = 0; tmp[i] && pos + 1 < sizeof(buf); i++) buf[pos++] = tmp[i];
            if (left) {
                for (int i = len; i < width && pos + 1 < sizeof(buf); i++) buf[pos++] = ' ';
            }
        } else if (*fmt == 'x' || *fmt == 'X' || *fmt == 'p') {
            uint32_t v = __builtin_va_arg(ap, uint32_t);
            const char* hex = (*fmt == 'X') ? "0123456789ABCDEF" : "0123456789abcdef";
            int len = 0;
            if (v == 0) tmp[len++] = '0';
            else {
                while (v) { tmp[len++] = hex[v & 0xF]; v >>= 4; }
            }
            if (*fmt == 'p') { width = 8; pad = '0'; left = false; }
            while (len < width && len < 31 && !left) tmp[len++] = pad;
            int out_len = len;
            while (len > 0 && pos + 1 < sizeof(buf)) buf[pos++] = tmp[--len];
            if (left) {
                for (int i = out_len; i < width && pos + 1 < sizeof(buf); i++) buf[pos++] = ' ';
            }
        } else if (*fmt == '%') {
            buf[pos++] = '%';
        }
        if (*fmt) fmt++;
    }
    buf[pos] = '\0';
    __builtin_va_end(ap);
    serial_puts(buf);
}
