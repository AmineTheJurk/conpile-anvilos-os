#ifndef ANVIL_ANV_H
#define ANVIL_ANV_H

#ifdef __ANVIL_KERNEL__
#include "types.h"
#else
#include <stdint.h>
#endif

/*
 * AnvilOS Custom Executable Format (.anv)
 * Magic: "ANV1" (0x31564E41 little-endian)
 * Position-independent 32-bit x86 machine code + rodata + bss
 * Syscall ABI via INT 0x80 (EAX=nr, EBX=a1, ECX=a2, EDX=a3)
 */
#define ANV_MAGIC       0x31564E41
#define ANV_VERSION     1
#define ANV_ARCH_X86_32 1
#define ANV_FLAG_CONSOLE 0x01
#define ANV_FLAG_GUI     0x02

typedef struct __attribute__((packed)) {
    uint32_t magic;       /* 0x31564E41 ("ANV1") */
    uint16_t version;     /* 1 */
    uint16_t arch;        /* ANV_ARCH_X86_32 */
    uint32_t flags;       /* ANV_FLAG_* */
    uint32_t entry_off;   /* Entry offset inside code section */
    uint32_t code_size;   /* Code section byte length */
    uint32_t rodata_size; /* Rodata section byte length */
    uint32_t bss_size;    /* Zero-initialized data length */
    uint32_t stack_size;  /* Suggested stack size */
    char     app_name[32];/* Program name */
} anv_header_t;           /* 64 bytes exact */

/* Syscall numbers shared between kernel and agcc compiler */
#ifdef SYS_OPEN
#undef SYS_OPEN
#endif
#define SYS_EXIT      1
#define SYS_READ      3
#define SYS_WRITE     4
#define SYS_OPEN      5
#define SYS_CLOSE     6
#define SYS_GETPID    20
#define SYS_UPTIME    24
#define SYS_MKDIR     39
#define SYS_PUTCHAR    40
#define SYS_PRINT_INT  41

#ifdef __ANVIL_KERNEL__
typedef void (*anv_stdout_cb_t)(const char* text, uint32_t len);
int anv_exec_file(const char* path, anv_stdout_cb_t stdout_cb, int* exit_code_out);
#endif

#endif
