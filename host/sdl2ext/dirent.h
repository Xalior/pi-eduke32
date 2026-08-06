//
// dirent.h — directory entries, with the file type this board's C library
// does not record.
//
// CIRCLE'S `struct dirent` HAS NO `d_type`. It carries an inode number and a
// name and nothing else, because FatFs has no inode concept and Circle's
// newlib glue mirrors what FatFs returns. EDuke32's file-finding code reads
// `d_type` to tell a directory from a file, so this port declares the
// structure it needs and fills the field itself.
//
// The field is filled in circle_syscalls.cpp, where every readdir already
// passes through a wrapper: the shim's I/O service reports whether an entry
// is a directory, so an entry read from the application core — which is
// where the game runs — carries a real answer. An entry read on core 0 comes
// straight from the C library, which does not know, and is reported as
// DT_UNKNOWN rather than guessed at.
//
// This header is FIRST on the include path, so it is what every translation
// unit in this build sees, the syscall wrapper included. Both sides
// therefore agree on the layout, and no structure written by one is ever
// read by the other through a different declaration.
//
#ifndef _rapi_dirent_h
#define _rapi_dirent_h

#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

// The handle is Circle's, opaque and passed straight through.
typedef struct _CIRCLE_DIR DIR;

// The BSD/Linux d_type values, at their usual numbers, because that is the
// set of names the code reading this field expects.
#define DT_UNKNOWN	0
#define DT_FIFO		1
#define DT_CHR		2
#define DT_DIR		4
#define DT_BLK		6
#define DT_REG		8
#define DT_LNK		10
#define DT_SOCK		12
#define DT_WHT		14

// 256 bytes of name, which is what the shim's I/O service carries and more
// than a FAT long name can be.
#define RAPI_DIRENT_NAME_MAX	256

// d_type is LAST on purpose. d_ino and d_name then sit at exactly the
// offsets Circle's own structure puts them at, so the syscall wrapper can
// read a C library entry through this declaration before copying it into an
// entry of its own. Putting the new field first would move d_name and the
// wrapper would read the wrong bytes, silently.
struct dirent
{
	ino_t		d_ino;
	char		d_name[RAPI_DIRENT_NAME_MAX];
	unsigned char	d_type;
};

DIR *opendir(const char *path);
struct dirent *readdir(DIR *dir);
int closedir(DIR *dir);
void rewinddir(DIR *dir);

#ifdef __cplusplus
}
#endif

#endif
