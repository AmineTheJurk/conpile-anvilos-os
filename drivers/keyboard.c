#include "keyboard.h"
#include "idt.h"
#include "io.h"

#define KBD_BUF_SIZE 128

static volatile char     kbd_buf[KBD_BUF_SIZE];
static volatile uint32_t kbd_head = 0;
static volatile uint32_t kbd_tail = 0;
static bool shift_down = false;
static bool caps_lock  = false;
static bool ext_prefix = false;

static const char scancode_normal[128] = {
    0, 27, '1','2','3','4','5','6','7','8','9','0','-','=','\b',
    '\t','q','w','e','r','t','y','u','i','o','p','[',']','\n',
    0,'a','s','d','f','g','h','j','k','l',';','\'','`',
    0,'\\','z','x','c','v','b','n','m',',','.','/',0,
    '*',0,' ',0,0,0,0,0,0,0,0,0,0,0,0,0,
    '7','8','9','-','4','5','6','+','1','2','3','0','.',0,0,0
};

static const char scancode_shift[128] = {
    0, 27, '!','@','#','$','%','^','&','*','(',')','_','+','\b',
    '\t','Q','W','E','R','T','Y','U','I','O','P','{','}','\n',
    0,'A','S','D','F','G','H','J','K','L',':','"','~',
    0,'|','Z','X','C','V','B','N','M','<','>','?',0,
    '*',0,' ',0,0,0,0,0,0,0,0,0,0,0,0,0,
    '7','8','9','-','4','5','6','+','1','2','3','0','.',0,0,0
};

static void kbd_push(char c) {
    uint32_t next = (kbd_head + 1) % KBD_BUF_SIZE;
    if (next != kbd_tail) {
        kbd_buf[kbd_head] = c;
        kbd_head = next;
    }
}

static void kbd_irq(registers_t* regs) {
    UNUSED(regs);
    uint8_t sc = inb(0x60);

    if (sc == 0xE0) {
        ext_prefix = true;
        return;
    }

    if (ext_prefix) {
        ext_prefix = false;
        if (!(sc & 0x80)) {
            if (sc == 0x48) kbd_push((char)KEY_UP);
            else if (sc == 0x50) kbd_push((char)KEY_DOWN);
            else if (sc == 0x4B) kbd_push((char)KEY_LEFT);
            else if (sc == 0x4D) kbd_push((char)KEY_RIGHT);
        }
        return;
    }

    if (sc == 0x2A || sc == 0x36) { shift_down = true;  return; }
    if (sc == 0xAA || sc == 0xB6) { shift_down = false; return; }
    if (sc == 0x3A) { caps_lock = !caps_lock; return; }

    if (sc & 0x80) return; /* Key release */

    if (sc < 128) {
        char c = shift_down ? scancode_shift[sc] : scancode_normal[sc];
        if (caps_lock && c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
        else if (caps_lock && c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
        if (c) kbd_push(c);
    }
}

void keyboard_init(void) {
    kbd_head = 0;
    kbd_tail = 0;
    irq_register(1, kbd_irq);
}

bool keyboard_has_char(void) {
    return kbd_head != kbd_tail;
}

char keyboard_getc(void) {
    if (kbd_head == kbd_tail) return 0;
    char c = kbd_buf[kbd_tail];
    kbd_tail = (kbd_tail + 1) % KBD_BUF_SIZE;
    return c;
}
