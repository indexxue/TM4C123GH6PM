/**
 * @file type.h
 * @brief 固件通用宽度别名与 status_t（TM4C123 / FreeRTOS）。
 */

#ifndef COMMON_TYPE_H
#define COMMON_TYPE_H

#ifndef TYPE_H
#define TYPE_H

#include <stddef.h>
#include <stdint.h>

typedef void void_t;
typedef char char_t;
typedef signed char s8_t;
typedef unsigned char u8_t;
typedef int16_t s16_t;
typedef uint16_t u16_t;
typedef int32_t s32_t;
typedef uint32_t u32_t;
typedef int64_t s64_t;
typedef uint64_t u64_t;
typedef float f32_t;
typedef double f64_t;
typedef size_t usize_t;
typedef void_t *ptr_t;
typedef u8_t bool_t;

#define FALSE ((bool_t)0U)
#define TRUE  ((bool_t)1U)

#ifndef NULL_PTR
#define NULL_PTR ((void_t *)0)
#endif

#define UNUSED(param) ((void_t)(param))

#endif /* TYPE_H */

typedef enum {
    STATUS_OK = 0,
    STATUS_FAIL = -1,
    STATUS_INVALID_ARG = -2,
    STATUS_NO_MEM = -3,
    STATUS_TIMEOUT = -4,
    STATUS_NOT_SUPPORTED = -5,
    STATUS_INVALID_STATE = -6,
} status_t;

#endif /* COMMON_TYPE_H */
