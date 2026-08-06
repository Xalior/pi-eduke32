//
// circle_syscalls.cpp — the game's file access, routed to the core that
// owns the hardware.
//
// THE PROBLEM. EDuke32 reads its GRP, its configuration and its save games
// with plain C: fopen/fread, opendir/readdir, remove(). Those reach
// newlib, and newlib's Circle glue drives FatFs and the SD card directly.
// On this kernel the game runs on the application core, and touching a
// device from any core but core 0 is illegal — the whole Circle world,
// interrupts included, belongs to core 0. The game has no file layer of
// its own to redirect, so there is nothing in it to change.
//
// THE FIX. circle-libsdl2 publishes an I/O service (SDL2Circle_IO*, in
// SDL2/SDL_circle.h) that is valid from ANY core: each call is marshalled
// to core 0, performed there, and the result handed back. This file puts
// that service underneath the C library, so unmodified code gets it for
// free.
//
// HOW THE INTERPOSITION WORKS. The linker's --wrap (see the Makefile) is
// used rather than redefining the symbols. Redefinition would be a trap:
// newlib's glue defines _open, _read, _write, _close, _lseek, _fstat,
// opendir, readdir, closedir, dup and more in ONE object file, so replacing
// some of them either collides or silently drags the originals back in
// beside the replacements. --wrap leaves the glue untouched and renames the
// references: everything calling _open reaches __wrap__open here, and
// __real__open still reaches the genuine implementation. Nothing in the
// vendored tree is edited.
//
// That surviving __real_* is not a convenience, it is the mechanism. The
// I/O service performs its work by making these very calls on core 0, so
// without a way back to the original this file would call itself forever.
// Every wrapper therefore reads: on core 0, do the real thing; anywhere
// else, ride the service.
//
// WHAT THE SERVICE DOES NOT COVER. It is a file and directory service, so
// the console and a few descriptor-table calls have no route through it.
// The game's own output — every std::cout it makes, on descriptors 1 and 2
// — goes to the library's log ring instead: the application core formats
// the line and returns, and the hardware core prints it. That output used
// to be marshalled a write at a time, which put the game's core to sleep
// on the mailbox for every fragment of every line it printed. The
// remaining descriptor-table calls, which nothing here makes, are still
// marshalled with SDL2Circle_CallOn0 and run unchanged on core 0.
//
// POSITIONS. The service names an offset on every read and write, while the
// C library expects a file to remember where it is. So the position lives
// here, one slot per open descriptor, and is advanced exactly as a real
// file position would be.
//
// DIRECTORY ENTRIES CARRY A TYPE HERE. Circle's `struct dirent` has a name
// and an inode number and nothing else; the Build engine reads `d_type` to
// tell a directory from a file. sdl2ext/dirent.h declares the structure
// with that field, and readdir below is what fills it. The service knows
// whether an entry is a directory, so an entry read from the application
// core — where the game runs — carries a real answer; an entry read on
// core 0 comes straight from the C library, which does not know, and is
// reported as DT_UNKNOWN rather than guessed at.
//
#include <SDL2/SDL_circle.h>

#include <circle/multicore.h>

#include <cerrno>
#include <cstring>
#include <utility>
#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

extern "C" {

// The genuine newlib glue, still reachable under these names because of
// --wrap. Called on core 0 only.
int __real__open(const char *path, int flags, ...);
int __real__close(int fd);
long __real__read(int fd, void *buf, size_t len);
long __real__write(int fd, const void *buf, size_t len);
off_t __real__lseek(int fd, off_t off, int whence);
int __real__fstat(int fd, struct stat *st);
int __real__stat(const char *path, struct stat *st);
int __real_lstat(const char *path, struct stat *st);
int __real__unlink(const char *path);
int __real__rename(const char *from, const char *to);
int __real__fcntl(int fd, int cmd, ...);
int __real_mkdir(const char *path, mode_t mode);
int __real_chdir(const char *path);
char *__real_getcwd(char *buf, size_t size);
int __real_dup(int fd);
int __real_dup2(int a, int b);
DIR *__real_opendir(const char *path);
struct dirent *__real_readdir(DIR *dir);
int __real_closedir(DIR *dir);
void __real_rewinddir(DIR *dir);

} // extern "C"

