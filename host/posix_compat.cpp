//
// posix_compat.cpp — the POSIX functions EDuke32 calls and this board does
// not have.
//
// Every declaration is in sdl2ext/, and every definition is here. Nothing in
// this file pretends: where the board genuinely cannot do the thing, the
// function fails and sets errno, so a caller that checks gets an answer it
// can act on and a caller that does not is no worse off than it would be on
// a system where the call failed for a mundane reason.
//
// They fall into three groups.
//
//   THE C LIBRARY HAS IT, THE HEADER HIDES IT. clock_gettime is implemented
//   by Circle's newlib; only its declaration sits behind a feature macro
//   Circle does not set. Nothing is needed here for it — the declaration in
//   eduke32-compat.h is enough, and that is why it is absent below.
//
//   THE BOARD CAN DO IT DIFFERENTLY. nanosleep becomes a scheduler delay.
//   mmap becomes a read into ordinary memory. Both do the job the caller
//   wanted, by other means, and the difference is written down where it
//   matters.
//
//   THE BOARD CANNOT DO IT AT ALL. There are no threads to identify, no
//   stack to walk, no device table to control, no password file to read.
//   Each of those answers honestly and says so here.
//
#include <SDL2/SDL.h>
#include <SDL2/SDL_circle.h>

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fcntl.h>
#include <pthread.h>
#include <pwd.h>
#include <signal.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include <execinfo.h>
#include <eduke32-compat.h>

static const char From[] = "posix";

