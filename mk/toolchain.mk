# toolchain.mk — find the Arm GNU aarch64-none-elf cross toolchain, and the
# GNU tools circle-stdlib's configure needs on macOS.
#
# Include this at the top of any Makefile that compiles for the Pi.
#
# The cross compiler is looked for on PATH first, so a machine that already
# has it installed (a stranger's, a CI runner's) is left alone. Failing that,
# two places are searched, in order:
#
#   $RAPI_TOOLCHAIN_DIR — set it when the toolchain lives somewhere else
#                         entirely, which is the case for anyone consuming
#                         this repository from outside it.
#   toolchains/         — a copy unpacked into this repository.
#
# Either may be the unpacked toolchain itself or a directory holding one or
# more unpacked releases:
#
#   <dir>/arm-gnu-toolchain-<release>-aarch64-none-elf/bin/
#   <dir>/bin/
#
# That directory is not tracked in git — it holds a couple of gigabytes of
# vendor binaries. Download release 15.2.Rel1 for the aarch64-none-elf
# target, matching the machine you build ON, from
# https://developer.arm.com/downloads/-/arm-gnu-toolchain-downloads and
# unpack it there, or put its bin/ on your PATH. A symbolic link to a copy
# unpacked elsewhere works just as well.
#
# Build from the repository root — `make rpi5`, not `make -C host`. macOS
# ships GNU make 3.81, which passes a makefile's exported variables to its
# recipes but NOT to the $(shell ...) function. Circle's Rules.mk finds the
# compiler's support objects with $(shell $(CPP) -print-file-name=...), so
# in a make started without the toolchain already in its environment those
# lookups come back empty while every compile succeeds — and the failure
# surfaces much later as the linker reporting it "cannot find" a file with
# no name. Running from the root avoids it: the PATH exported here reaches
# the sub-make as an ordinary environment variable, because the sub-make is
# started from a recipe.

TOOLCHAIN_MK_DIR := $(patsubst %/,%,$(dir $(lastword $(MAKEFILE_LIST))))
REPO_ROOT        := $(abspath $(TOOLCHAIN_MK_DIR)/..)

# Where to look, in order, when the compiler is not already on PATH:
# RAPI_TOOLCHAIN_DIR if the environment names one, then this repository's own
# toolchains/. Either may be the unpacked toolchain itself (it has a bin/) or
# a directory holding one or more unpacked releases.
# A RAPI_TOOLCHAIN_DIR that names a directory which is not there is a
# mistake, not an absence, and it is refused rather than searched past. The
# variable comes from an environment rather than from this file, so it beats
# any default here: left to fall through, the build carries on with whatever
# else it finds and the wrong toolchain - or none, and a link error naming a
# file with no name - is what surfaces instead of the setting that caused it.
ifneq ($(RAPI_TOOLCHAIN_DIR),)
ifeq ($(wildcard $(RAPI_TOOLCHAIN_DIR)),)
$(error RAPI_TOOLCHAIN_DIR names $(RAPI_TOOLCHAIN_DIR), which does not exist)
endif
endif

TOOLCHAIN_SEARCH := $(RAPI_TOOLCHAIN_DIR) $(REPO_ROOT)/toolchains

ifeq ($(shell command -v aarch64-none-elf-gcc 2>/dev/null),)
TOOLCHAIN_BIN := $(firstword \
	$(wildcard $(addsuffix /arm-gnu-toolchain-*-aarch64-none-elf/bin,$(TOOLCHAIN_SEARCH))) \
	$(wildcard $(addsuffix /bin,$(TOOLCHAIN_SEARCH))))
ifneq ($(TOOLCHAIN_BIN),)
export PATH := $(TOOLCHAIN_BIN):$(PATH)
endif
endif

# GNU getopt for circle-stdlib's configure: macOS's BSD getopt drops long
# options, which lands as "Error: Invalid toolchain prefix".
GETOPT_BIN := $(firstword $(wildcard /opt/homebrew/opt/gnu-getopt/bin /usr/local/opt/gnu-getopt/bin))
ifneq ($(GETOPT_BIN),)
export PATH := $(GETOPT_BIN):$(PATH)
endif

# A bash 5 for `bash ./configure`: macOS ships 3.2, which has no mapfile.
BASH5_BIN := $(firstword $(wildcard /opt/homebrew/bin/bash /usr/local/bin/bash))
ifneq ($(BASH5_BIN),)
export PATH := $(patsubst %/,%,$(dir $(BASH5_BIN))):$(PATH)
endif

.PHONY: check-toolchain
check-toolchain:
	@command -v aarch64-none-elf-g++ >/dev/null 2>&1 || { \
		echo "aarch64-none-elf-g++ not found."; \
		echo "Put the Arm GNU aarch64-none-elf toolchain on your PATH, or"; \
		echo "unpack it into $(REPO_ROOT)/toolchains/, or set RAPI_TOOLCHAIN_DIR"; \
		echo "to where it lives — see mk/toolchain.mk."; \
		exit 1; }
	@echo "  TOOLCHAIN $$(command -v aarch64-none-elf-g++)"
	@aarch64-none-elf-g++ --version | head -1
