//
// defaults.h — this kernel's side of the patchable-defaults block.
//
// The block is a fixed region inside the kernel image that anything holding
// the image before it boots — a build step, a network loader, a boot picker
// — can write a line of text into. The kernel reads that text at startup,
// splits it into arguments, and hands them to the game. So a switch can be
// changed per boot, over the network, without editing the card or rebuilding
// anything.
//
// The block's layout is a shared interface, in defaultsblock.h. This header
// only says how this kernel consumes it.
//
// Arguments beginning `--rapi-` belong to the kernel: they are acted on here
// and removed, so the game never sees them. Everything else is passed
// through to the game untouched.
//
// An empty block, or an image nobody ever wrote to, appends nothing at all.
//
#ifndef _eduke32_defaults_h
#define _eduke32_defaults_h

// Build the final argument list: the baked arguments first, then whatever
// the block carries, minus the kernel's own switches. ppArgv must have room
// for nMax pointers, and the returned count never exceeds it. The token
// storage is static, so this is called once, from one core, before the game
// starts.
int DefaultsBuildArgv(const char **pBaked, unsigned nBaked,
                      const char **ppArgv, unsigned nMax);

// Set by --rapi-debug-uart in the block: arms serial key injection, which
// lets a console attached to the serial port type into the running game.
// A bench convenience; off unless asked for.
extern "C" int rapi_debug_uart;

#endif