extern "C" {

// ---------------------------------------------------------------------------
// Sleeping
// ---------------------------------------------------------------------------

// The Build engine's idle path asks for a millisecond or two when it has
// nothing to draw. SDL_Delay is the shim's own wait, and on the application
// core it yields to the servo on core 0 — which is what keeps the sound
// device fed and USB pumped while the game is asleep. A raw spin would not.
//
// Sub-millisecond requests round up to one whole millisecond rather than
// down to nothing, because a caller asking to sleep wants to give the
// machine away, and a zero-length sleep in a `do {} while` loop would be a
// spin.
int nanosleep(const struct timespec *rqtp, struct timespec *rmtp)
{
    if (rqtp == nullptr || rqtp->tv_nsec < 0 || rqtp->tv_nsec >= 1000000000L)
    {
        errno = EINVAL;
        return -1;
    }

    unsigned long ms = (unsigned long)rqtp->tv_sec * 1000UL
                       + (unsigned long)(rqtp->tv_nsec / 1000000L);
    if (ms == 0 && rqtp->tv_nsec != 0)
        ms = 1;

    SDL_Delay((Uint32)ms);

    // Nothing here can interrupt a sleep, so the whole request was served.
    if (rmtp != nullptr)
    {
        rmtp->tv_sec  = 0;
        rmtp->tv_nsec = 0;
    }
    return 0;
}

// ---------------------------------------------------------------------------
// Threads that do not exist
// ---------------------------------------------------------------------------
//
// EDuke32's logging library, Loguru, identifies and names the calling thread
// on every message. There is one application core running one flow of
// control, so there is exactly one thread to identify, and these give a
// consistent account of it: one identity, one name, one set of keys.
//
// They are not a pthread implementation and must not be read as the start of
// one. If a payload ever genuinely needs threads, the answer is the shim's
// core split, not this file.

static const pthread_t s_TheOnlyThread = (pthread_t)1;

// A fixed name so a log line reads the same way it would on a desktop. It is
// settable because Loguru sets it, and readable because Loguru reads it back.
static char s_ThreadName[32] = "main";

pthread_t pthread_self(void)
{
    return s_TheOnlyThread;
}

int pthread_setname_np(pthread_t, const char *name)
{
    if (name == nullptr)
        return EINVAL;
    strncpy(s_ThreadName, name, sizeof(s_ThreadName) - 1);
    s_ThreadName[sizeof(s_ThreadName) - 1] = '\0';
    return 0;
}

int pthread_getname_np(pthread_t, char *name, size_t len)
{
    if (name == nullptr || len == 0)
        return EINVAL;
    strncpy(name, s_ThreadName, len - 1);
    name[len - 1] = '\0';
    return 0;
}

// pthread_once_t is newlib's { is_initialized, init_executed }. With one
// thread there is no race to lose, so this is a plain flag.
int pthread_once(pthread_once_t *once_control, void (*init_routine)(void))
{
    if (once_control == nullptr || init_routine == nullptr)
        return EINVAL;
    if (once_control->init_executed == 0)
    {
        once_control->init_executed = 1;
        init_routine();
    }
    return 0;
}

// Thread-specific storage for the one thread there is: a small table of
// values, and a destructor recorded but never run, because the only moment
// it would run is the thread ending, and this thread ends when the board
// stops.
namespace
{
const int MAX_KEYS = 16;

struct TLSKey
{
    bool  used;
    void *value;
};

TLSKey s_Keys[MAX_KEYS];
}

int pthread_key_create(pthread_key_t *key, void (*)(void *))
{
    if (key == nullptr)
        return EINVAL;
    for (int i = 0; i < MAX_KEYS; i++)
    {
        if (!s_Keys[i].used)
        {
            s_Keys[i].used  = true;
            s_Keys[i].value = nullptr;
            *key = (pthread_key_t)i;
            return 0;
        }
    }
    return EAGAIN;
}

int pthread_key_delete(pthread_key_t key)
{
    if ((unsigned)key >= MAX_KEYS || !s_Keys[key].used)
        return EINVAL;
    s_Keys[key].used  = false;
    s_Keys[key].value = nullptr;
    return 0;
}

void *pthread_getspecific(pthread_key_t key)
{
    if ((unsigned)key >= MAX_KEYS || !s_Keys[key].used)
        return nullptr;
    return s_Keys[key].value;
}

int pthread_setspecific(pthread_key_t key, const void *value)
{
    if ((unsigned)key >= MAX_KEYS || !s_Keys[key].used)
        return EINVAL;
    s_Keys[key].value = const_cast<void *>(value);
    return 0;
}

// ---------------------------------------------------------------------------
// Stack traces that cannot be taken
// ---------------------------------------------------------------------------
//
// The board has no unwinder to walk from a signal handler and no symbol
// table in the image to name a frame from. Reporting zero frames is the
// truthful answer: a handler that runs prints its own message and stops,
// rather than printing a stack that was made up.
//
// A real trace is available at the bench by other means — the image's .map
// and .lst files are produced beside every kernel — so nothing is lost here
// that was ever going to be found here.

int backtrace(void **, int)
{
    return 0;
}

char **backtrace_symbols(void *const *, int)
{
    return nullptr;
}

void backtrace_symbols_fd(void *const *, int, int)
{
}

// ---------------------------------------------------------------------------
// Device control
// ---------------------------------------------------------------------------
//
// Reached only through the bundled ENet's header, and no ENet code is
// compiled into this image. It exists so that header parses.

int ioctl(int, unsigned long, ...)
{
    errno = ENOTTY;
    return -1;
}

// ---------------------------------------------------------------------------
// The password file
// ---------------------------------------------------------------------------
//
// The Build engine asks for the calling user's home directory when the
// environment does not name one, and asks it the way a Unix program does.
// There is no user and no home directory on this board: the game's files are
// all inside RAPI_GAME_DIR, which the kernel makes the working directory
// before the game starts. Answering "no such user" sends the caller down its
// own null-result path, which is where it should be.

struct passwd *getpwuid(uid_t)
{
    errno = ENOENT;
    return nullptr;
}

struct passwd *getpwnam(const char *)
{
    errno = ENOENT;
    return nullptr;
}

// ---------------------------------------------------------------------------
// Signal handlers
// ---------------------------------------------------------------------------
//
// EDuke32's logging library installs crash handlers with sigaction and the
// SA_SIGINFO flag, which asks for the three-argument handler form. Circle's
// newlib has neither that field nor that flag (see sdl2ext/eduke32-compat.h
// for why, and for the renaming that brings this function into play), and
// what it does have underneath is the one-argument signal().
//
// So both forms are accepted and both are installed through signal(). A
// three-argument handler is reached by way of a trampoline that calls it
// with no signal information and no processor context, because there is
// none to give: nothing on this board delivers a signal asynchronously.
// The only ones that arrive at all are the ones the program raises itself —
// abort(), or an assertion failing — and for those the signal number is the
// whole story anyway.

namespace
{
// One three-argument handler per signal, and the trampoline that reaches
// them. Sized past the highest signal number Circle's newlib defines.
const int MAX_SIGNALS = 32;

void (*s_SigInfoHandlers[MAX_SIGNALS])(int, siginfo_t *, void *);

void SigInfoTrampoline(int signum)
{
    if (signum >= 0 && signum < MAX_SIGNALS
        && s_SigInfoHandlers[signum] != nullptr)
        s_SigInfoHandlers[signum](signum, nullptr, nullptr);
}
}

int rapi_sigaction(int signum, const struct rapi_sigaction *act,
                   struct rapi_sigaction *oldact)
{
    if (signum < 0 || signum >= MAX_SIGNALS)
    {
        errno = EINVAL;
        return -1;
    }

    void (*previous)(int) = nullptr;

    if (act != nullptr)
    {
        if ((act->sa_flags & SA_SIGINFO) != 0)
        {
            s_SigInfoHandlers[signum] = act->sa_sigaction;
            previous = signal(signum, SigInfoTrampoline);
        }
        else
        {
            s_SigInfoHandlers[signum] = nullptr;
            previous = signal(signum, act->sa_handler);
        }

        if (previous == SIG_ERR)
            return -1;
    }

    if (oldact != nullptr)
    {
        memset(oldact, 0, sizeof(*oldact));
        oldact->sa_handler = (previous == SIG_ERR) ? nullptr : previous;
    }
    return 0;
}

// ---------------------------------------------------------------------------
// Memory-mapped files
// ---------------------------------------------------------------------------
//
// There is no MMU mapping to make here, so a mapping is a READ INTO ORDINARY
// MEMORY: mmap allocates, reads the requested extent, and hands it back;
// munmap frees it. The trade is the same one Chocolate Doom's portable WAD
// reader makes, and it has one consequence worth stating plainly — the
// mapping is a private copy, so writing to it never reaches the file, and
// msync says so by failing rather than by silently discarding the write.
//
// The only caller in this build is the Build engine's texture cache, which
// this software-renderer image never fills. It is implemented properly
// anyway, because a stub that returned MAP_FAILED would make an ordinary
// missing-cache path look like an error.

namespace
{
struct Mapping
{
    void   *base;
    size_t  length;
    Mapping *next;
};

Mapping *s_Mappings = nullptr;
}

void *mmap(void *addr, size_t length, int prot, int flags, int fd, off_t offset)
{
    (void)addr;
    (void)prot;

    if (length == 0)
    {
        errno = EINVAL;
        return MAP_FAILED;
    }

    void *base = calloc(1, length);
    Mapping *rec = (Mapping *)calloc(1, sizeof(Mapping));
    if (base == nullptr || rec == nullptr)
    {
        free(base);
        free(rec);
        errno = ENOMEM;
        return MAP_FAILED;
    }

    // An anonymous mapping is zeroed memory and nothing more, which calloc
    // has already produced.
    if ((flags & MAP_ANONYMOUS) == 0)
    {
        if (lseek(fd, offset, SEEK_SET) == (off_t)-1)
        {
            free(base);
            free(rec);
            return MAP_FAILED;
        }

        // Short reads are not an error: a mapping may legitimately extend
        // past the end of the file, and those bytes read as zero, which is
        // what calloc already put there.
        size_t done = 0;
        while (done < length)
        {
            long n = read(fd, (char *)base + done, length - done);
            if (n < 0)
            {
                free(base);
                free(rec);
                return MAP_FAILED;
            }
            if (n == 0)
                break;
            done += (size_t)n;
        }
    }

    rec->base   = base;
    rec->length = length;
    rec->next   = s_Mappings;
    s_Mappings  = rec;
    return base;
}

int munmap(void *addr, size_t)
{
    Mapping **link = &s_Mappings;
    for (Mapping *m = s_Mappings; m != nullptr; link = &m->next, m = m->next)
    {
        if (m->base != addr)
            continue;
        *link = m->next;
        free(m->base);
        free(m);
        return 0;
    }
    errno = EINVAL;
    return -1;
}

// A mapping made here is a private copy of the file's bytes, so there is
// nothing to flush back and no way to make one. Saying so is better than
// returning success to a caller that then believes its writes are on the
// card.
int msync(void *, size_t, int)
{
    SDL2Circle_Log(From, SDL2CIRCLE_LOG_WARNING,
                   "msync: mappings here are private copies — nothing written back");
    errno = ENOSYS;
    return -1;
}

// Every page on this board is already readable, writable and executable:
// Circle maps the whole of memory once at startup and never changes it.
// There is no protection to change, so asking for one is not an error.
int mprotect(void *, size_t, int)
{
    return 0;
}

} // extern "C"
