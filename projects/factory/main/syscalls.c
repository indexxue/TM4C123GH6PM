/**
 * \file    syscalls.c
 * \brief   裸机 newlib 系统调用存根
 */

#include <errno.h>
#include <stdint.h>
#include <sys/stat.h>

#undef errno
extern int errno;

extern uint32_t _ebss;
extern uint32_t _estack;

#define MAIN_STACK_RESERVE 4096U

static uint8_t *s_heap_end;

void *_sbrk(int incr)
{
    uint8_t *prev;
    uint8_t *limit;

    if (s_heap_end == NULL) {
        s_heap_end = (uint8_t *)&_ebss;
    }

    if (incr < 0) {
        s_heap_end += incr;
        return (void *)(s_heap_end - incr);
    }

    prev = s_heap_end;
    {
        uintptr_t stack_top = (uintptr_t)&_estack;

        limit = (uint8_t *)(stack_top - (uintptr_t)MAIN_STACK_RESERVE);
    }
    if ((s_heap_end + (uint32_t)incr) > limit) {
        errno = ENOMEM;
        return (void *)-1;
    }

    s_heap_end += incr;
    return prev;
}

int _write(int file, char *ptr, int len)
{
    (void)file;
    (void)ptr;
    return len;
}

int _read(int file, char *ptr, int len)
{
    (void)file;
    (void)ptr;
    (void)len;
    return 0;
}

int _close(int file)                { (void)file; return -1; }

int _fstat(int file, struct stat *st)
{
    (void)file;
    st->st_mode = S_IFCHR;
    return 0;
}

int _isatty(int file)               { (void)file; return 1; }
int _lseek(int file, int ptr, int dir)
{
    (void)file; (void)ptr; (void)dir;
    return 0;
}

int _getpid(void)                   { return 1; }

int _kill(int pid, int sig)
{
    (void)pid; (void)sig;
    errno = EINVAL;
    return -1;
}
