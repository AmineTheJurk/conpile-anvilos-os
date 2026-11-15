#ifndef ANVIL_IDT_H
#define ANVIL_IDT_H

#include "types.h"

typedef struct __attribute__((packed)) {
    uint32_t ds;
    uint32_t edi, esi, ebp, esp, ebx, edx, ecx, eax;
    uint32_t int_no, err_code;
    uint32_t eip, cs, eflags, useresp, ss;
} registers_t;

typedef void (*isr_handler_t)(registers_t* regs);

void idt_init(void);
void irq_register(uint8_t irq, isr_handler_t handler);

#endif
