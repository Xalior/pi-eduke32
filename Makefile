#
# pi-eduke32 — EDuke32 as a bootable bare-metal Raspberry Pi image.
#
#   make check-toolchain     report the cross compiler this build will use
#   make deps                the three circle-stdlib worlds and the shim
#                            archives built against them (long: the worlds
#                            build newlib and libc++ from source)
#   make deps-rpi4           the same for one board only, for a machine that
#                            cannot hold three worlds at once
#   make rpi5 | rpi4 | rpi3  one board's kernel image
#   make kernels             all three, built in parallel
#   make verify              truth-gate: every image exists and is non-empty
#   make media               download the freely redistributable game data
#                            into media/
#   make netboot             stage the Pi 5 image and its boot configuration
#                            into build/netboot-rpi5/
#   make card                stage the whole card into build/sd-card/, copying
#                            in whatever media/ holds and naming what it does
#                            not. It never downloads anything
#   make clean-boards        drop every board's build tree
#
# The three boards never share mutable state: each has its own circle-stdlib
# world, its own shim archive and its own object directory, so building them
# at the same time is safe and building one never disturbs another.
#
# The libc++ sources every world is built from are one immutable git tag, and
# CIRCLE_LLVM says where that checkout lives. The default puts it beside this
# repository, which is right for a plain clone and for a CI runner. Point
# several projects at one directory to fetch it once for all of them:
#
#   make deps CIRCLE_LLVM=/path/to/circle-llvm
#

include mk/toolchain.mk

# Stated explicitly because the first rule this file sees comes from an
# included makefile, and that would otherwise decide the default goal.
.DEFAULT_GOAL := kernels

BOARDS ?= rpi3 rpi4 rpi5

IMAGE_rpi3 = kernel8.img
IMAGE_rpi4 = kernel8-rpi4.img
IMAGE_rpi5 = kernel_2712.img

.PHONY: deps kernels verify media netboot card clean-boards $(BOARDS)
.PHONY: $(addprefix deps-,$(BOARDS))

deps:
	$(MAKE) -C circle-libsdl2 deps

# One board's dependencies: its own circle-stdlib world and the shim archive
# built against it. A machine with a small disk — a CI runner, most obviously
# — builds one board at a time and keeps only that board's world.
# Written as a static pattern rule over the board list rather than a plain
# pattern rule: these targets are phony, and make does not apply pattern rules
# to phony targets — it would quietly answer "nothing to be done" and leave
# the world unbuilt.
$(addprefix deps-,$(BOARDS)): deps-%:
	$(MAKE) -C circle-libsdl2 world BOARD=$*
	$(MAKE) -C circle-libsdl2 libSDL2-$*.a BOARD=$*

$(BOARDS): check-toolchain
	$(MAKE) -C host RAPI_BOARD=$@

# All three at once. Each sub-make owns a different world and a different
# output directory, so there is nothing for them to collide on.
#
# Each board is waited for BY PID, and its status kept. A bare `wait` reports
# only that the shell has no children left — it is success whatever the jobs
# did — so a board that failed to build would leave this target reporting
# success, and the truth-gate would then pass the board's PREVIOUS image,
# still on disk.
kernels: check-toolchain
	@pids=; fail=0; \
	for b in $(BOARDS); do $(MAKE) -C host RAPI_BOARD=$$b & pids="$$pids $$!"; done; \
	for p in $$pids; do wait $$p || fail=1; done; \
	exit $$fail

# Truth-gate: ask the filesystem, not the exit codes. An image that is
# missing or empty fails here even if the build claimed success.
verify:
	@fail=0; \
	for b in $(BOARDS); do \
		case $$b in \
			rpi3) img=host/build/rpi3/$(IMAGE_rpi3) ;; \
			rpi4) img=host/build/rpi4/$(IMAGE_rpi4) ;; \
			rpi5) img=host/build/rpi5/$(IMAGE_rpi5) ;; \
		esac; \
		if [ -s "$$img" ]; then \
			echo "  OK    $$img ($$(wc -c < $$img | tr -d ' ') bytes)"; \
		else \
			echo "  FAIL  $$img missing or empty"; fail=1; \
		fi; \
	done; \
	exit $$fail

# ---------------------------------------------------------------------------
# Game data
# ---------------------------------------------------------------------------
#
#   media/           what `make media` downloads. Gitignored, never shipped,
#                    and never part of a build.
#   build/sd-card/   what `make card` stages. It copies from media/ and
#                    fetches nothing.
#
# `card` does not depend on `media`, so a card built without it is complete
# except for the data and names the file that is absent.
#
# `make media` fetches one file: DUKE3D.GRP, the shareware release of Duke
# Nukem 3D's first episode, "L.A. Meltdown". No other GRP — the full retail
# GRP (all four episodes, Atomic Edition) is a commercial product and is
# copied into media/ by hand.
#
# What arrives is checked against both checksums. The MD5 is the one 3D
# Realms and eduke32's own wiki FAQ publish for the v1.1 shareware GRP,
# documented independently of this download; the SHA256 was computed from
# the file this project fetched, so a later fetch is known to be identical.
# Re-running re-verifies rather than re-downloading.
MEDIA_DIR = media

