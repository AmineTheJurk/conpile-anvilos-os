#include "task.h"
#include "timer.h"
#include "serial.h"
#include "vfs.h"
#include "gui.h"
#include "string.h"

static task_info_t tasks[MAX_TASKS];
static int current_pid = 1;
static uint32_t next_pid = 2;
static anv_stdout_cb_t active_stdout = NULL;

extern volatile bool     g_anv_exited;
extern volatile int32_t  g_anv_exit_code;
extern void anv_exit_trampoline(void);

void task_init(void) {
    memset(tasks, 0, sizeof(tasks));
    tasks[0].pid        = 1;
    strcpy(tasks[0].name, "kernel/init");
    tasks[0].state      = TASK_RUNNING;
    tasks[0].start_tick = 0;
    tasks[0].mem_bytes  = 131072;
    current_pid = 1;
}

int task_register(const char* name, uint32_t mem_bytes) {
    for (int i = 0; i < MAX_TASKS; i++) {
        if (tasks[i].state == TASK_UNUSED || tasks[i].state == TASK_ZOMBIE) {
            tasks[i].pid = (int)next_pid++;
            strncpy(tasks[i].name, name, 31);
            tasks[i].name[31] = '\0';
            tasks[i].state = TASK_RUNNING;
            tasks[i].start_tick = timer_ticks();
            tasks[i].mem_bytes = mem_bytes;
            current_pid = (int)tasks[i].pid;
            return (int)tasks[i].pid;
        }
    }
    return -1;
}

void task_finish(int pid) {
    for (int i = 0; i < MAX_TASKS; i++) {
        if ((int)tasks[i].pid == pid) {
            tasks[i].state = TASK_ZOMBIE;
        }
    }
    current_pid = 1;
}

int task_list(task_info_t* out, int max_out) {
    int count = 0;
    for (int i = 0; i < MAX_TASKS && count < max_out; i++) {
        if (tasks[i].state != TASK_UNUSED) {
            out[count++] = tasks[i];
        }
    }
    return count;
}

int task_current_pid(void) {
    return current_pid;
}

void task_set_stdout(anv_stdout_cb_t cb) {
    active_stdout = cb;
}

static void emit_out(const char* s, uint32_t len) {
    for (uint32_t i = 0; i < len; i++) {
        serial_putc(s[i]);
    }
    if (active_stdout) {
        active_stdout(s, len);
    }
}

void syscall_handler(registers_t* regs) {
    uint32_t num = regs->eax;
    uint32_t a1  = regs->ebx;
    uint32_t a2  = regs->ecx;
    uint32_t a3  = regs->edx;

    switch (num) {
        case SYS_EXIT:
            g_anv_exit_code = (int32_t)a1;
            g_anv_exited = true;
            regs->eip = (uint32_t)anv_exit_trampoline;
            break;

        case SYS_WRITE: {
            int fd = (int)a1;
            const char* buf = (const char*)a2;
            uint32_t len = a3;
            if (!buf) { regs->eax = (uint32_t)-1; break; }
            if (fd == 1 || fd == 2) {
                emit_out(buf, len);
                regs->eax = len;
            } else {
                regs->eax = (uint32_t)vfs_write(fd, buf, len);
            }
            break;
        }

        case SYS_READ: {
            int fd = (int)a1;
            void* buf = (void*)a2;
            uint32_t len = a3;
            regs->eax = (uint32_t)vfs_read(fd, buf, len);
            break;
        }

        case SYS_OPEN:
            regs->eax = (uint32_t)vfs_open((const char*)a1, (int)a2);
            break;

        case SYS_CLOSE:
            regs->eax = (uint32_t)vfs_close((int)a1);
            break;

        case SYS_GETPID:
            regs->eax = (uint32_t)current_pid;
            break;

        case SYS_UPTIME:
            regs->eax = timer_uptime_ms();
            break;

        case SYS_MKDIR:
            regs->eax = vfs_mkdir((const char*)a1) ? 0 : (uint32_t)-1;
            break;

        case SYS_PUTCHAR: {
            char ch = (char)(a1 & 0xFF);
            emit_out(&ch, 1);
            regs->eax = 1;
            break;
        }

        case SYS_PRINT_INT: {
            char tmp[24];
            snprintf(tmp, sizeof(tmp), "%d", (int32_t)a1);
            emit_out(tmp, (uint32_t)strlen(tmp));
            regs->eax = (uint32_t)strlen(tmp);
            break;
        }

        default:
            regs->eax = (uint32_t)-1;
            break;
    }
}
