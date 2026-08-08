#
# EDuke32 as a bootable Raspberry Pi kernel image.
#
# One board per invocation (RAPI_BOARD, default rpi5). The board selects
# three things at once: the circle-stdlib world the kernel is compiled and
# linked against, the shim archive built against that world, and — through
# the world's Config.mk — the CPU tuning and RASPPI value. Each board's
# newlib and libc++ are built for its own architecture, so an image is
# never portable between boards.
#
#   make                     the default board's image
#   make RAPI_BOARD=rpi3     that board's image
#   make clean-board         drop just this board's build/<board>/ tree
#
# Everything this invocation generates lands under build/<board>/: the
# kernel's own objects, every EDuke32 object, and the final
# .elf/.map/.lst/.img. The object names are shared across boards but their
# contents are not (different -mcpu, different -DRASPPI, different world),
# so a flat build directory would silently relink one board's objects into
# another board's kernel — a corrupt image with no build error. A directory
# per board keeps all three isolated: no `make clean` between boards, and
# three boards can build at the same time without touching each other.
#
# EDuke32's own source tree (../eduke32) is never written into: it is
# read-only input, and every object it produces is redirected here. Upstream
# builds itself with its own GNU makefile; that makefile is not used. It
# detects a desktop operating system, runs sdl2-config, and links against
# system libraries none of which exist here. What it decides is instead
# re-stated below as an explicit source list and an explicit set of feature
# switches, which is also what makes the choices readable in one place.
#
# Prerequisite: the board's world and shim archive exist —
# `make -C ../circle-libsdl2 deps`, or `make deps` at the repository root.
#

include ../mk/toolchain.mk

RAPI_BOARD ?= rpi5

# WHERE THE BUILT WORLD COMES FROM.
#
# The default is this game's own nested shim, so a clone of this repository
# alone builds and uses its own worlds and nothing needs setting. A parent
# that keeps ONE set of built worlds for several games — they are gigabytes
# each, and identical when the pin and the configuration match — points
# CIRCLE_WORLDS at them.
#
# Named before the include below rather than beside SHIM further down,
# because make reads an include when it reaches it.
CIRCLE_WORLDS ?= ../circle-libsdl2
WORLD = $(CIRCLE_WORLDS)/circle-stdlib-$(RAPI_BOARD)

include $(WORLD)/Config.mk

ROOT = ..
# WHERE THE SHIM COMES FROM.
#
# Unset, this repository's own pinned copy — what a clone of this repository
# gets, and what CI builds. A parent proving one shim across several games
# points SHIM at the single checkout it is proving, so the library is built
# once rather than once per game.
SHIM ?= $(ROOT)/circle-libsdl2
DUKE = $(ROOT)/eduke32

OBJDIR = build/$(RAPI_BOARD)

# Circle's own image names, which the Raspberry Pi firmware selects between
# from the model sections of config.txt: the Pi 3 world boots kernel8.img,
# the Pi 4 world kernel8-rpi4.img, the Pi 5 world kernel_2712.img.
IMAGE_rpi3 = kernel8
IMAGE_rpi4 = kernel8-rpi4
IMAGE_rpi5 = kernel_2712
IMAGE = $(IMAGE_$(RAPI_BOARD))
ifeq ($(IMAGE),)
$(error unknown RAPI_BOARD '$(RAPI_BOARD)' — expected rpi3, rpi4 or rpi5)
endif

TARGET = $(OBJDIR)/$(IMAGE)

# Stated explicitly because the first rule this file sees comes from an
# included makefile, and that would otherwise decide the default goal.
.DEFAULT_GOAL := image

# ---------------------------------------------------------------------------
# Sources
# ---------------------------------------------------------------------------

# The kernel wrapper, the syscall layer, the POSIX gap layer, the game-state
# storage mmulti.cpp would otherwise own, and this port's own SDL layer:
# C++, and this project's own code.
HOST_SRCS = main.cpp kernel.cpp defaults.cpp circle_syscalls.cpp \
	posix_compat.cpp duke_globals.cpp
HOST_OBJS = $(addprefix $(OBJDIR)/,$(HOST_SRCS:.cpp=.o))

