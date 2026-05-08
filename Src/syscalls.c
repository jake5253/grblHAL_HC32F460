/*
  syscalls.c - minimal newlib syscall stubs for HC32F460

  Part of grblHAL
*/

#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "hc32_ddl.h"

extern char __HeapBase;
extern char __HeapLimit;

caddr_t _sbrk (int incr)
{
    static char *heap_end;
    char *prev_heap_end;

    if(heap_end == NULL)
        heap_end = &__HeapBase;

    prev_heap_end = heap_end;

    if((heap_end + incr) > &__HeapLimit) {
        errno = ENOMEM;
        return (caddr_t)-1;
    }

    heap_end += incr;

    return (caddr_t)prev_heap_end;
}

int _write (int file, const void *ptr, size_t len)
{
    (void)file;
    (void)ptr;

    return (int)len;
}

int _read (int file, void *ptr, size_t len)
{
    (void)file;
    (void)ptr;
    (void)len;

    return 0;
}

int _close (int file)
{
    (void)file;
    return -1;
}

off_t _lseek (int file, off_t ptr, int dir)
{
    (void)file;
    (void)ptr;
    (void)dir;

    return 0;
}

int _fstat (int file, struct stat *st)
{
    (void)file;

    st->st_mode = S_IFCHR;

    return 0;
}

int _isatty (int file)
{
    (void)file;
    return 1;
}

int _getpid (void)
{
    return 1;
}

int _kill (int pid, int sig)
{
    (void)pid;
    (void)sig;

    errno = EINVAL;
    return -1;
}

void _exit (int status)
{
    (void)status;

    __disable_irq();

    for(;;) {
    }
}
