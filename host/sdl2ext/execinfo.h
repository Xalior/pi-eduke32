//
// execinfo.h — the glibc backtrace interface, declared for a board that has
// no unwinder to walk and no symbol table to name.
//
// EDuke32's SDL layer installs signal handlers that print a stack trace, and
// it decides at compile time that any GNU compiler on a non-Windows system
// has this header. The declarations therefore have to exist. The
// implementations, in host/posix_compat.cpp, report zero frames — which is
// the truth here — so a handler that runs prints its message and nothing
// else rather than printing a fabricated stack.
//
#ifndef _execinfo_h
#define _execinfo_h

#ifdef __cplusplus
extern "C" {
#endif

int backtrace(void **buffer, int size);
char **backtrace_symbols(void *const *buffer, int size);
void backtrace_symbols_fd(void *const *buffer, int size, int fd);

#ifdef __cplusplus
}
#endif

#endif
