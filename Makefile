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
#   make netboot             stage the Pi 5 image and its boot configuration
#                            into build/netboot-rpi5/
#   make card                stage the whole card, except the game data, into
#                            build/sd-card/
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

.PHONY: deps kernels verify netboot card clean-boards $(BOARDS)
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
# the three kernels and the boot configuration. The GRP file is not here and
# is not ours to ship — see the README for which one to use and where it
# legitimately comes from.
CARD_DIR = build/sd-card
card: kernels
	@rm -rf $(CARD_DIR)
	@mkdir -p $(CARD_DIR)/games/eduke32
	@cp host/build/rpi3/$(IMAGE_rpi3) $(CARD_DIR)/
	@cp host/build/rpi4/$(IMAGE_rpi4) $(CARD_DIR)/
	@cp host/build/rpi5/$(IMAGE_rpi5) $(CARD_DIR)/
	@cp host/config.txt host/cmdline.txt $(CARD_DIR)/
	@echo "  STAGED $(CARD_DIR)/"
	@echo "  The Raspberry Pi firmware files and the GRP are not staged here."
	@echo "  Put DUKE3D.GRP in $(CARD_DIR)/games/eduke32/ before writing the card."

clean-boards:
	@for b in $(BOARDS); do $(MAKE) -C host RAPI_BOARD=$$b clean-board; done
	rm -rf $(NETBOOT_DIR) $(CARD_DIR)
