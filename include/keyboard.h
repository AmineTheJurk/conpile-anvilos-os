#ifndef ANVIL_KEYBOARD_H
#define ANVIL_KEYBOARD_H

#include "types.h"

#define KEY_UP    0x80
#define KEY_DOWN  0x81
#define KEY_LEFT  0x82
#define KEY_RIGHT 0x83

void keyboard_init(void);
bool keyboard_has_char(void);
char keyboard_getc(void);

#endif
