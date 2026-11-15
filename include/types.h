#ifndef ANVIL_TYPES_H
#define ANVIL_TYPES_H

typedef unsigned char      uint8_t;
typedef signed char        int8_t;
typedef unsigned short     uint16_t;
typedef signed short       int16_t;
typedef unsigned int       uint32_t;
typedef signed int         int32_t;
typedef unsigned long long uint64_t;
typedef signed long long   int64_t;

typedef uint32_t size_t;
typedef int32_t  ssize_t;
typedef uint32_t uintptr_t;

/* In C23 (default in GCC 15+) bool/true/false are keywords; only define them for older standards. */
#if !defined(__STDC_VERSION__) || __STDC_VERSION__ <= 201710L
typedef uint8_t bool;
#define true  1
#define false 0
#endif
#define NULL  ((void*)0)

#define UNUSED(x) ((void)(x))
#define ALIGN_UP(x, a)   (((x) + ((a) - 1)) & ~((a) - 1))
#define ALIGN_DOWN(x, a) ((x) & ~((a) - 1))

#endif