namespace
{

// Are we the core that owns the hardware? Before the split is armed there
// is only one core and the answer is always yes, which is what makes this
// file inert on a single-core boot and under rapi-split=0.
inline bool OnHardwareCore(void)
{
    if (!SDL2Circle_SplitActive())
        return true;
#ifdef ARM_ALLOW_MULTI_CORE
    return CMultiCoreSupport::ThisCore() == 0;
#else
    return true;
#endif
}

// Run a call on core 0 and bring back both its result and its errno. Used
// for the handful of things the I/O service does not cover; the marshalled
// body runs on core 0, so the wrappers it re-enters take their direct path.
template <typename F, typename R = decltype(std::declval<F &>()())>
R OnHardwareCoreDo(F fn)
{
    struct Box
    {
        F *fn;
        R result;
        int err;
    } box{&fn, R(), 0};

    SDL2Circle_CallOn0([](void *p)
    {
        Box *b = static_cast<Box *>(p);
        errno = 0;
        b->result = (*b->fn)();
        b->err = errno;
    }, &box);

    errno = box.err;
    return box.result;
}

// Descriptors 0, 1 and 2 are the serial console, not files.
inline bool IsConsole(int fd) { return fd >= 0 && fd <= 2; }

// ---------------------------------------------------------------------------
// Open-file positions.
//
// Sized past Circle's own descriptor table (20 entries), because the
// descriptors here ARE that table's — the service opens the file on core 0
// and hands the real descriptor back. A descriptor outside this range would
// mean the glue grew, so it is refused rather than silently unpositioned.
// ---------------------------------------------------------------------------

const int MAX_TRACKED_FD = 64;

struct OpenFile
{
    bool used;
    long long pos;      // where the next read or write starts
    long long size;     // last known length, for seeks from the end
};

OpenFile s_files[MAX_TRACKED_FD];

OpenFile *Track(int fd)
{
    if (fd < 0 || fd >= MAX_TRACKED_FD || !s_files[fd].used)
        return nullptr;
    return &s_files[fd];
}

// The service reports failures as a negated errno and never touches the
// caller's. Put it back where C expects it.
int Fail(int negErrno)
{
    errno = negErrno < 0 ? -negErrno : EIO;
    return -1;
}

} // namespace

