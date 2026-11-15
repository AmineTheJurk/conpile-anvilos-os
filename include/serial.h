#ifndef ANVIL_SERIAL_H
#define ANVIL_SERIAL_H

#include "types.h"

void serial_init(void);
void serial_putc(char c);
void serial_puts(const char* s);
void serial_printf(const char* fmt, ...);
bool serial_has_char(void);
char serial_getc(void);

#endif