# The SD card's read cache, which lives in circle-libsdl2 rather than here.
#
# It is not part of SDL and does not use it: it is Circle device-layer code
# that wraps the card and is picked up by name, so FatFs and every file the
# game opens go through it without knowing. It is kept in that repository
# because that is where the shared pieces are today, and it is compiled
# straight into this kernel — there is no library to link, and no other port
# has to agree with this one about how it is built.
DISKCACHE_DIR = $(SHIM)/circle-diskcache
DISKCACHE_OBJ = $(OBJDIR)/diskcache.o

# ---------------------------------------------------------------------------
# EDuke32's source list
# ---------------------------------------------------------------------------
#
# Upstream builds a directory at a time: every .c and .cpp in a module's src/
# except a named exclusion list. The lists below are that computation carried
# out once, with this port's feature switches applied, and written down. The
# exclusions come in three kinds and each is named where it is removed:
# another operating system's backend, the map editor (Mapster32, a second
# program that shares this source tree), and a feature switched off here.

# The Build engine. Its C files are compiled as C++, which is what upstream's
# CPLUSPLUS=1 does by default and what the rest of the tree is written to
# expect.
ENGINE_C = lz4.c miniz.c miniz_tdef.c miniz_tinfl.c xxhash.c

# a-c.cpp is the portable C rewrite of the Build engine's inner rendering
# loops, which upstream otherwise supplies as x86 assembly. It is the whole
# reason the classic renderer can run on this board at all.
ENGINE_CPP = \
	2d.cpp a-c.cpp baselayer.cpp cache1d.cpp clip.cpp colmatch.cpp \
	common.cpp communityapi.cpp compat.cpp cpuid.cpp crc32.cpp defs.cpp \
	dxtfilter.cpp engine.cpp fix16.cpp hash.cpp hightile.cpp klzw.cpp \
	kplib.cpp loguru.cpp md4.cpp mhk.cpp mutex.cpp osd.cpp palette.cpp \
	pngwrite.cpp pragmas.cpp rev.cpp screenshot.cpp screentext.cpp \
	scriptfile.cpp sdlayer.cpp sjson.cpp smalltextfont.cpp smmalloc.cpp \
	smmalloc_generic.cpp smmalloc_tls.cpp softsurface.cpp texcache.cpp \
	textfont.cpp tiles.cpp timer.cpp vfs.cpp

# The Duke Nukem 3D game itself.
DUKE_CPP = \
	actors.cpp anim.cpp cheats.cpp cmdline.cpp common.cpp config.cpp \
	demo.cpp dnames.cpp game.cpp gamedef.cpp gameexec.cpp \
	gamestructures.cpp gamevars.cpp global.cpp grpscan.cpp input.cpp \
	menus.cpp network.cpp osdcmds.cpp osdfuncs.cpp player.cpp premap.cpp \
	rts.cpp savegame.cpp sbar.cpp screens.cpp sector.cpp sounds.cpp \
	text.cpp

# The sound library. Its Windows and ALSA output drivers are left out; the
# SDL one is what this board has. The Vorbis, FLAC and XMP decoders are
# compiled — each is guarded by a define this build does not set, so each
# produces an object that reports the format is unavailable and says in its
# own source why. See the music note under DUKE_DEFINE below.
AUDIO_CPP = \
	driver_adlib.cpp driver_sdl.cpp driver_sf2.cpp drivers.cpp flac.cpp \
	formats.cpp fx_man.cpp gmtimbre.cpp midi.cpp mix.cpp mixst.cpp \
	multivoc.cpp music.cpp opl3.cpp pitch.cpp vorbis.cpp xa.cpp xmp.cpp

# The input and animation helper library shared by the Build games.
MACT_CPP = animlib.cpp control.cpp joystick.cpp keyboard.cpp scriplib.cpp

