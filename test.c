#include <stdarg.h>
#include <stdio.h>
#include <unistd.h>

unsigned long cnt = 0x1122334455667788;

void procmsg(const char* format, ...)
{
    va_list ap;
    fprintf(stdout, "[%d] ", getpid());
    va_start(ap, format);
    vfprintf(stdout, format, ap);
    va_end(ap);
}

void advance()
{
    procmsg("after advance, cnt = %#lx.\n", ++cnt);
}


int main()
{
    procmsg("initial cnt = %#lx.\n", cnt);
    for (int i = 0; i < 4; ++i) {
        advance();
    }
    procmsg("main will return.\n");
    return 0;
}