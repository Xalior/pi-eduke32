//
// sys/mman.h — memory-mapped files, for a board that has no virtual memory
// manager to map with.
//
// The Build engine's texture cache is written against mio, a header-only
// memory-mapping wrapper, and that header is included through texcache.h
// wherever the cache is mentioned — including in the software-renderer build,
// where the cache itself is never filled. The declarations therefore have to
// exist.
//
// host/posix_compat.cpp implements mmap by reading the file into ordinary
// memory, which is the same trade Chocolate Doom's portable WAD reader
// makes, and munmap by freeing it. A mapping is therefore private and a
// write to it never reaches the file; msync says so by failing.
//
#ifndef _sys_mman_h
#define _sys_mman_h

#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PROT_NONE	0x0
#define PROT_READ	0x1
#define PROT_WRITE	0x2
#define PROT_EXEC	0x4

#define MAP_SHARED	0x01
#define MAP_PRIVATE	0x02
#define MAP_FIXED	0x10
#define MAP_ANONYMOUS	0x20
#define MAP_ANON	MAP_ANONYMOUS

#define MAP_FAILED	((void *) -1)

#define MS_ASYNC	1
#define MS_INVALIDATE	2
#define MS_SYNC		4

void *mmap(void *addr, size_t length, int prot, int flags, int fd, off_t offset);
int munmap(void *addr, size_t length);
int msync(void *addr, size_t length, int flags);
int mprotect(void *addr, size_t length, int prot);

#ifdef __cplusplus
}
#endif

#endif