# Dear ImGui, EDuke32's in-game UI toolkit. sdlayer.cpp includes and calls
# its core (context creation, frame begin/end) and its SDL2 input backend
# unconditionally for any SDL2 build — only the OpenGL renderer backend is
# guarded by USE_OPENGL, and this board has none. The demo window, the Win32
# backend and the OpenGL backend are not in this list: nothing here calls
# them, matching the OpenGL exclusions above.
IMGUI_CPP = imgui.cpp imgui_draw.cpp imgui_tables.cpp imgui_widgets.cpp \
	imgui_impl_sdl2.cpp

# The window icon: a plain pixel array in upstream's resource directory,
# compiled like any other Build engine C file.
ICON_C = eduke32_icon.c

DUKE_SRCS = \
	$(addprefix source/build/src/,$(ENGINE_C) $(ENGINE_CPP)) \
	$(addprefix source/duke3d/src/,$(DUKE_CPP)) \
	$(addprefix source/duke3d/rsrc/,$(ICON_C)) \
	$(addprefix source/audiolib/src/,$(AUDIO_CPP)) \
	$(addprefix source/mact/src/,$(MACT_CPP)) \
	$(addprefix source/imgui/src/,$(IMGUI_CPP))

DUKE_OBJS = $(addprefix $(OBJDIR)/ek/,$(basename $(DUKE_SRCS)))
DUKE_OBJS := $(addsuffix .o,$(DUKE_OBJS))

OBJS = $(HOST_OBJS) $(DISKCACHE_OBJ) $(DUKE_OBJS)

STANDARD = -std=gnu++17

# Stated, not inherited: this is Circle's own default, written down so the
# build's optimization level is a fact in the file rather than a forensic
# reconstruction.
OPTIMIZE = -O2

# Circle's Rules.mk defines a link rule for $(TARGET).img with its own linker
# script; sdl-app.mk below defines the one this port actually uses. Two
# recipes for one target makes make warn on every board of every build — a
# warning that reads like a fault and is not one. Pointing TARGET at a name
# nothing builds while Rules.mk is read attaches its rule there instead, so
# there is only ever one recipe for the image.
SDL_APP_IMAGE := $(TARGET)
TARGET := $(OBJDIR)/.circle-unused
include $(CIRCLEHOME)/Rules.mk
TARGET := $(SDL_APP_IMAGE)

# The world must be the multicore one circle-libsdl2 configures: the shim's
# core-split machinery cannot exist without it, and an object compiled
# against a single-core world links without complaint and then misbehaves.
# Every object here inherits the flag through Rules.mk's CPPFLAGS, so this
# check covers the whole build.
ifeq ($(findstring ARM_ALLOW_MULTI_CORE,$(DEFINE)),)
$(error the payload requires the multicore circle-stdlib world — build it with 'make -C ../circle-libsdl2 deps')
endif

# WHERE THIS GAME LIVES ON THE CARD. One card carries several games, and two
# of them writing a `settings.cfg` into the FAT root would each silently
# overwrite the other's. Every game therefore keeps to a directory of its
# own, and nothing of this port's ever touches the root.
#
# It is a build-time constant, not a boot switch: a card's layout is decided
# when the card is staged, and a game that could be relocated at boot would
# only mean two places to look for the same file.
#
# The path is used ONLY by this port's own host layer — argv, the working
# directory, and SDL's preference path. Upstream EDuke32 is never told about
# it and needs no patch: it looks where it is pointed.
RAPI_GAME_DIR ?= /games/eduke32
DEFINE += -DRAPI_GAME_DIR='"$(RAPI_GAME_DIR)"'

# ---------------------------------------------------------------------------
# Include paths
# ---------------------------------------------------------------------------
#
# sdl2ext/ carries the POSIX headers this board's C library does not have but
# EDuke32's sources include. Each one is a declaration only; the definitions
# are in posix_compat.cpp, and every one of them says there what it really
# does. It goes FIRST so those headers are found before anything else on the
# path, and it is also where eduke32-compat.h lives.
#
# The glad directory is on the path even though this is a software-renderer
# build with no OpenGL in it: build.h includes glad/glad.h unconditionally,
# so the declarations have to be readable. No glad object is compiled and no
# GL entry point is ever called. The imgui directory is on the path for its
# own headers, which sdlayer.cpp includes unconditionally (see IMGUI_CPP
# above for which of its sources are actually compiled).
INCLUDE := -I $(CURDIR)/sdl2ext \
	-I $(SHIM)/include -I $(SHIM)/include/SDL2 \
	-I $(DISKCACHE_DIR) \
	-I $(DUKE)/source/build/include \
	-I $(DUKE)/source/glad/include \
	-I $(DUKE)/source/imgui/include \
	-I $(DUKE)/source/mact/include \
	-I $(DUKE)/source/audiolib/include \
	-I $(DUKE)/source/voidwrap/include \
	-I $(DUKE)/source/duke3d/src \
	$(CIRCLE_STDLIB_INCLUDES) $(INCLUDE)

