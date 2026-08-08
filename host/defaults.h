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

// Set by --rapi-cache-size=<size> in the block: the disk cache's pool, in
// kilobytes, with 0 meaning no pool at all. It starts at the cache's own
// provisional default, so a boot that says nothing still gets one.
//
// The point of putting it in the block is that the right size differs from
// one game to the next and is settled by measurement: the same image can be
// run at several sizes, one boot each, with nothing rebuilt in between.
extern "C" unsigned rapi_cache_kb;

// Set by --rapi-cache-readahead=<size>: how much the cache fetches ahead when
// a read carries on from the previous one, in kilobytes, with 0 meaning it
// fetches only what was asked for.
//
// Separate from the pool because it answers a different problem and is worth
// measuring on its own. A pool helps data asked for twice; read-ahead helps a
// file streamed through once, which no pool can. The depth is a trade in both
// directions — too shallow still pays the card's per-request toll, too deep
// fetches sectors nobody wants — so it is measured, not assumed. The cache
// holds it in proportion to the pool whatever is asked for here.
extern "C" unsigned rapi_cache_readahead_kb;

#endif
