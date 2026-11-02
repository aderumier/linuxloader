#ifndef ALIGNED_CALL_H
#define ALIGNED_CALL_H

#include <stdint.h>

// Stubs calling a function on a 16-byte-aligned stack, for the games built
// before the i386 ABI required it (Tux Racer, Dead Heat: their calls fault
// the libraries' SSE moves, see alignedCall.c).
void *alignedCallStub(uintptr_t target);
// 1 if the named function works on the caller's frame (setjmp and company)
// and so must be called directly, not through a stub.
int alignedCallDirect(const char *name);

#endif // ALIGNED_CALL_H
