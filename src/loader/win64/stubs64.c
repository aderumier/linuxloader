// Not yet written bridges.
#include "ll64.h"
#include <windows.h>


// The shared code's log (iniParser): to the loader's.
int logGeneric(int level, const char *file, int line, const char *fmt, ...)
{
    (void)level;
    (void)file;
    (void)line;
    (void)fmt;
    return 0;
}
