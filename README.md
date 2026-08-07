# pi-eduke32

**Duke Nukem 3D on a bare-metal Raspberry Pi.** No Linux, no desktop, no
window manager. The board powers on, the firmware loads one file, and that
file is the game.

This repository contains only the part that makes that possible: a small
kernel that starts the board, a layer that gives the game the operating
system calls it expects, and a build that compiles EDuke32 against them.
EDuke32 itself is included as a submodule and is never modified.

## What you need before anything works

**A `DUKE3D.GRP` file.** This is the game's data — every level, texture,
sound and piece of music. It is not in this repository and never will be,
because it is not ours to give away.

There are two kinds of GRP:

- **The shareware GRP.** Duke Nukem 3D's first episode was released as
  shareware in 1996 and its data file may be freely copied and shared. This
  is the one to use if you do not own the game. It is about 11 MB and
  contains the first episode, *L.A. Meltdown*. `make media` fetches this one
  for you — see [Game data, and `make media`](#game-data-and-make-media)
  below.
- **The full GRP**, from a copy of the game you bought. It contains all the
  episodes. This one is not fetched by anything here; you supply it.

Either one works. Run `make card` and it ends up in the game's directory on
the card (see [The card](#the-card) below).

**Nothing in this project circumvents a licence, a paywall or a shop.** The
full GRP is a commercial product; if you do not have it, use the shareware
GRP or supply your own.

## The renderer: software only

EDuke32 can draw in three ways: the original 8-bit software renderer that
Duke Nukem 3D shipped with in 1996 (which its own documentation calls
**classic**), and two later renderers built on OpenGL — **Polymost** and
**Polymer**.

**This build has only the classic software renderer.** There is no OpenGL on
this board at all: the graphics layer underneath draws into the Raspberry
Pi's framebuffer directly and provides no GL. Building with EDuke32's
`USE_OPENGL` switched off removes both hardware renderers, and the files
that make them up are simply not compiled:

| Left out | What it was |
| --- | --- |
| `polymost.cpp`, `polymer.cpp` | the two OpenGL renderers |
| `glbuild.cpp`, `glsurface.cpp` | the OpenGL setup and its output surface |
| `mdsprite.cpp`, `voxmodel.cpp`, `tilepacker.cpp` | 3D models, voxels and texture atlases, all OpenGL-only features |
| `animvpx.cpp` | the VP8 cutscene player, an OpenGL path needing a separate video library |

This is not a compromise for the sake of the port. The classic renderer is
what Duke Nukem 3D was designed around, it is complete, and it produces the
picture the game was drawn for.

### Other things switched off, and why

| Switched off | Reason |
| --- | --- |
| **Multiplayer** (`NETCODE_DISABLE`) | there is no network stack under this board. The bundled ENet library and the older `mmulti` transport are not compiled. |
| **The startup window** | EDuke32 normally shows a settings dialog before the game starts, drawn with GTK, Windows or macOS widgets. There is no window manager here. Settings come from the configuration file on the card instead. |
| **Ogg Vorbis, FLAC and XMP music** | two of these decode on a background thread, and this board runs the game on a single core with no threads. Music from the GRP is MIDI, which the game's own OPL3 emulation plays without any of them. |
| **mimalloc** | a thread-aware allocator, of no use where nothing contends. Memory comes from the board's own allocator. |
| **PhysicsFS** | an archive-backed virtual filesystem. The GRP is read directly. |

## What this project adds

Everything this project writes lives in `host/`. Upstream is never edited.

### A Circle kernel

`host/kernel.cpp` starts the board — interrupts, timers, the SD card, a
serial console — and then calls the game's entry point. It also divides the
work between the processor's cores:

- **Core 0** owns every piece of hardware. On this system, only this core is
  allowed to touch a device at all.
- **Core 1** runs the game: the Build engine's classic renderer and the game
  loop, and nothing else.
- **Core 2** takes each finished frame and puts it on the screen.
- **Core 3** is parked.

EDuke32's entry point is renamed at compile time — `-Dmain=eduke32_main`,
applied only to `source/build/src/sdlayer.cpp` — because `main()` here
belongs to the kernel. That file compiles as C++, like the rest of the Build
engine, so the renamed function keeps ordinary C++ linkage: the kernel's own
declaration of `eduke32_main` must **not** be `extern "C"`, or it asks the
linker for the unmangled name while the object file offers the mangled one.
This cost the port its first working link.

### The operating system calls the board does not have

The game is written for a Unix-like system. This board's C library is
newlib, which has much of that but not all of it. `host/posix_compat.cpp`
supplies what is missing, and every function in it either does the job by
another means or fails honestly — nothing pretends to work.

| Missing | What this project does |
| --- | --- |
| `nanosleep` | sleeps through the graphics layer's own wait, which hands the processor to the core that serves devices |
| `mmap` / `munmap` | reads the file into ordinary memory and frees it afterwards. There is no memory manager to map with, so a mapping is a private copy and `msync` says so by failing |
| `pthread_self`, `pthread_once`, thread-local keys, thread names | the logging library asks the calling thread to identify itself. There is one, and these give a consistent account of it |
| `backtrace` | reports zero frames, which is the truth: there is no unwinder to walk and no symbol table in the image to name |
| `getpwuid`, `getuid` | reports no such user. There is no user and no home directory; the game's files are all in one place |
| `ioctl` | fails. It is reached only through a header, and no code that would call it is compiled |
| `posix_memalign` | delegates to Circle's own `memalign()`, which already returns every block aligned to the processor's cache line — the same guarantee `malloc()` gives, and the same free()-compatible block |
| `execvp`, `waitpid` | fail. Reached only through dear imgui's default "open this in the shell" callback, which nothing in this game ever asks for |

### A file type on directory entries

Circle's `struct dirent` carries a name and an inode number and nothing else,
because the FAT filesystem underneath has no more to give. EDuke32 reads a
`d_type` field to tell a directory from a file.

`host/sdl2ext/dirent.h` declares the structure with that field and
`host/circle_syscalls.cpp` fills it: the graphics layer's file service
reports whether an entry is a directory, so an entry read from the core the
game runs on carries a real answer. An entry read on the hardware core comes
straight from the C library, which does not know, and is reported as
`DT_UNKNOWN` rather than guessed at.

`opendir`, `readdir`, `closedir` and `rewinddir` are wrapped with `--wrap` so
every call reaches the graphics layer's file service instead of driving
FatFs from whichever core called it. Newlib defines a fifth function,
`readdir_r`, in the same translation unit as the other four — so the kernel
image carries it too, dead weight pulled in by the object file rather than
by name. Nothing in this game, this project, or the graphics layer calls
`readdir_r`; it is not wrapped, and if anything ever did call it, it would
reach FatFs directly from whatever core called it, off core 0. Worth
checking again if a future change adds a directory scan through a different
path.

### An 8-bit surface

The graphics layer renders from 32-bit textures. The Build engine draws into
an 8-bit paletted buffer and converts it through its palette on the way out.
`host/circle_stubs.cpp` supplies the surface handling that conversion needs.

### The patchable-defaults block

Every port in this family carries a small block of text at a fixed place
inside the image, which anything holding the image before it boots can write
into. The kernel reads it at startup and adds what it finds to the game's
command line, so a setting can be changed for one boot, over the network,
without rebuilding anything or rewriting the card.

## The mouse

**The mouse is implemented and has not been tested on hardware by this
port.** The graphics layer implements the whole SDL mouse interface,
including the relative mode that first-person look-around needs, reading
movement straight from the USB report. Nothing in this project disables it.

Whether Duke's aiming actually feels right through it is unknown until
somebody plays it. That is a different statement from "there is no mouse",
and older notes elsewhere in this family of projects that say the pointer
never moves are out of date.

## Building

You need the Arm GNU `aarch64-none-elf` cross compiler, release 15.2.Rel1.
Put its `bin/` on your `PATH`, unpack it into `toolchains/`, or point
`RAPI_TOOLCHAIN_DIR` at it.

```
git clone --recursive https://github.com/Xalior/pi-eduke32.git
cd pi-eduke32
make deps          # long: builds newlib and libc++ from source, three times
make kernels       # the Pi 3, Pi 4 and Pi 5 images
make verify        # checks each image exists and is not empty
```

`make deps` builds three complete C library worlds, one per board, because
each is compiled for its own processor and an image is never portable
between boards. It takes a long time and a lot of disk. On a machine that
cannot hold all three, build one:

```
make deps-rpi4
make rpi4
```

## Game data, and `make media`

**This repository ships no game data, and `make card` never downloads
anything.** Two directories, and the difference between them matters:

| | |
|---|---|
| `media/` | Where game data lives on your machine. `make media` downloads into it; you copy your own files into it by hand. It is never committed and never shipped. |
| `build/sd-card/` | What `make card` stages. It **copies from `media/`** and fetches nothing. |

`make card` works whether or not `media/` has anything in it. A card built
with no data is a real card — it just says plainly that the GRP is missing.

```
make media
```

fetches exactly one file, with `curl`:

**`DUKE3D.GRP`** — the shareware release of Duke Nukem 3D's first episode,
*L.A. Meltdown*, version 1.1. 3D Realms distributed it free of charge in
1996, and eduke32's own project still points to it as the legitimate
no-cost way to run the engine:

```
https://archive.org/download/3D_Realms_Duke_Nukem_3D_Shareware/3D%20Realms%20-%20Duke%20Nukem%203D%20%28Shareware%20Version%29.zip
```

What arrives is checked against two hashes: the MD5 published independently
of this download — it is the well-documented checksum of the v1.1 shareware
GRP, listed on eduke32's own wiki FAQ — and a SHA256 computed from the file
this project fetched, so a later fetch is known to be identical. If either
does not match, the target stops rather than handing you a file to put on a
card. A `provenance.txt` is written beside it recording the URL, the date,
the licence and both hashes. Running it again re-verifies what is already
there instead of downloading it a second time.

**The full, commercial GRP is not fetched by anything here.** If you own
Duke Nukem 3D — GOG or Steam both sell *Duke Nukem 3D: 20th Anniversary
World Tour*, DRM-free once installed — copy your own `DUKE3D.GRP` into
`media/`. `make card` picks it up from there, in place of the shareware one.

Read this section before you run it. It is your machine and your
responsibility.

## The card

```
make card
```

stages the whole card into `build/sd-card/`:

```
kernel8.img              the Pi 3 image
kernel8-rpi4.img         the Pi 4 image
kernel_2712.img          the Pi 5 image
config.txt               tells the firmware which image this board boots
cmdline.txt              boot settings
games/eduke32/           the game's own directory, including DUKE3D.GRP if
                          media/ holds one
```

Copy that onto a FAT-formatted card together with the Raspberry Pi firmware
files, and boot it. If `media/` held no GRP, `make card` says so plainly —
put one in `games/eduke32/DUKE3D.GRP` on the card by hand before booting it.

Everything the game reads or writes stays inside `games/eduke32/`. One card
can carry several of these games, and each keeps to its own directory so
that two of them never overwrite each other's settings.

## Where the source comes from

EDuke32 is included as a submodule from
**`https://voidpoint.io/terminx/eduke32.git`**, the project's own GitLab
server. This is the repository EDuke32 publishes as its source and the one
its own website points at.

The project's history began in Subversion, and for years the Subversion
server was the only place the source lived. It has since moved to Git on the
server above, which carries that whole history. Copies exist on GitHub, but
they are unofficial mirrors that lag behind, so this project uses the
project's own server rather than a mirror of it.

The graphics layer, `circle-libsdl2`, is a submodule as well. It provides an
SDL2 interface on top of Circle, a bare-metal environment for the Raspberry
Pi.

Both submodules use `https://` addresses so that anyone can clone this
repository and everything in it without an account.

## State of this port

Read this section before expecting the game to run.

- **The whole game compiles and links clean for all three boards, but for
  one symbol.** `SDL_GL_DeleteContext` is the last thing circle-libsdl2 owes
  this port. It is dead code on this board — `sdlayer.cpp` calls it only
  from `if (sdl_context) SDL_GL_DeleteContext(sdl_context);` in its shutdown
  path, and `sdl_context` is always null here, since `USE_OPENGL` is unset
  and no GL context is ever created — but the symbol still has to resolve
  for the link to complete. `SDL_GL_CreateContext` is not in circle-libsdl2
  either, which reads as GL context management never having been in the
  shim's scope at all, rather than an oversight. Neither belongs in this
  repository: circle-libsdl2 is the SDL2 layer, and this is what it still
  owes this port. (A longer list — `SDL_CloseAudio`, `SDL_OpenURL`,
  `SDL_GetDisplayDPI`, `SDL_GetKeyboardFocus`, `SDL_GetWindowWMInfo`,
  `SDL_GL_GetDrawableSize`, `SDL_SetTextInputRect`,
  `SDL_Vulkan_GetDrawableSize` — was in this section until circle-libsdl2
  added them; dear imgui's SDL2 backend, called unconditionally every frame
  and every event for any SDL2 build, needed most of it.)
- **It runs on a Pi 5, and nothing has been seen on a screen yet.** The game
  starts, reads its GRP, compiles its scripts and reaches its main loop, and
  it stays there. Nobody has yet watched Duke's first level appear on a
  display, and serial output is not gameplay: until somebody has, this is a
  game that runs, not one that works. The Pi 3 and the Pi 4 have not been
  tried at all.
- **The picture is 640 by 480, because that is the smallest the engine
  accepts.** EDuke32 screens every display mode against its own minimum of
  640 by 480 and discards anything below it, and this port gives the game one
  display mode: the virtual display `host/kernel.cpp` declares. Declared
  smaller — at 320 by 200, the raster the game was drawn for — the mode list
  came out empty, and an empty mode list is not an error to EDuke32: its
  start-up skips setting a video mode, skips saying that it could not, and
  skips starting sound as well, so the game runs on with no window, no
  picture and no audio.
- **Nothing about performance is known.** The classic renderer costs time
  for every pixel it draws, on one core, with no acceleration of any kind,
  and 640 by 480 is nearly five times the pixels of the size the game was
  drawn for. The graphics layer scales that up to the panel. Whether a Pi 3
  can hold a playable frame rate at that size, and how much larger a Pi 5 can
  go, are open questions that only the hardware can answer.
- **Sound is untested**, and music is MIDI through the OPL3 emulation only.

## Licences

The code this project adds — everything in `host/`, the build files and this
document — is released under the GNU Lesser General Public License, version
3. See [LICENSE](LICENSE).

The parts that come from elsewhere keep their own licences:

- **Duke Nukem 3D's source code** was released by 3D Realms in 2003 under
  the GNU General Public License, version 2.
- **The Build engine** is Ken Silverman's, released in 2000 under its own
  licence. Its terms are in `eduke32/source/build/buildlic.txt`.
- **EDuke32** combines both, and the terms of each apply to the parts they
  cover.
- **Circle**, the bare-metal environment underneath, is released under the
  GNU General Public License, version 3.
- **The game data** — `DUKE3D.GRP` — is not covered by any of these and is
  not distributed here at all.

An image built from this repository combines all of the above, so
redistributing one means honouring every licence involved. Doing that for
yourself is straightforward; this project does not do it for you.
