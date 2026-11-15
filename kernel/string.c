#include "string.h"

void* memcpy(void* dst, const void* src, size_t n) {
    uint8_t* d = (uint8_t*)dst;
    const uint8_t* s = (const uint8_t*)src;
    while (n--) *d++ = *s++;
    return dst;
}

void* memmove(void* dst, const void* src, size_t n) {
    uint8_t* d = (uint8_t*)dst;
    const uint8_t* s = (const uint8_t*)src;
    if (d < s) {
        while (n--) *d++ = *s++;
    } else if (d > s) {
        d += n;
        s += n;
        while (n--) *--d = *--s;
    }
    return dst;
}

void* memset(void* dst, int c, size_t n) {
    uint8_t* d = (uint8_t*)dst;
    while (n--) *d++ = (uint8_t)c;
    return dst;
}

int memcmp(const void* a, const void* b, size_t n) {
    const uint8_t* p = (const uint8_t*)a;
    const uint8_t* q = (const uint8_t*)b;
    for (size_t i = 0; i < n; i++) {
        if (p[i] != q[i]) return (int)p[i] - (int)q[i];
    }
    return 0;
}

size_t strlen(const char* s) {
    size_t len = 0;
    while (s && s[len]) len++;
    return len;
}

int strcmp(const char* a, const char* b) {
    while (*a && (*a == *b)) { a++; b++; }
    return (int)(uint8_t)*a - (int)(uint8_t)*b;
}

int strncmp(const char* a, const char* b, size_t n) {
    for (size_t i = 0; i < n; i++) {
        if (a[i] != b[i] || !a[i] || !b[i]) {
            return (int)(uint8_t)a[i] - (int)(uint8_t)b[i];
        }
    }
    return 0;
}

char* strcpy(char* dst, const char* src) {
    char* r = dst;
    while ((*dst++ = *src++));
    return r;
}

char* strncpy(char* dst, const char* src, size_t n) {
    size_t i = 0;
    for (; i < n && src[i]; i++) dst[i] = src[i];
    for (; i < n; i++) dst[i] = '\0';
    return dst;
}

char* strcat(char* dst, const char* src) {
    char* p = dst + strlen(dst);
    while ((*p++ = *src++));
    return dst;
}

char* strchr(const char* s, int c) {
    while (*s) {
        if (*s == (char)c) return (char*)s;
        s++;
    }
    return (c == 0) ? (char*)s : NULL;
}

char* strrchr(const char* s, int c) {
    const char* last = NULL;
    while (*s) {
        if (*s == (char)c) last = s;
        s++;
    }
    if (c == 0) return (char*)s;
    return (char*)last;
}

static void append_ch(char* buf, size_t size, size_t* pos, char c) {
    if (*pos + 1 < size) buf[*pos] = c;
    (*pos)++;
}

static void append_str(char* buf, size_t size, size_t* pos, const char* s, int pad_width, bool left_align) {
    if (!s) s = "(null)";
    int len = (int)strlen(s);
    if (!left_align) {
        for (int i = len; i < pad_width; i++) append_ch(buf, size, pos, ' ');
    }
    while (*s) append_ch(buf, size, pos, *s++);
    if (left_align) {
        for (int i = len; i < pad_width; i++) append_ch(buf, size, pos, ' ');
    }
}

static void append_uint(char* buf, size_t size, size_t* pos, uint32_t val, int base, bool upper, int pad_width, char pad_char, bool left_align, bool negative) {
    char tmp[32];
    const char* digits = upper ? "0123456789ABCDEF" : "0123456789abcdef";
    int idx = 0;
    if (val == 0) {
        tmp[idx++] = '0';
    } else {
        while (val > 0) {
            tmp[idx++] = digits[val % (uint32_t)base];
            val /= (uint32_t)base;
        }
    }
    int total_len = idx + (negative ? 1 : 0);
    if (left_align) {
        if (negative) append_ch(buf, size, pos, '-');
        while (idx > 0) append_ch(buf, size, pos, tmp[--idx]);
        for (int i = total_len; i < pad_width; i++) append_ch(buf, size, pos, ' ');
    } else {
        if (pad_char == '0') {
            if (negative) append_ch(buf, size, pos, '-');
            for (int i = total_len; i < pad_width; i++) append_ch(buf, size, pos, '0');
        } else {
            for (int i = total_len; i < pad_width; i++) append_ch(buf, size, pos, ' ');
            if (negative) append_ch(buf, size, pos, '-');
        }
        while (idx > 0) append_ch(buf, size, pos, tmp[--idx]);
    }
}

int snprintf(char* buf, size_t size, const char* fmt, ...) {
    __builtin_va_list ap;
    __builtin_va_start(ap, fmt);
    size_t pos = 0;

    while (*fmt) {
        if (*fmt != '%') {
            append_ch(buf, size, &pos, *fmt++);
            continue;
        }
        fmt++;
        bool left_align = false;
        char pad_char = ' ';
        int pad_width = 0;
        while (*fmt == '-' || *fmt == '0') {
            if (*fmt == '-') left_align = true;
            else if (*fmt == '0' && !left_align) pad_char = '0';
            fmt++;
        }
        while (*fmt >= '0' && *fmt <= '9') {
            pad_width = pad_width * 10 + (*fmt - '0');
            fmt++;
        }
        if (left_align) pad_char = ' ';
        switch (*fmt) {
            case 's':
                append_str(buf, size, &pos, __builtin_va_arg(ap, const char*), pad_width, left_align);
                break;
            case 'c': {
                char c = (char)__builtin_va_arg(ap, int);
                if (!left_align) {
                    for (int i = 1; i < pad_width; i++) append_ch(buf, size, &pos, ' ');
                }
                append_ch(buf, size, &pos, c);
                if (left_align) {
                    for (int i = 1; i < pad_width; i++) append_ch(buf, size, &pos, ' ');
                }
                break;
            }
            case 'd':
            case 'i': {
                int32_t v = __builtin_va_arg(ap, int32_t);
                bool neg = (v < 0);
                uint32_t uv = neg ? (uint32_t)(-(int64_t)v) : (uint32_t)v;
                append_uint(buf, size, &pos, uv, 10, false, pad_width, pad_char, left_align, neg);
                break;
            }
            case 'u':
                append_uint(buf, size, &pos, __builtin_va_arg(ap, uint32_t), 10, false, pad_width, pad_char, left_align, false);
                break;
            case 'x':
                append_uint(buf, size, &pos, __builtin_va_arg(ap, uint32_t), 16, false, pad_width, pad_char, left_align, false);
                break;
            case 'X':
                append_uint(buf, size, &pos, __builtin_va_arg(ap, uint32_t), 16, true, pad_width, pad_char, left_align, false);
                break;
            case 'p':
                append_str(buf, size, &pos, "0x", 0, false);
                append_uint(buf, size, &pos, __builtin_va_arg(ap, uint32_t), 16, false, 8, '0', false, false);
                break;
            case '%':
                append_ch(buf, size, &pos, '%');
                break;
            default:
                append_ch(buf, size, &pos, '%');
                if (*fmt) append_ch(buf, size, &pos, *fmt);
                break;
        }
        if (*fmt) fmt++;
    }

    if (size > 0) {
        buf[(pos < size) ? pos : (size - 1)] = '\0';
    }
    __builtin_va_end(ap);
    return (int)pos;
}
