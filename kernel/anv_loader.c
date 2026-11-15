#include "anv.h"
#include "vfs.h"
#include "mem.h"
#include "task.h"
#include "string.h"
#include "serial.h"

volatile bool    g_anv_exited    = false;
volatile int32_t g_anv_exit_code = 0;

static uint32_t saved_esp = 0;
static uint32_t saved_ebp = 0;
static uint32_t saved_ret = 0;

void anv_exit_trampoline(void) {
    __asm__ volatile (
        "movl %0, %%esp\n\t"
        "movl %1, %%ebp\n\t"
        "movl %2, %%eax\n\t"
        "jmpl *%3\n\t"
        :
        : "m"(saved_esp), "m"(saved_ebp), "m"(g_anv_exit_code), "m"(saved_ret)
    );
}

static int32_t invoke_anv_entry(uint32_t entry_addr) {
    int32_t ret = 0;
    __asm__ volatile (
        "pusha\n\t"
        "mov %%esp, %1\n\t"
        "mov %%ebp, %2\n\t"
        "mov $1f, %3\n\t"
        "call *%4\n\t"
        "mov %%eax, %0\n\t"
        "jmp 2f\n\t"
        "1:\n\t"
        "mov %5, %%eax\n\t"
        "mov %%eax, %0\n\t"
        "2:\n\t"
        "popa\n\t"
        : "=m"(ret), "=m"(saved_esp), "=m"(saved_ebp), "=m"(saved_ret)
        : "r"(entry_addr), "m"(g_anv_exit_code)
        : "eax", "memory"
    );
    return ret;
}

int anv_exec_file(const char* path, anv_stdout_cb_t stdout_cb, int* exit_code_out) {
    vfs_stat_t st;
    if (!vfs_stat(path, &st) || st.type != ANVILFS_TYPE_FILE) {
        return -1;
    }
    if (st.size < sizeof(anv_header_t) || st.size > 65536) {
        return -2;
    }

    uint8_t* raw = (uint8_t*)kmalloc(st.size);
    if (!raw) return -3;

    int fd = vfs_open(path, O_RDONLY);
    if (fd < 0) {
        kfree(raw);
        return -4;
    }
    int got = vfs_read(fd, raw, st.size);
    vfs_close(fd);
    if (got < (int)sizeof(anv_header_t)) {
        kfree(raw);
        return -5;
    }

    const anv_header_t* hdr = (const anv_header_t*)raw;
    if (hdr->magic != ANV_MAGIC || hdr->version != ANV_VERSION || hdr->arch != ANV_ARCH_X86_32) {
        kfree(raw);
        return -6;
    }

    uint32_t payload_need = hdr->code_size + hdr->rodata_size;
    if (sizeof(anv_header_t) + payload_need > (uint32_t)got) {
        kfree(raw);
        return -7;
    }
    if (hdr->entry_off >= hdr->code_size) {
        kfree(raw);
        return -8;
    }

    uint32_t total_mem = payload_need + hdr->bss_size + 4096;
    uint8_t* image = (uint8_t*)kmalloc(total_mem);
    if (!image) {
        kfree(raw);
        return -9;
    }

    memset(image, 0, total_mem);
    memcpy(image, raw + sizeof(anv_header_t), payload_need);
    kfree(raw);

    int pid = task_register(hdr->app_name[0] ? hdr->app_name : path, total_mem);
    task_set_stdout(stdout_cb);

    g_anv_exited = false;
    g_anv_exit_code = 0;

    serial_printf("[ANV] Exec '%s' (pid=%d, code=%uB, rodata=%uB)\n",
                  hdr->app_name, pid, hdr->code_size, hdr->rodata_size);

    int32_t rc = invoke_anv_entry((uint32_t)(image + hdr->entry_off));

    task_set_stdout(NULL);
    if (pid > 0) task_finish(pid);
    kfree(image);

    if (exit_code_out) *exit_code_out = (int)rc;
    return 0;
}
