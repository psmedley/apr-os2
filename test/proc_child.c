#include "apr.h"
#include <stdio.h>
#if APR_HAVE_UNISTD_H
#include <unistd.h>
#endif
#if APR_HAVE_IO_H
#include <io.h>
#endif
#include <stdlib.h>
#ifdef __INNOTEK_LIBC__
#include <fcntl.h>
#endif

int main(void)
{
    char buf[256];
    int bytes;
#if defined(__WATCOMC__)||defined(__INNOTEK_LIBC__)
    setmode(STDIN_FILENO, O_BINARY);
#endif
    
    bytes = (int)read(STDIN_FILENO, buf, 256);
    if (bytes > 0)
        write(STDOUT_FILENO, buf, (unsigned int)bytes);

    return 0; /* just to keep the compiler happy */
}