# ---------------------------------------------------------------------------
# EDuke32's own feature switches, as this port sets them
# ---------------------------------------------------------------------------
#
# These are upstream's switches, not this port's inventions: each is a name
# upstream's own build system sets, given the value this board requires.
#
#   RENDERTYPESDL   the SDL video and input backend, which is the only one
#                   that is not tied to Windows.
#   NOASM           use a-c.cpp, the portable C rewrite of the renderer's
#                   inner loops, instead of the x86 assembly ones.
#   NETCODE_DISABLE no multiplayer. There is no network stack under this
#                   board, and the two files that would carry one — the
#                   bundled ENet and mmulti.cpp — are left out of the source
#                   list above rather than compiled to nothing.
#   B_*_ENDIAN      upstream detects endianness from the host operating
#                   system's headers and stops the build when it recognises
#                   none. It recognises none here, so it is told: every
#                   Raspberry Pi runs little-endian.
#
# NOT SET, and each absence is a decision:
#
#   USE_OPENGL      THE SOFTWARE (CLASSIC) RENDERER ONLY. Leaving this unset
#                   removes both hardware renderers — Polymost and Polymer —
#                   and with them glbuild, glsurface, mdsprite, tilepacker
#                   and voxmodel, none of which are in the source list. There
#                   is no OpenGL under this board: the shim draws through
#                   Circle's framebuffer, and Duke's 8-bit classic renderer
#                   is what it was written for anyway.
#   POLYMER         follows USE_OPENGL, as it does upstream.
#   USE_LIBVPX      the VP8 cutscene decoder, which is an OpenGL path and a
#                   separate library. animvpx.cpp is not in the source list.
#   STARTUP_WINDOW  the pre-game settings dialog, which is a GTK, Windows or
#                   Cocoa window. There is no window manager here.
#   HAVE_GTK2       as above.
#   USE_MIMALLOC    a thread-aware allocator. This board runs the game on one
#                   core with no thread that can contend, and the allocator
#                   underneath is Circle's.
#   HAVE_VORBIS
#   HAVE_FLAC
#   HAVE_XMP        the compressed-music decoders. Two of them run the decode
#                   on a worker thread through the bundled libasync, which
#                   builds on std::thread; there are no threads here. Music
#                   from the GRP is MIDI, which the OPL3 emulation plays
#                   without any of them.
#   USE_PHYSFS      an archive-backed virtual filesystem. The GRP is read
#                   directly.
DUKE_DEFINE = \
	-DRENDERTYPESDL=1 -DSDL_TARGET=2 -DNOASM -DNETCODE_DISABLE \
	-DB_LITTLE_ENDIAN=1 -DB_BIG_ENDIAN=0

# EDuke32 is C++14 with exceptions and RTTI switched off, which is how
# upstream builds it. eduke32-compat.h is forced in front of every
# translation unit: it declares the POSIX functions this board's C library
# has but hides behind feature macros it does not define.
#
# -funsigned-char comes AFTER $(CFLAGS), not before, and the order is the
# whole point. Circle's Rules.mk puts -fsigned-char in CFLAGS, and the last
# of the two on a command line wins. The Build engine is written throughout
# for a plain `char` that cannot be negative — a table it declares as
# `const char[] = {1,2,4,...,128}` does not even compile with a signed one —
# so this must be the one that survives.
DUKE_CFLAGS = -std=gnu++14 -fno-exceptions -fno-rtti \
	-include eduke32-compat.h $(DUKE_DEFINE) $(CFLAGS) $(EXTRACPPFLAGS) \
	-funsigned-char

