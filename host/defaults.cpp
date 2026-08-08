//
// defaults.cpp — carrying and consuming the patchable-defaults block.
//
// Three jobs, all against the shared interface in defaultsblock.h:
//
//  1. CARRY the block. The image's one copy lives in its own section, which
//     eduke32-defaults.ld pins to image offset 0x800. It ships with the
//     magic, the capacity, and an empty string, so an image nobody has
//     written to boots through exactly the same code as one that has been.
//
//  2. OPEN the image. The first four bytes of the image are a branch over
//     the reserved space and the block, to Circle's own startup. Without it
//     the processor would begin executing the block.
//
//  3. CONSUME the text. Check the magic first, split the text into
//     arguments, act on the kernel's own switches and remove them, and pass
//     the rest to the game.
//
// The magic check is a seatbelt, and it is deliberately made at the fixed
// offset rather than through the symbol: if a future link ever moved the
// block, the magic would not be where it belongs, and the block is then
// ignored rather than trusted. A misplaced block must read as absent, never
// as whatever bytes happen to be sitting there.
//
#include "defaults.h"
#include "defaultsblock.h"
#include <diskcache.h>

#include <SDL2/SDL_circle.h>
#include <circle/memorymap.h>
#include <cstring>

static const char From[] = "defaults";

// ---------------------------------------------------------------------------
// 2. The trampoline: the image's first four bytes.
//
// `b _start` is PC-relative and reaches far further than it needs to, so the
// entry stays one instruction wherever Circle's startup ends up.
// ---------------------------------------------------------------------------
// Both the trampoline and the block's placement are written in the object
// format the kernel is built in. A host build of this file for testing the
// reader below uses a different one, which has no way to express either —
// and does not need to, because the reader finds the block at its address
// and never through the symbol.
#ifdef __ELF__
__asm__ (
    "\t.section .eduke32.entry, \"ax\", %progbits\n"
    "\t.globl _eduke32_entry\n"
    "_eduke32_entry:\n"
    "\tb _start\n"
    "\t.previous\n"
);
#define DEFAULTS_BLOCK_PLACEMENT \
    __attribute__ ((section (".eduke32.defaults"), used, aligned (8)))
#else
#define DEFAULTS_BLOCK_PLACEMENT __attribute__ ((used, aligned (8)))
#endif

// ---------------------------------------------------------------------------
// 1. The block itself. `used` and the linker script's KEEP() stop it being
// discarded for having no callers; the script's ASSERTs refuse any link that
// puts it anywhere but 0x800.
// ---------------------------------------------------------------------------
extern "C"
{

DEFAULTS_BLOCK_PLACEMENT
TDefaultsBlock _eduke32_defaults =
{
    {DEFAULTS_MAGIC0, DEFAULTS_MAGIC1, DEFAULTS_MAGIC2, DEFAULTS_MAGIC3},
    DEFAULTS_BUFFER_BYTES,
    0,          // Length: empty
    {0}         // Text: nothing to append
};

int rapi_debug_uart = 0;

unsigned rapi_cache_kb = DISKCACHE_DEFAULT_KB;

unsigned rapi_cache_readahead_kb = DISKCACHE_DEFAULT_READAHEAD_KB;

}

// A size in kilobytes, which is what both disk-cache switches take. Digits
// alone are kilobytes; a trailing K or M says so outright, because the
// difference between 4096 and 4096M is a board that will not boot. Anything
// else is not a size, and the caller treats it as an unrecognised switch
// rather than acting on a number nobody wrote.
static bool ParseSizeKB(const char *pValue, unsigned *pKB)
{
    unsigned nSize = 0;
    unsigned nScale = 1;
    const char *p = pValue;

    for (; *p >= '0' && *p <= '9'; p++)
        nSize = nSize * 10 + (unsigned)(*p - '0');

    if (p == pValue)
        return false;               // no digits at all

    if (*p == 'M' || *p == 'm')
        { nScale = 1024; p++; }
    else if (*p == 'K' || *p == 'k')
        p++;

    if (*p != '\0')
        return false;               // trailing rubbish

    *pKB = nSize * nScale;
    return true;
}

