/**
 * \file    syscalls.c
 * \brief   裸机 newlib 系统调用存根
 *
 * 此文件提供 CMSIS / newlib 在裸机场景下所需的系统调用。
 * 默认实现空操作，可配合调试器或 UART 输出重写 _write / _read。
 */

#include <errno.h>
#include <sys/stat.h>
#include <stdint.h>

#undef errno
extern int errno;

/* 堆空间起始 (由链接脚本定义) */
extern uint32_t _ebss;

/* ---------------------------------------------------------------------------
 * _sbrk – 堆空间分配 (malloc / free 依赖)
 * -------------------------------------------------------------------------*/
void *_sbrk(int incr)
{
    static uint32_t *heap_limit = (uint32_t *)&_ebss;
    uint32_t *prev = heap_limit;
    heap_limit += incr;
    return (void *)prev;
}

/* ---------------------------------------------------------------------------
 * _write – 输出字符流 (空操作；重定向到 UART 或 Semihosting)
 * -------------------------------------------------------------------------*/
int _write(int file, char *ptr, int len)
{
    (void)file;
    (void)ptr;
    return len;
}

/* ---------------------------------------------------------------------------
 * _read – 输入字符流 (空操作)
 * -------------------------------------------------------------------------*/
int _read(int file, char *ptr, int len)
{
    (void)file;
    (void)ptr;
    return 0;
}

/* ---------- 其余 newlib 必要存根 -----------------------------------------*/
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