# The shim comes in whole, every member, rather than only the members the
# link happens to need. An object file linked straight into the kernel beats
# an archive member of the same name and does it silently, so a stub in
# circle_stubs.cpp left behind after the library implemented the real thing
# would quietly win — the game would keep calling the stub and nothing would
# say so. Pulling the archive in whole turns that into a duplicate-symbol
# error at link time, which is the only moment it can still be cheap to fix.
LIBS = --whole-archive $(SHIM)/libSDL2-$(RAPI_BOARD).a --no-whole-archive \
	$(CIRCLE_STDLIB_LIBS)

# ---------------------------------------------------------------------------
# Filesystem syscalls, redirected at link time
# ---------------------------------------------------------------------------
#
# The game runs on the application core, and newlib's Circle glue drives
# FatFs and the SD card straight from wherever it is called — which off core
# 0 is illegal. circle_syscalls.cpp puts the shim's any-core I/O service
# underneath the C library instead; these flags are what put it there.
#
# --wrap rather than redefinition, because redefinition cannot work here:
# the glue defines _open, _read, _write, _close, _lseek, _fstat, opendir,
# readdir, closedir, dup and more in a SINGLE object file. Redefining part
# of that set either collides at link time or, worse, leaves the originals
# linked in beside the replacements with no diagnostic. --wrap renames the
# references instead and leaves the vendored glue untouched — and it is what
# keeps __real_* reachable, which is how the service performs the real work
# once it has reached core 0.
#
# Every name here must also appear in circle_syscalls.cpp: a wrap with no
# __wrap_ definition is an undefined reference, so the link is the check.
WRAPPED_SYSCALLS = \
	_open _close _read _write _lseek _fstat _stat lstat _unlink _rename \
	_fcntl mkdir chdir getcwd dup dup2 \
	opendir readdir closedir rewinddir

# ---------------------------------------------------------------------------
# Two SDL functions the shim owns and this port extends
# ---------------------------------------------------------------------------
#
# NO SDL IS WRAPPED. The Build engine's frame buffer is 8-bit paletted, and
# this port used to intercept SDL_CreateRGBSurface and SDL_FreeSurface to add
# that case — the library did not have it, and a surface with a palette
# cannot be freed by a function that knows nothing about palettes.
#
# circle-libsdl2 does paletted surfaces now: the format reports 8 bits, the
# palette is allocated with the surface and freed with it, and blitting,
# conversion and colour keys all resolve through it. So the pair is gone and
# the engine reaches the library's own implementations directly.
#
# Worth knowing if SDL ever needs wrapping again: --wrap slips past the
# --whole-archive check below. It renames an undefined reference rather than
# defining a duplicate, so a wrapper is invisible to the duplicate-symbol
# test that catches every other kind of private SDL.

# The link is driven by the cross ld directly (Circle's Rules.mk sets
# LD = <prefix>ld for GCC builds), so these are plain linker options, not
# compiler-driver -Wl, ones.
LDFLAGS += $(addprefix --wrap=,$(WRAPPED_SYSCALLS))

# ---------------------------------------------------------------------------
# Compilation
# ---------------------------------------------------------------------------

# This project's own objects, board-scoped. These more-specific rules win
# over Rules.mk's in-place %.o/%.d, so nothing is written beside the source.
$(OBJDIR)/%.o: %.cpp | $(OBJDIR)
	@echo "  CPP   $@"
	@$(CPP) $(CPPFLAGS) -c -o $@ $<

$(OBJDIR)/%.d: %.cpp | $(OBJDIR)
	@$(CPP) $(CPPFLAGS) -M -MG -MT $(@:.d=.o) -MT $@ -MF $@ $<

# The disk cache, which lives outside this directory. Its own rule because
# the pattern rule above only reaches sources beside this makefile, and its
# object goes in this board's tree like every other.
$(DISKCACHE_OBJ): $(DISKCACHE_DIR)/diskcache.cpp | $(OBJDIR)
	@echo "  CPP   $@"
	@$(CPP) $(CPPFLAGS) -c -o $@ $<

