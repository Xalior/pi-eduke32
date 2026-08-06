//
// eduke32-compat.h — the declarations EDuke32's sources expect from a POSIX
// system and this board's C library keeps hidden.
//
// Forced in front of every EDuke32 translation unit by the host Makefile
// (-include). It declares only; the definitions live in
// host/posix_compat.cpp, and each says there what it actually does.
//
// WHY A FORCED INCLUDE RATHER THAN A PATCH. Nothing in the upstream tree is
// edited, ever. Three of the functions below exist in Circle's newlib
// headers but sit inside `#if defined(_POSIX_TIMERS)` and
// `#if defined(_POSIX_THREADS)` blocks that Circle does not switch on, so
// the header is present, the declaration is not, and the compile stops. Two
// more are declared only under `#if defined __linux`, which this is not.
// Re-declaring them here is byte-identical to what those headers would have
// said.
//
#ifndef _eduke32_compat_h
#define _eduke32_compat_h

#include <sys/types.h>
#include <time.h>
#include <pthread.h>
#include <pwd.h>
#include <libgen.h>
#include <signal.h>

#ifdef __cplusplus
extern "C" {
#endif

// High-resolution sleep. Declared by newlib's <time.h> under _POSIX_TIMERS,
// which Circle does not define. The Build engine's idle loop calls it once
// per frame it has nothing to do.
int nanosleep(const struct timespec *rqtp, struct timespec *rmtp);

// The monotonic clock, likewise. This one Circle's newlib DOES implement —
// only the declaration is behind the switch.
int clock_gettime(clockid_t clock_id, struct timespec *tp);

// The pthread calls Loguru — EDuke32's logging library — uses to name and
// identify the calling thread. Circle's newlib declares them under
// _POSIX_THREADS and implements none of them.
pthread_t pthread_self(void);
int pthread_once(pthread_once_t *once_control, void (*init_routine)(void));
int pthread_key_create(pthread_key_t *key, void (*destructor)(void *));
int pthread_key_delete(pthread_key_t key);
void *pthread_getspecific(pthread_key_t key);
int pthread_setspecific(pthread_key_t key, const void *value);
int pthread_setname_np(pthread_t thread, const char *name);
int pthread_getname_np(pthread_t thread, char *name, size_t len);

#ifdef __cplusplus
}
#endif

// ---------------------------------------------------------------------------
// sigaction, in the shape POSIX describes it
// ---------------------------------------------------------------------------
//
// Circle's newlib has a `struct sigaction` with three fields: a
// one-argument handler, a mask and flags. POSIX also describes a
// three-argument handler, `sa_sigaction`, selected with the SA_SIGINFO
// flag, and EDuke32's logging library installs its crash handlers that way.
// Neither the field nor the flag exists here — newlib puts both inside a
// branch it takes only when building for RTEMS — and a member cannot be
// added to somebody else's structure from outside it.
//
// So this port declares its own, and renames every mention: after the macro
// below, `struct sigaction` in EDuke32's sources means the structure
// declared here and `sigaction(...)` means the function declared here. The
// C library's own remains untouched and is what the C library keeps using.
//
// The two handler fields are separate members rather than the union POSIX
// permits, because there is no reason to overlap them and separate ones
// cannot be read as each other by mistake.

#define SA_SIGINFO	0x2
#define SA_NODEFER	0x40000000
#define SA_RESETHAND	0x80000000

struct rapi_sigaction
{
	void	(*sa_handler)(int);
	sigset_t  sa_mask;
	int	  sa_flags;
	void	(*sa_sigaction)(int, siginfo_t *, void *);
};

#ifdef __cplusplus
extern "C" {
#endif
int rapi_sigaction(int signum, const struct rapi_sigaction *act,
                   struct rapi_sigaction *oldact);
#ifdef __cplusplus
}
#endif

// Renames the structure tag and the function together, which is what makes
// the two halves agree. It comes last in this header so that everything
// above still refers to the C library's own.
#define sigaction rapi_sigaction

// PTHREAD_ONCE_INIT is a macro, so it cannot be declared — only defined.
// Newlib defines it as _PTHREAD_ONCE_INIT inside the same switched-off
// block; that spelling is still available from <sys/_pthreadtypes.h>, so
// this is the header's own value, reached the only way that is left.
#ifndef PTHREAD_ONCE_INIT
#define PTHREAD_ONCE_INIT _PTHREAD_ONCE_INIT
#endif

#endif