// Every argument starting `--rapi-` is the kernel's, and every one of them is
// removed here whether it is recognised or not: a mistyped kernel switch must
// not reach the game, which would treat it as a filename or reject it.
static void DispatchKernelSwitch(const char *pSwitch)
{
    if (strcmp(pSwitch, "--rapi-debug-uart") == 0)
    {
        rapi_debug_uart = 1;
        SDL2Circle_Log(From, SDL2CIRCLE_LOG_NOTICE,
                       "--rapi-debug-uart consumed: serial key injection on");
    }
    else if (strncmp(pSwitch, "--rapi-perf=", 12) == 0)
    {
        // Seconds between performance reports. Nothing but digits is an
        // answer: anything else falls through to the unrecognised branch
        // rather than arming an interval nobody asked for.
        const char *pValue = pSwitch + 12;
        unsigned nSeconds = 0;
        bool bDigits = *pValue != '\0';
        for (const char *p = pValue; *p != '\0'; p++)
        {
            if (*p < '0' || *p > '9')
            {
                bDigits = false;
                break;
            }
            nSeconds = nSeconds * 10 + (unsigned)(*p - '0');
        }

        if (bDigits)
        {
            SDL2Circle_SetPerfInterval(nSeconds);
            SDL2Circle_Log(From, SDL2CIRCLE_LOG_NOTICE,
                           "--rapi-perf consumed: performance reports every %u s",
                           nSeconds);
        }
        else
        {
            SDL2Circle_Log(From, SDL2CIRCLE_LOG_WARNING,
                           "unrecognised kernel switch \"%s\" ignored", pSwitch);
        }
    }
    else if (strncmp(pSwitch, "--rapi-cache-size=", 18) == 0)
    {
        unsigned nKB = 0;
        if (ParseSizeKB(pSwitch + 18, &nKB))
        {
            rapi_cache_kb = nKB;
            SDL2Circle_Log(From, SDL2CIRCLE_LOG_NOTICE,
                           "--rapi-cache-size consumed: disk cache pool %u KB",
                           rapi_cache_kb);
        }
        else
        {
            SDL2Circle_Log(From, SDL2CIRCLE_LOG_WARNING,
                           "unrecognised kernel switch \"%s\" ignored", pSwitch);
        }
    }
    else if (strncmp(pSwitch, "--rapi-cache-readahead=", 23) == 0)
    {
        unsigned nKB = 0;
        if (ParseSizeKB(pSwitch + 23, &nKB))
        {
            rapi_cache_readahead_kb = nKB;
            SDL2Circle_Log(From, SDL2CIRCLE_LOG_NOTICE,
                           "--rapi-cache-readahead consumed: read-ahead %u KB",
                           rapi_cache_readahead_kb);
        }
        else
        {
            SDL2Circle_Log(From, SDL2CIRCLE_LOG_WARNING,
                           "unrecognised kernel switch \"%s\" ignored", pSwitch);
        }
    }
    else
    {
        SDL2Circle_Log(From, SDL2CIRCLE_LOG_WARNING,
                       "unrecognised kernel switch \"%s\" ignored", pSwitch);
    }
}

// The text is copied out before it is split, because splitting writes into
// it. The block itself stays as the writer left it, which matters when the
// bench is trying to work out what was actually stamped.
static char s_TokenText[DEFAULTS_BUFFER_BYTES];

int DefaultsBuildArgv(const char **pBaked, unsigned nBaked,
                      const char **ppArgv, unsigned nMax)
{
    unsigned nArgc = 0;
    for (unsigned i = 0; i < nBaked && nArgc < nMax - 1; i++)
        ppArgv[nArgc++] = pBaked[i];

    const TDefaultsBlock *pBlock =
        (const TDefaultsBlock *)(MEM_KERNEL_START + DEFAULTS_BLOCK_OFFSET);
    if (   pBlock->Magic[0] != DEFAULTS_MAGIC0
        || pBlock->Magic[1] != DEFAULTS_MAGIC1
        || pBlock->Magic[2] != DEFAULTS_MAGIC2
        || pBlock->Magic[3] != DEFAULTS_MAGIC3)
    {
        SDL2Circle_Log(From, SDL2CIRCLE_LOG_WARNING,
                       "no block magic at 0x%lX — nothing appended",
                       (unsigned long)(MEM_KERNEL_START + DEFAULTS_BLOCK_OFFSET));
        ppArgv[nArgc] = nullptr;
        return nArgc;
    }

    // Bounded by the block's own capacity, never past this build's buffer,
    // and terminated whatever the writer claimed: Length is the writer's
    // convenience, not something to trust.
    unsigned nBound = pBlock->Capacity;
    if (nBound > DEFAULTS_BUFFER_BYTES)
        nBound = DEFAULTS_BUFFER_BYTES;
    memcpy(s_TokenText, pBlock->Text, nBound);
    s_TokenText[nBound - 1] = '\0';

    if (s_TokenText[0] == '\0')
    {
        SDL2Circle_Log(From, SDL2CIRCLE_LOG_NOTICE,
                       "defaults block empty — nothing appended");
        ppArgv[nArgc] = nullptr;
        return nArgc;
    }

    SDL2Circle_Log(From, SDL2CIRCLE_LOG_NOTICE, "injected: \"%s\"", s_TokenText);

    // Split on whitespace, in place. Double quotes group whitespace into one
    // argument and are removed from it, which is the only way to express an
    // argument containing a space, or an empty one. Quotes only ever shorten
    // an argument, so the write position never overtakes the read position
    // and one buffer is enough.
    unsigned nInjected = 0;
    unsigned nConsumed = 0;
    char *p = s_TokenText;
    while (*p != '\0')
    {
        while (*p == ' ' || *p == '\t')
            p++;
        if (*p == '\0')
            break;

        char *pToken = p;       // where the argument starts
        char *pWrite = p;       // where the next kept character goes
        while (*p != '\0' && *p != ' ' && *p != '\t')
        {
            if (*p == '"')
            {
                p++;
                while (*p != '\0' && *p != '"')
                    *pWrite++ = *p++;
                if (*p == '"')
                    p++;
                continue;
            }
            *pWrite++ = *p++;
        }
        if (*p != '\0')
            p++;                // step past the separator
        *pWrite = '\0';

        if (strncmp(pToken, "--rapi-", 7) == 0)
        {
            DispatchKernelSwitch(pToken);
            nConsumed++;
        }
        else if (nArgc < nMax - 1)
        {
            ppArgv[nArgc++] = pToken;
            nInjected++;
        }
        else
        {
            SDL2Circle_Log(From, SDL2CIRCLE_LOG_ERROR,
                           "argument list full — \"%s\" dropped", pToken);
        }
    }

    SDL2Circle_Log(From, SDL2CIRCLE_LOG_NOTICE,
                   "%u argument(s) appended, %u kernel switch(es) consumed",
                   nInjected, nConsumed);

    ppArgv[nArgc] = nullptr;
    return nArgc;
}