extern "C" {

// ---- files -----------------------------------------------------------------

int __wrap__open(const char *path, int flags, ...)
{
    // The mode argument only matters when creating, and the service always
    // creates with the same permissions, so it is not forwarded.
    if (OnHardwareCore())
        return __real__open(path, flags, 0666);

    unsigned io = 0;
    switch (flags & O_ACCMODE)
    {
    case O_WRONLY: io = SDL2CIRCLE_IO_WRITE; break;
    case O_RDWR:   io = SDL2CIRCLE_IO_READ | SDL2CIRCLE_IO_WRITE; break;
    default:       io = SDL2CIRCLE_IO_READ; break;
    }
    if (flags & O_CREAT)
        io |= SDL2CIRCLE_IO_CREATE;

    uint64_t size = 0;
    int fd = SDL2Circle_IOOpen(path, io, &size);
    if (fd < 0)
        return Fail(fd);
    if (fd >= MAX_TRACKED_FD)
    {
        SDL2Circle_IOClose(fd);
        errno = EMFILE;
        return -1;
    }

    s_files[fd].used = true;
    s_files[fd].size = (long long)size;
    // O_APPEND starts at the end; everything else at the beginning. A
    // create truncated the file, so its length is zero whatever was there.
    s_files[fd].pos = (flags & O_APPEND) ? (long long)size : 0;
    if (io & SDL2CIRCLE_IO_CREATE)
        s_files[fd].size = 0;
    return fd;
}

int __wrap__close(int fd)
{
    if (OnHardwareCore())
        return __real__close(fd);
    if (IsConsole(fd))
        return OnHardwareCoreDo([&] { return __real__close(fd); });

    OpenFile *f = Track(fd);
    if (f)
        f->used = false;
    int r = SDL2Circle_IOClose(fd);
    return r < 0 ? Fail(r) : 0;
}

long __wrap__read(int fd, void *buf, size_t len)
{
    if (OnHardwareCore())
        return __real__read(fd, buf, len);
    if (IsConsole(fd))
        return OnHardwareCoreDo([&] { return __real__read(fd, buf, len); });

    OpenFile *f = Track(fd);
    if (!f)
    {
        errno = EBADF;
        return -1;
    }
    long n = SDL2Circle_IORead(fd, buf, (uint64_t)f->pos, (uint32_t)len);
    if (n < 0)
        return Fail((int)n);
    f->pos += n;
    return n;
}

long __wrap__write(int fd, const void *buf, size_t len)
{
    if (OnHardwareCore())
        return __real__write(fd, buf, len);
    if (IsConsole(fd))
    {
        // The game's output, from the game's own core: format it into the
        // log ring and return. No mailbox, no waiting, no device touched.
        SDL2Circle_LogBytes(fd == 2 ? "stderr" : "stdout",
                            (const char *)buf, (unsigned)len);
        return (long)len;
    }

    OpenFile *f = Track(fd);
    if (!f)
    {
        errno = EBADF;
        return -1;
    }
    long n = SDL2Circle_IOWrite(fd, buf, (uint64_t)f->pos, (uint32_t)len);
    if (n < 0)
        return Fail((int)n);
    f->pos += n;
    if (f->pos > f->size)
        f->size = f->pos;
    return n;
}

off_t __wrap__lseek(int fd, off_t off, int whence)
{
    if (OnHardwareCore())
        return __real__lseek(fd, off, whence);
    if (IsConsole(fd))
        return OnHardwareCoreDo([&] { return __real__lseek(fd, off, whence); });

    OpenFile *f = Track(fd);
    if (!f)
    {
        errno = EBADF;
        return -1;
    }
    long long target;
    switch (whence)
    {
    case SEEK_SET: target = off; break;
    case SEEK_CUR: target = f->pos + off; break;
    // The length recorded when the file was opened, advanced by anything
    // written since. Nothing else on this board can be changing the file
    // underneath, so it is exact.
    case SEEK_END: target = f->size + off; break;
    default:
        errno = EINVAL;
        return -1;
    }
    if (target < 0)
    {
        errno = EINVAL;
        return -1;
    }
    f->pos = target;
    return (off_t)target;
}

// Called by the C++ library on every file it opens, to size its buffer.
int __wrap__fstat(int fd, struct stat *st)
{
    if (OnHardwareCore())
        return __real__fstat(fd, st);
    if (IsConsole(fd))
        return OnHardwareCoreDo([&] { return __real__fstat(fd, st); });

    OpenFile *f = Track(fd);
    if (!f)
    {
        errno = EBADF;
        return -1;
    }
    memset(st, 0, sizeof(*st));
    st->st_mode = S_IFREG | 0666;
    st->st_size = (off_t)f->size;
    st->st_nlink = 1;
    st->st_blksize = 512;
    st->st_blocks = (f->size + 511) / 512;
    return 0;
}

static int stat_through_service(const char *path, struct stat *st)
{
    SDL2Circle_IOStat s;
    int r = SDL2Circle_IOStatPath(path, &s);
    if (r < 0)
        return Fail(r);
    memset(st, 0, sizeof(*st));
    st->st_mode = (s.isdir ? S_IFDIR : S_IFREG) | 0666;
    st->st_size = (off_t)s.size;
    st->st_mtime = (time_t)s.mtime;
    st->st_nlink = 1;
    st->st_blksize = 512;
    st->st_blocks = (s.size + 511) / 512;
    return 0;
}

int __wrap__stat(const char *path, struct stat *st)
{
    if (OnHardwareCore())
        return __real__stat(path, st);
    return stat_through_service(path, st);
}

// No symbolic links exist on this filesystem, so this is stat.
int __wrap_lstat(const char *path, struct stat *st)
{
    if (OnHardwareCore())
        return __real_lstat(path, st);
    return stat_through_service(path, st);
}

int __wrap__unlink(const char *path)
{
    if (OnHardwareCore())
        return __real__unlink(path);
    int r = SDL2Circle_IOUnlink(path);
    return r < 0 ? Fail(r) : 0;
}

int __wrap_mkdir(const char *path, mode_t mode)
{
    if (OnHardwareCore())
        return __real_mkdir(path, mode);
    int r = SDL2Circle_IOMkdir(path);
    return r < 0 ? Fail(r) : 0;
}

// ---- directories -----------------------------------------------------------
//
// The service opens the directory on core 0 and hands back the very handle
// the C library would have given, so it travels as an opaque DIR * here.

DIR *__wrap_opendir(const char *path)
{
    if (OnHardwareCore())
        return __real_opendir(path);
    return (DIR *)SDL2Circle_IOOpenDir(path);
}

// One buffer serves every caller, on the same terms the C library sets: the
// entry is only valid until the next call, and there is a single reader.
//
// Every path copies into it rather than handing back what it was given. On
// core 0 that is because the C library's entry has no d_type field to write
// into — the storage behind the pointer is the library's, and shorter than
// what this port declares. Off core 0 it is because the service's answer
// lives on the caller's stack.
static struct dirent s_entry;

struct dirent *__wrap_readdir(DIR *dir)
{
    if (OnHardwareCore())
    {
        struct dirent *pReal = __real_readdir(dir);
        if (pReal == nullptr)
            return nullptr;

        memset(&s_entry, 0, sizeof(s_entry));
        s_entry.d_ino = pReal->d_ino;
        strncpy(s_entry.d_name, pReal->d_name, sizeof(s_entry.d_name) - 1);
        // The C library does not report a type and this side will not
        // invent one. DT_UNKNOWN is the value a filesystem that cannot
        // answer is supposed to give.
        s_entry.d_type = DT_UNKNOWN;
        return &s_entry;
    }

    SDL2Circle_IODirEntry e;
    int r = SDL2Circle_IOReadDir((intptr_t)dir, &e);
    if (r <= 0)
    {
        if (r < 0)
            errno = -r;
        return nullptr;
    }
    memset(&s_entry, 0, sizeof(s_entry));
    strncpy(s_entry.d_name, e.name, sizeof(s_entry.d_name) - 1);
    s_entry.d_type = e.isdir ? DT_DIR : DT_REG;
    return &s_entry;
}

int __wrap_closedir(DIR *dir)
{
    if (OnHardwareCore())
        return __real_closedir(dir);
    SDL2Circle_IOCloseDir((intptr_t)dir);
    return 0;
}

// ---- the residue -----------------------------------------------------------
//
// Descriptor-table and path calls with no route through the I/O service.
// Nothing in this payload calls them, but they touch the same FatFs state,
// so they are marshalled rather than left to run on the wrong core.

void __wrap_rewinddir(DIR *dir)
{
    if (OnHardwareCore())
    {
        __real_rewinddir(dir);
        return;
    }
    OnHardwareCoreDo([&] { __real_rewinddir(dir); return 0; });
}

int __wrap__rename(const char *from, const char *to)
{
    if (OnHardwareCore())
        return __real__rename(from, to);
    return OnHardwareCoreDo([&] { return __real__rename(from, to); });
}

int __wrap__fcntl(int fd, int cmd, int arg)
{
    if (OnHardwareCore())
        return __real__fcntl(fd, cmd, arg);
    return OnHardwareCoreDo([&] { return __real__fcntl(fd, cmd, arg); });
}

int __wrap_chdir(const char *path)
{
    if (OnHardwareCore())
        return __real_chdir(path);
    return OnHardwareCoreDo([&] { return __real_chdir(path); });
}

char *__wrap_getcwd(char *buf, size_t size)
{
    if (OnHardwareCore())
        return __real_getcwd(buf, size);
    return OnHardwareCoreDo([&] { return __real_getcwd(buf, size); });
}

int __wrap_dup(int fd)
{
    if (OnHardwareCore())
        return __real_dup(fd);
    return OnHardwareCoreDo([&] { return __real_dup(fd); });
}

int __wrap_dup2(int a, int b)
{
    if (OnHardwareCore())
        return __real_dup2(a, b);
    return OnHardwareCoreDo([&] { return __real_dup2(a, b); });
}

} // extern "C"