$(OBJDIR)/diskcache.d: $(DISKCACHE_DIR)/diskcache.cpp | $(OBJDIR)
	@$(CPP) $(CPPFLAGS) -M -MG -MT $(DISKCACHE_OBJ) -MT $@ -MF $@ $<

# EDuke32's objects, mirroring its source tree under $(OBJDIR)/ek/. The
# source tree itself is the upstream submodule, and is never written to.
# Both extensions go through the C++ compiler, which is what upstream's
# CPLUSPLUS=1 does.
$(OBJDIR)/ek/%.o: $(DUKE)/%.cpp
	@mkdir -p $(dir $@)
	@echo "  CXX   $@"
	@$(CPP) $(DUKE_CFLAGS) -c -o $@ $<

$(OBJDIR)/ek/%.d: $(DUKE)/%.cpp
	@mkdir -p $(dir $@)
	@$(CPP) $(DUKE_CFLAGS) -M -MG -MT $(@:.d=.o) -MT $@ -MF $@ $<

$(OBJDIR)/ek/%.o: $(DUKE)/%.c
	@mkdir -p $(dir $@)
	@echo "  CXX   $@"
	@$(CPP) $(DUKE_CFLAGS) -c -o $@ $<

$(OBJDIR)/ek/%.d: $(DUKE)/%.c
	@mkdir -p $(dir $@)
	@$(CPP) $(DUKE_CFLAGS) -M -MG -MT $(@:.d=.o) -MT $@ -MF $@ $<

# EDuke32's main() is the program's entry point on a desktop, but here main()
# belongs to the Circle kernel (main.cpp in this directory), which calls the
# game once the board is up. Renaming the entry point through the
# preprocessor for this one translation unit keeps upstream's source
# untouched, and the kernel calls the new name.
$(OBJDIR)/ek/source/build/src/sdlayer.o: $(DUKE)/source/build/src/sdlayer.cpp
	@mkdir -p $(dir $@)
	@echo "  CXX   $@ (entry point renamed)"
	@$(CPP) $(DUKE_CFLAGS) -Dmain=eduke32_main -c -o $@ $<

$(OBJDIR)/ek/source/build/src/sdlayer.d: $(DUKE)/source/build/src/sdlayer.cpp
	@mkdir -p $(dir $@)
	@$(CPP) $(DUKE_CFLAGS) -Dmain=eduke32_main -M -MG -MT $(@:.d=.o) -MT $@ -MF $@ $<

$(OBJDIR):
	@mkdir -p $(OBJDIR)

-include $(DEPS)

# The image link rule. sdl-app.mk lets the caller choose the linker script,
# so this build supplies its own: the shim's script with room made at image
# offset 0x800 for the patchable-defaults block. Everything else about the
# script — including the TLS-section adjacency binutils 2.44 and later
# require — is inherited from the shim's.
SDL_APP_LDSCRIPT = $(CURDIR)/eduke32-defaults.ld
include $(SHIM)/sdl-app.mk

# The other half of the guarantee the linker script's ASSERTs begin: check
# the BYTES of the finished image, not just the link. A pre-boot writer
# refuses to stamp an image whose magic is not at 0x800, so an image that
# lost it would boot and then quietly ignore everything ever sent to it —
# including --rapi-debug-uart, which is the channel used to drive the board
# when nobody is at the bench. Better to have no image than that one.
.PHONY: image
image: $(TARGET).img
	@if [ "`dd if=$(TARGET).img bs=4 skip=512 count=1 2>/dev/null`" = "PM8D" ]; \
	then \
		echo "  GATE  $(TARGET).img defaults block at 0x800: present"; \
	else \
		echo "  GATE  $(TARGET).img: defaults block ABSENT at 0x800 — image deleted"; \
		rm -f $(TARGET).img; \
		exit 1; \
	fi

# Board-scoped clean: drop just this board's tree, leaving the other two
# boards' images and objects untouched.
.PHONY: clean-board
clean-board:
	rm -rf $(OBJDIR) build/$(RAPI_BOARD)-stub