DUKE3D_GRP     = $(MEDIA_DIR)/DUKE3D.GRP
DUKE3D_ZIP_URL = https://archive.org/download/3D_Realms_Duke_Nukem_3D_Shareware/3D%20Realms%20-%20Duke%20Nukem%203D%20%28Shareware%20Version%29.zip
DUKE3D_SHA256  = b218feef4a7b0e48027de17746bd744dc0624b275b9dac0ea8a726f85b48887f
DUKE3D_MD5     = 9b0683a74c8bf36bf85631616385bec8

# sha256sum and md5sum on Linux, shasum and md5 on macOS. Whichever exists;
# if either is missing the target stops rather than accepting a download it
# cannot check.
SHA256SUM := $(firstword $(shell command -v sha256sum 2>/dev/null) \
                         $(shell command -v shasum 2>/dev/null))
MD5SUM    := $(firstword $(shell command -v md5sum 2>/dev/null) \
                         $(shell command -v md5 2>/dev/null))

media:
	@if [ -z "$(SHA256SUM)" ] || [ -z "$(MD5SUM)" ]; then \
		echo "  MEDIA no checksum tool on this machine (sha256sum/shasum and"; \
		echo "        md5sum/md5 are both needed) — refusing to download"; \
		echo "        something that cannot be verified."; \
		exit 1; \
	fi
	@command -v unzip >/dev/null 2>&1 || { \
		echo "  MEDIA no 'unzip' on this machine — the shareware release is"; \
		echo "        distributed as a zip and cannot be unpacked without it."; \
		exit 1; }
	@mkdir -p $(MEDIA_DIR)
	@if [ -f "$(DUKE3D_GRP)" ]; then \
		echo "  MEDIA $(DUKE3D_GRP) already here — verifying"; \
	else \
		echo "  MEDIA fetching $(DUKE3D_ZIP_URL)"; \
		curl -fL --retry 3 -o "$(MEDIA_DIR)/duke3d-shareware.zip.part" "$(DUKE3D_ZIP_URL)" || { \
			rm -f "$(MEDIA_DIR)/duke3d-shareware.zip.part"; \
			echo "  MEDIA download failed"; exit 1; }; \
		mv "$(MEDIA_DIR)/duke3d-shareware.zip.part" "$(MEDIA_DIR)/duke3d-shareware.zip"; \
		rm -rf "$(MEDIA_DIR)/.duke3d-extract"; \
		mkdir -p "$(MEDIA_DIR)/.duke3d-extract"; \
		unzip -q -j -o "$(MEDIA_DIR)/duke3d-shareware.zip" -d "$(MEDIA_DIR)/.duke3d-extract" || { \
			rm -rf "$(MEDIA_DIR)/.duke3d-extract" "$(MEDIA_DIR)/duke3d-shareware.zip"; \
			echo "  MEDIA could not unpack the shareware zip"; exit 1; }; \
		grp=`find "$(MEDIA_DIR)/.duke3d-extract" -iname '*.grp' | head -1`; \
		if [ -z "$$grp" ]; then \
			rm -rf "$(MEDIA_DIR)/.duke3d-extract" "$(MEDIA_DIR)/duke3d-shareware.zip"; \
			echo "  MEDIA no .GRP file found inside the shareware zip"; exit 1; \
		fi; \
		mv "$$grp" "$(DUKE3D_GRP)"; \
		rm -rf "$(MEDIA_DIR)/.duke3d-extract" "$(MEDIA_DIR)/duke3d-shareware.zip"; \
	fi
	@got=`$(SHA256SUM) -a 256 "$(DUKE3D_GRP)" 2>/dev/null || $(SHA256SUM) "$(DUKE3D_GRP)"`; \
	got=`echo "$$got" | awk '{print $$1}'`; \
	if [ "$$got" != "$(DUKE3D_SHA256)" ]; then \
		echo "  MEDIA SHA256 MISMATCH for $(DUKE3D_GRP)"; \
		echo "        expected $(DUKE3D_SHA256)"; \
		echo "        got      $$got"; \
		echo "        the file has been left in place for inspection, and is"; \
		echo "        NOT safe to put on a card."; \
		exit 1; \
	fi; \
	got=`$(MD5SUM) -q "$(DUKE3D_GRP)" 2>/dev/null || $(MD5SUM) "$(DUKE3D_GRP)"`; \
	got=`echo "$$got" | awk '{print $$1}'`; \
	if [ "$$got" != "$(DUKE3D_MD5)" ]; then \
		echo "  MEDIA MD5 MISMATCH for $(DUKE3D_GRP)"; \
		echo "        expected $(DUKE3D_MD5) — the published checksum of the"; \
		echo "        v1.1 shareware GRP"; \
		echo "        got      $$got"; \
		exit 1; \
	fi; \
	head -c 12 "$(DUKE3D_GRP)" | grep -q KenSilverman || { \
		echo "  MEDIA $(DUKE3D_GRP) does not begin with the Build engine's GRP magic"; exit 1; }; \
	echo "  MEDIA $(DUKE3D_GRP) verified ($$(wc -c < $(DUKE3D_GRP) | tr -d ' ') bytes)"
	@printf '%s\n' \
		"DUKE3D.GRP — the Duke Nukem 3D shareware GRP" \
		"" \
		"Source:   $(DUKE3D_ZIP_URL)" \
		"Item:     https://archive.org/details/3D_Realms_Duke_Nukem_3D_Shareware" \
		"Fetched:  `date -u '+%Y-%m-%d %H:%M:%S UTC'`" \
		"SHA256:   $(DUKE3D_SHA256)  (computed from this download)" \
		"MD5:      $(DUKE3D_MD5)  (published checksum of the v1.1 shareware GRP," \
		"          documented independently of this download — see eduke32's" \
		"          own wiki FAQ)" \
		"" \
		"What it is: the one-episode shareware release of Duke Nukem 3D," \
		"\"L.A. Meltdown\", version 1.1 — every level, texture, sound and piece" \
		"of music for that episode." \
		"" \
		"Licence: 3D Realms' original 1996 shareware terms. The one-episode" \
		"shareware release is freely redistributable, unmodified, for free," \
		"non-commercially. eduke32's own project points users at this exact" \
		"free distribution as the legitimate no-cost way to run the engine." \
		"" \
		"Duke Nukem 3D is a trademark of 3D Realms / Gearbox Software. This" \
		"file is not redistributed by this repository." \
		> $(MEDIA_DIR)/provenance.txt
	@echo "  MEDIA provenance written to $(MEDIA_DIR)/provenance.txt"

