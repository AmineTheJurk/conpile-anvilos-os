#ifndef ANVIL_TASK_H
#define ANVIL_TASK_H

#include "types.h"
#include "idt.h"
#include "anv.h"

#define MAX_TASKS 16

typedef enum {
    TASK_UNUSED = 0,
    TASK_READY,
    TASK_RUNNING,
    TASK_ZOMBIE
} task_state_t;

typedef struct {
    uint32_t     pid;
    char         name[32];
    task_state_t state;
    uint32_t     start_tick;
    uint32_t     mem_bytes;
} task_info_t;

void task_init(void);
int  task_register(const char* name, uint32_t mem_bytes);
void task_finish(int pid);
int  task_list(task_info_t* out, int max_out);
int  task_current_pid(void);
void task_set_stdout(anv_stdout_cb_t cb);
void syscall_handler(registers_t* regs);

#endif