# The Pi 5 netboot bundle: the image the Pi 5 firmware looks for, plus the
# boot configuration it must be served alongside. Copy the contents into the
# TFTP root the board boots from (the Raspberry Pi firmware files themselves
# come from that root's existing installation, not from here).
NETBOOT_DIR = build/netboot-rpi5
netboot: rpi5
	@mkdir -p $(NETBOOT_DIR)
	@cp host/build/rpi5/$(IMAGE_rpi5) $(NETBOOT_DIR)/
	@cp host/config.txt host/cmdline.txt $(NETBOOT_DIR)/
	@echo "  STAGED $(NETBOOT_DIR)/"
	@ls -l $(NETBOOT_DIR)/

# The card, staged into a directory to copy onto media formatted elsewhere:
# the three kernels, the boot configuration and whatever game data media/
# happens to hold.
#
# Everything belonging to this game lives in one directory on the card, named
# by RAPI_GAME_DIR in host/Makefile. A card carries several games, and two of
# them writing a settings file into the FAT root would each silently
# overwrite the other's.
#
# This target downloads nothing. It copies what `make media` left and names
# what is absent.
CARD_DIR  = build/sd-card
CARD_GAME = $(CARD_DIR)/games/eduke32

card: kernels
	@rm -rf $(CARD_DIR)
	@mkdir -p $(CARD_GAME)
	@cp host/build/rpi3/$(IMAGE_rpi3) $(CARD_DIR)/
	@cp host/build/rpi4/$(IMAGE_rpi4) $(CARD_DIR)/
	@cp host/build/rpi5/$(IMAGE_rpi5) $(CARD_DIR)/
	@cp host/config.txt host/cmdline.txt $(CARD_DIR)/
	@echo "  STAGED $(CARD_DIR)/"
	@for f in DUKE3D.GRP duke3d.grp; do \
		if [ -f "$(MEDIA_DIR)/$$f" ]; then \
			cp "$(MEDIA_DIR)/$$f" $(CARD_GAME)/DUKE3D.GRP; \
			echo "  DATA   $$f"; \
		fi; \
	done
	@echo
	@if ls $(CARD_GAME)/*.GRP >/dev/null 2>&1 || ls $(CARD_GAME)/*.grp >/dev/null 2>&1; then :; else \
		echo "  ABSENT no GRP. The game cannot start without one. Either the"; \
		echo "         full DUKE3D.GRP from a copy of the game you own, or"; \
		echo "         the free shareware GRP — 'make media' fetches that one."; \
	fi
	@echo "  NOTE   The Raspberry Pi firmware files are not staged here either."
	@echo "         See README.md."

clean-boards:
	@for b in $(BOARDS); do $(MAKE) -C host RAPI_BOARD=$$b clean-board; done
	rm -rf $(NETBOOT_DIR) $(CARD_DIR)
