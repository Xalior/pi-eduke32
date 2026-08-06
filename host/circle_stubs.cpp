//
// circle_stubs.cpp — the SDL2 surface EDuke32 uses and circle-libsdl2 does
// not implement.
//
// The shim draws from textures: a window, a renderer, streaming textures,
// present. EDuke32's classic renderer is older than that idea. It asks the
// window for a SURFACE, draws a frame's worth of pixels straight into it,
// and tells SDL to put the window on the screen. Neither call exists in the
// shim, so the two of them are the substance of this file — everything else
// here is small.
//
// Two functions REPLACE a library function instead of adding one:
// SDL_CreateRGBSurface and SDL_FreeSurface exist in the shim and refuse
// anything but 32 bits. They are reached through the linker's --wrap (see
// the WRAPPED_SDL list in the Makefile), so the library's own versions stay
// in place and still do the 32-bit work — this file only adds the paletted
// case on top and hands everything else straight back. Redefining them
// outright would be a duplicate symbol at best and a silent shadow at worst.
//
// Everything else is an addition. Each one either does the job properly or
// fails honestly — returns an error, returns null — so that nothing pretends
// to work. Where a function is a deliberate no-op it says why: on a
// bare-metal board with one fullscreen display and no window manager there
// is nothing for it to do.
//
// These are seams, not permanent furniture. When the shim implements one of
// these for real, the way to adopt it is to DELETE the stub here: the
// archive is linked whole, so a leftover stub becomes a duplicate-symbol
// error at link time rather than a silent winner over the real thing.
//
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <SDL2/SDL.h>
#include <SDL2/SDL_circle.h>

static const char From[] = "sdl-ext";

extern "C" {

// The library's own versions of the two wrapped functions.
SDL_Surface *__real_SDL_CreateRGBSurface(Uint32 flags, int width, int height,
                                         int depth, Uint32 Rmask, Uint32 Gmask,
                                         Uint32 Bmask, Uint32 Amask);
void __real_SDL_FreeSurface(SDL_Surface *surface);

// ---------------------------------------------------------------------------
// Surfaces this file owns
// ---------------------------------------------------------------------------
//
// A surface made here carries allocations the library knows nothing about —
// a palette, a heap pixel format, usually the pixels — so freeing it is this
// file's job too. Rather than guess from the surface's contents, every one
// made here is recorded, and the free path looks it up: found means ours and
// freed our way, not found means the library's and handed back to it.

struct OwnedSurface
{
    SDL_Surface     *surface;
    SDL_PixelFormat *format;      // always heap, always ours
    SDL_Palette     *palette;     // null for the direct-colour formats
    bool             owns_pixels; // false when the caller supplied them
    OwnedSurface    *next;
};

static OwnedSurface *s_owned = nullptr;

// Fill in a heap pixel format for one of the two layouts this port needs.
// depth 8 gets a palette; depth 32 is ARGB8888, which is byte-for-byte what
// the shim's streaming textures take.
static SDL_PixelFormat *MakeFormat(int depth)
{
    SDL_PixelFormat *fmt = (SDL_PixelFormat *)calloc(1, sizeof(SDL_PixelFormat));
    if (fmt == nullptr)
        return nullptr;

    fmt->BitsPerPixel  = (Uint8)depth;
    fmt->BytesPerPixel = (Uint8)(depth / 8);
    fmt->refcount      = 1;

    if (depth == 8)
    {
        fmt->format = SDL_PIXELFORMAT_INDEX8;
    }
    else
    {
        fmt->format = SDL_PIXELFORMAT_ARGB8888;
        fmt->Rmask  = 0x00FF0000;
        fmt->Gmask  = 0x0000FF00;
        fmt->Bmask  = 0x000000FF;
        fmt->Amask  = 0xFF000000;
        fmt->Rshift = 16;
        fmt->Gshift = 8;
        fmt->Bshift = 0;
        fmt->Ashift = 24;
    }
    return fmt;
}

static SDL_Palette *MakePalette(void)
{
    SDL_Palette *pal = (SDL_Palette *)calloc(1, sizeof(SDL_Palette));
    if (pal == nullptr)
        return nullptr;

    pal->colors = (SDL_Color *)calloc(256, sizeof(SDL_Color));
    if (pal->colors == nullptr)
    {
        free(pal);
        return nullptr;
    }
    pal->ncolors  = 256;
    pal->refcount = 1;
    // Opaque black until the game sets a real palette. A surface whose
    // palette had zero alpha throughout would convert to a fully transparent
    // picture and read as "the game drew nothing".
    for (int i = 0; i < 256; i++)
        pal->colors[i].a = 0xFF;
    return pal;
}

static SDL_Surface *NewOwnedSurface(int width, int height, int depth,
                                    void *pixels, int pitch)
{
    if (width <= 0 || height <= 0 || (depth != 8 && depth != 32))
    {
        SDL_SetError("unsupported surface: %dx%d at %d bits", width, height, depth);
        return nullptr;
    }

    const bool prealloc = (pixels != nullptr);

    SDL_Surface *surface = (SDL_Surface *)calloc(1, sizeof(SDL_Surface));
    OwnedSurface *rec    = (OwnedSurface *)calloc(1, sizeof(OwnedSurface));
    SDL_PixelFormat *fmt = MakeFormat(depth);
    SDL_Palette *pal     = (depth == 8) ? MakePalette() : nullptr;

    if (surface == nullptr || rec == nullptr || fmt == nullptr
        || (depth == 8 && pal == nullptr))
    {
        if (pal != nullptr) { free(pal->colors); free(pal); }
        free(fmt);
        free(rec);
        free(surface);
        SDL_SetError("out of memory allocating surface");
        return nullptr;
    }

    fmt->palette = pal;

    if (pitch == 0)
        pitch = width * fmt->BytesPerPixel;

    if (!prealloc)
    {
        pixels = calloc(1, (size_t)pitch * height);
        if (pixels == nullptr)
        {
            if (pal != nullptr) { free(pal->colors); free(pal); }
            free(fmt);
            free(rec);
            free(surface);
            SDL_SetError("out of memory allocating surface pixels");
            return nullptr;
        }
    }

    surface->flags     = prealloc ? SDL_PREALLOC : 0;
    surface->format    = fmt;
    surface->w         = width;
    surface->h         = height;
    surface->pitch     = pitch;
    surface->pixels    = pixels;
    surface->clip_rect = { 0, 0, width, height };
    surface->refcount  = 1;

    rec->surface     = surface;
    rec->format      = fmt;
    rec->palette     = pal;
    rec->owns_pixels = !prealloc;
    rec->next        = s_owned;
    s_owned          = rec;

    return surface;
}

// The library makes 32-bit surfaces; this adds the paletted ones the Build
// engine's frame buffer needs and leaves everything else to the library.
SDL_Surface *__wrap_SDL_CreateRGBSurface(Uint32 flags, int width, int height,
                                         int depth, Uint32 Rmask, Uint32 Gmask,
                                         Uint32 Bmask, Uint32 Amask)
{
    if (depth == 8)
        return NewOwnedSurface(width, height, 8, nullptr, 0);

    return __real_SDL_CreateRGBSurface(flags, width, height, depth,
                                       Rmask, Gmask, Bmask, Amask);
}

void __wrap_SDL_FreeSurface(SDL_Surface *surface)
{
    if (surface == nullptr)
        return;

    OwnedSurface **link = &s_owned;
    for (OwnedSurface *o = s_owned; o != nullptr; link = &o->next, o = o->next)
    {
        if (o->surface != surface)
            continue;

        if (--surface->refcount > 0)
            return;

        *link = o->next;
        if (o->palette != nullptr)
        {
            free(o->palette->colors);
            free(o->palette);
        }
        if (o->owns_pixels)
            free(surface->pixels);
        free(o->format);
        free(o);
        free(surface);
        return;
    }

    __real_SDL_FreeSurface(surface);
}

SDL_Surface *SDL_CreateRGBSurfaceFrom(void *pixels, int width, int height,
                                      int depth, int pitch, Uint32 Rmask,
                                      Uint32 Gmask, Uint32 Bmask, Uint32 Amask)
{
    (void)Rmask; (void)Gmask; (void)Bmask; (void)Amask;
    return NewOwnedSurface(width, height, depth, pixels,
                           pitch != 0 ? pitch : width * (depth / 8));
}

int SDL_SetPaletteColors(SDL_Palette *palette, const SDL_Color *colors,
                         int firstcolor, int ncolors)
{
    if (palette == nullptr || colors == nullptr)
    {
        SDL_SetError("SDL_SetPaletteColors: no palette");
        return -1;
    }
    if (firstcolor < 0 || ncolors < 0 || firstcolor + ncolors > palette->ncolors)
    {
        SDL_SetError("SDL_SetPaletteColors: range outside the palette");
        return -1;
    }

    for (int i = 0; i < ncolors; i++)
    {
        SDL_Color c = colors[i];
        // Callers fill only r, g and b. A zero alpha here would convert the
        // whole picture to transparent.
        c.a = 0xFF;
        palette->colors[firstcolor + i] = c;
    }
    palette->version++;
    return 0;
}

// Nothing here is RLE encoded or hardware backed, so a lock is a formality.
int SDL_LockSurface(SDL_Surface *) { return 0; }
void SDL_UnlockSurface(SDL_Surface *) {}

// ---------------------------------------------------------------------------
// THE WINDOW'S SURFACE — how a frame reaches the screen
// ---------------------------------------------------------------------------
//
// This is the whole of this port's video output, and it is the one thing the
// shim does not provide in any form.
//
// EDuke32's classic renderer draws into an 8-bit buffer of its own, converts
// it through the palette, and writes the result into the pixels of the
// window's surface. Then it calls SDL_UpdateWindowSurface and expects the
// picture to appear. That is SDL's oldest way of putting a frame on screen,
// and the shim has only the newer one: a renderer and a streaming texture.
//
// So the surface here is real memory that the game writes into, and updating
// the window copies it into a texture and presents that. One extra copy per
// frame, which is the price of bridging the two ideas, and it happens on the
// core the game already owns.
//
// The surface's size is the window's, which is the virtual display the
// kernel declared before the game started. It is created once and kept: the
// game asks for it again after any update that fails, and handing back a
// fresh surface each time would throw away the frame just drawn into it.

static SDL_Surface  *s_WindowSurface  = nullptr;
static SDL_Renderer *s_WindowRenderer = nullptr;
static SDL_Texture  *s_WindowTexture  = nullptr;

SDL_Surface *SDL_GetWindowSurface(SDL_Window *window)
{
    if (window == nullptr)
    {
        SDL_SetError("SDL_GetWindowSurface: no window");
        return nullptr;
    }
    if (s_WindowSurface != nullptr)
        return s_WindowSurface;

    int w = 0, h = 0;
    SDL_GetWindowSize(window, &w, &h);
    if (w <= 0 || h <= 0)
    {
        SDL_SetError("SDL_GetWindowSurface: the window has no size");
        return nullptr;
    }

    s_WindowRenderer = SDL_CreateRenderer(window, -1, 0);
    if (s_WindowRenderer == nullptr)
        return nullptr;

    s_WindowTexture = SDL_CreateTexture(s_WindowRenderer,
                                        SDL_PIXELFORMAT_ARGB8888,
                                        SDL_TEXTUREACCESS_STREAMING, w, h);
    if (s_WindowTexture == nullptr)
    {
        SDL_DestroyRenderer(s_WindowRenderer);
        s_WindowRenderer = nullptr;
        return nullptr;
    }

    s_WindowSurface = NewOwnedSurface(w, h, 32, nullptr, 0);
    if (s_WindowSurface == nullptr)
    {
        SDL_DestroyTexture(s_WindowTexture);
        SDL_DestroyRenderer(s_WindowRenderer);
        s_WindowTexture  = nullptr;
        s_WindowRenderer = nullptr;
        return nullptr;
    }

    SDL2Circle_Log(From, SDL2CIRCLE_LOG_NOTICE,
                   "window surface: %dx%d at 32bpp, presented through a streaming texture",
                   w, h);
    return s_WindowSurface;
}

int SDL_UpdateWindowSurface(SDL_Window *window)
{
    if (window == nullptr || s_WindowSurface == nullptr
        || s_WindowTexture == nullptr || s_WindowRenderer == nullptr)
    {
        SDL_SetError("SDL_UpdateWindowSurface: no surface to present");
        return -1;
    }

    void *pixels = nullptr;
    int   pitch  = 0;
    if (SDL_LockTexture(s_WindowTexture, nullptr, &pixels, &pitch) != 0)
        return -1;

    // Row by row rather than one memcpy: the texture's pitch is whatever the
    // shim chose and need not match the surface's.
    const int rowbytes = s_WindowSurface->w * 4;
    for (int y = 0; y < s_WindowSurface->h; y++)
    {
        memcpy((Uint8 *)pixels + (size_t)y * pitch,
               (const Uint8 *)s_WindowSurface->pixels
                   + (size_t)y * s_WindowSurface->pitch,
               (size_t)rowbytes);
    }
    SDL_UnlockTexture(s_WindowTexture);

    SDL_RenderCopy(s_WindowRenderer, s_WindowTexture, nullptr, nullptr);
    SDL_RenderPresent(s_WindowRenderer);
    return 0;
}

// The whole frame is presented every time. Tracking damaged rectangles would
// save nothing: the texture upload and the present below it are both
// whole-surface operations in the shim.
int SDL_UpdateWindowSurfaceRects(SDL_Window *window, const SDL_Rect *, int)
{
    return SDL_UpdateWindowSurface(window);
}

// ---------------------------------------------------------------------------
// Window calls with nothing to do on this board
// ---------------------------------------------------------------------------
//
// The display is one fullscreen panel that the host kernel declared before
// the game started. Its size, its position, its border and its scaling are
// settled before any of these can be called, so each answers success and
// changes nothing — which is the truth, not a pretence.

int SDL_SetWindowFullscreen(SDL_Window *, Uint32) { return 0; }
void SDL_SetWindowSize(SDL_Window *, int, int) {}
void SDL_SetWindowMinimumSize(SDL_Window *, int, int) {}
void SDL_SetWindowPosition(SDL_Window *, int, int) {}
void SDL_SetWindowBordered(SDL_Window *, SDL_bool) {}
void SDL_SetWindowResizable(SDL_Window *, SDL_bool) {}
void SDL_SetWindowIcon(SDL_Window *, SDL_Surface *) {}
int SDL_SetWindowDisplayMode(SDL_Window *, const SDL_DisplayMode *) { return 0; }
int SDL_RenderSetLogicalSize(SDL_Renderer *, int, int) { return 0; }
int SDL_RenderSetIntegerScale(SDL_Renderer *, SDL_bool) { return 0; }

// There is no window position to report: the display is the whole panel and
// its origin is where it has always been.
void SDL_GetWindowPosition(SDL_Window *, int *x, int *y)
{
    if (x != nullptr) *x = 0;
    if (y != nullptr) *y = 0;
}

// There is one pointer and one window, so the pointer is always confined to
// it whether anyone asked or not. The shim's relative mouse mode is what
// actually matters for look-around, and it is the shim's own.
static SDL_bool s_WindowGrab = SDL_FALSE;
void SDL_SetWindowGrab(SDL_Window *, SDL_bool grabbed) { s_WindowGrab = grabbed; }
SDL_bool SDL_GetWindowGrab(SDL_Window *) { return s_WindowGrab; }

// Gamma is a property of the display pipeline, and the shim's does not have
// one to set. Failing rather than succeeding is what stops the game from
// believing a brightness setting took effect.
int SDL_SetWindowGammaRamp(SDL_Window *, const Uint16 *, const Uint16 *,
                           const Uint16 *)
{
    SDL_SetError("gamma ramps are not implemented");
    return -1;
}

int SDL_GetWindowGammaRamp(SDL_Window *, Uint16 *, Uint16 *, Uint16 *)
{
    SDL_SetError("gamma ramps are not implemented");
    return -1;
}

// There is exactly one mode — the virtual display — so it is the closest
// match to anything asked for.
SDL_DisplayMode *SDL_GetClosestDisplayMode(int displayIndex,
                                           const SDL_DisplayMode *,
                                           SDL_DisplayMode *closest)
{
    if (closest == nullptr)
        return nullptr;
    if (SDL_GetCurrentDisplayMode(displayIndex, closest) != 0)
        return nullptr;
    return closest;
}

// Render-to-texture: the shim's renderer draws to the screen and nowhere
// else. Restoring the default target succeeds because that is where it
// already is; asking for any other target fails, which is what stops a
// caller believing the picture went somewhere it did not.
int SDL_SetRenderTarget(SDL_Renderer *, SDL_Texture *texture)
{
    if (texture == nullptr)
        return 0;
    SDL_SetError("render targets are not implemented");
    return -1;
}

// ---------------------------------------------------------------------------
// Events
// ---------------------------------------------------------------------------

// The shim's queue has no peek. Answering "nothing queued" is the same
// answer an empty queue would give, and every caller of this already handles
// that case because it is the common one.
int SDL_PeepEvents(SDL_Event *, int, SDL_eventaction, Uint32, Uint32)
{
    return 0;
}

// Polling and yielding keeps the servo on core 0 running, which is what
// delivers the event being waited for. A null event pointer means the caller
// only wants to be woken, so the event is fetched and discarded.
int SDL_WaitEvent(SDL_Event *event)
{
    SDL_Event scratch;
    for (;;)
    {
        if (SDL_PollEvent(event != nullptr ? event : &scratch))
            return 1;
        SDL_Delay(5);
    }
}

int SDL_WaitEventTimeout(SDL_Event *event, int timeout)
{
    SDL_Event scratch;
    const Uint32 deadline = SDL_GetTicks() + (Uint32)(timeout > 0 ? timeout : 0);
    for (;;)
    {
        if (SDL_PollEvent(event != nullptr ? event : &scratch))
            return 1;
        if (timeout <= 0 || SDL_GetTicks() >= deadline)
            return 0;
        SDL_Delay(1);
    }
}

// An event filter runs inside SDL's own event delivery, which is the shim's,
// and there is no hook in it. Recorded so that reading it back gives what
// was set, and never called — which is visible from here rather than from a
// filter that mysteriously never fires.
static SDL_EventFilter s_EventFilter     = nullptr;
static void           *s_EventFilterData = nullptr;

void SDL_SetEventFilter(SDL_EventFilter filter, void *userdata)
{
    s_EventFilter     = filter;
    s_EventFilterData = userdata;
    if (filter != nullptr)
        SDL2Circle_Log(From, SDL2CIRCLE_LOG_WARNING,
                       "SDL_SetEventFilter: filters are not called by this implementation");
}

SDL_bool SDL_GetEventFilter(SDL_EventFilter *filter, void **userdata)
{
    if (filter != nullptr)   *filter   = s_EventFilter;
    if (userdata != nullptr) *userdata = s_EventFilterData;
    return s_EventFilter != nullptr ? SDL_TRUE : SDL_FALSE;
}

// ---------------------------------------------------------------------------
// Spin locks and mutexes
// ---------------------------------------------------------------------------
//
// The Build engine guards its sound mixer's state with these, and reaches
// them whether or not a second core ever contends. On this board the sound
// callback and the game genuinely do run on different cores, so these are
// real: a test-and-set over one word, which costs almost nothing and is
// correct when it matters.

void SDL_AtomicLock(SDL_SpinLock *lock)
{
    if (lock == nullptr)
        return;
    while (__atomic_exchange_n(lock, 1, __ATOMIC_ACQUIRE) != 0)
        asm volatile("yield" ::: "memory");
}

SDL_bool SDL_AtomicTryLock(SDL_SpinLock *lock)
{
    if (lock == nullptr)
        return SDL_FALSE;
    return __atomic_exchange_n(lock, 1, __ATOMIC_ACQUIRE) == 0
           ? SDL_TRUE : SDL_FALSE;
}

void SDL_AtomicUnlock(SDL_SpinLock *lock)
{
    if (lock == nullptr)
        return;
    __atomic_store_n(lock, 0, __ATOMIC_RELEASE);
}

struct SDL_mutex { volatile int held; };

SDL_mutex *SDL_CreateMutex(void)
{
    SDL_mutex *m = (SDL_mutex *)calloc(1, sizeof(SDL_mutex));
    if (m == nullptr)
        SDL_SetError("out of memory allocating mutex");
    return m;
}

void SDL_DestroyMutex(SDL_mutex *mutex) { free(mutex); }

int SDL_LockMutex(SDL_mutex *mutex)
{
    if (mutex == nullptr)
        return -1;
    while (__atomic_exchange_n(&mutex->held, 1, __ATOMIC_ACQUIRE) != 0)
        asm volatile("yield" ::: "memory");
    return 0;
}

int SDL_UnlockMutex(SDL_mutex *mutex)
{
    if (mutex == nullptr)
        return -1;
    __atomic_store_n(&mutex->held, 0, __ATOMIC_RELEASE);
    return 0;
}

// ---------------------------------------------------------------------------
// Odds and ends
// ---------------------------------------------------------------------------

// Where the game keeps its configuration files and its save games. On a
// desktop this is a per-user directory; here it is the card directory the
// game was started from, which is also where its GRP lives. The caller frees
// what it gets, so this hands back a fresh copy every time.
char *SDL_GetPrefPath(const char *, const char *)
{
    static const char path[] = RAPI_GAME_DIR "/";
    char *copy = (char *)SDL_malloc(sizeof(path));
    if (copy != nullptr)
        memcpy(copy, path, sizeof(path));
    return copy;
}

// The same directory: the program and its data are in one place here.
char *SDL_GetBasePath(void)
{
    static const char path[] = RAPI_GAME_DIR "/";
    char *copy = (char *)SDL_malloc(sizeof(path));
    if (copy != nullptr)
        memcpy(copy, path, sizeof(path));
    return copy;
}

// The environment is the C library's, and SDL's wrappers around it are
// nothing more than that on any platform.
char *SDL_getenv(const char *name)
{
    return getenv(name);
}

int SDL_setenv(const char *name, const char *value, int overwrite)
{
    return setenv(name, value, overwrite);
}

// The board has no window manager to put a dialog on top of, so the message
// goes where every other diagnostic goes: the serial console.
int SDL_ShowSimpleMessageBox(Uint32, const char *title, const char *message,
                             SDL_Window *)
{
    printf("%s: %s\n", title != nullptr ? title : "message",
           message != nullptr ? message : "");
    return 0;
}

// The same, for the form that offers buttons. Nobody can press one, so the
// caller is told no button was chosen and left to take its own default.
int SDL_ShowMessageBox(const SDL_MessageBoxData *messageboxdata, int *buttonid)
{
    if (messageboxdata != nullptr)
        printf("%s: %s\n",
               messageboxdata->title != nullptr ? messageboxdata->title : "message",
               messageboxdata->message != nullptr ? messageboxdata->message : "");
    if (buttonid != nullptr)
        *buttonid = -1;
    return 0;
}

// Names for the keys the game prints in its control menus. SDL builds these
// from a table this port does not carry; the printable keys name themselves
// and everything else is reported honestly as unnamed.
const char *SDL_GetKeyName(SDL_Keycode key)
{
    static char name[2];
    if (key >= ' ' && key < 0x7F)
    {
        name[0] = (char)key;
        name[1] = '\0';
        return name;
    }
    return "";
}

const char *SDL_GetScancodeName(SDL_Scancode)
{
    return "";
}

// There is no clipboard on this board and nothing to paste from.
SDL_bool SDL_HasClipboardText(void) { return SDL_FALSE; }

char *SDL_GetClipboardText(void)
{
    char *empty = (char *)SDL_malloc(1);
    if (empty != nullptr)
        empty[0] = '\0';
    return empty;
}

int SDL_SetClipboardText(const char *) { return 0; }

// The board runs from a power supply, not a battery. SDL_POWERSTATE_NO_BATTERY
// is exactly that answer, and the two "unknown" outputs are what SDL itself
// reports for a machine with no battery to measure.
SDL_PowerState SDL_GetPowerInfo(int *secs, int *pct)
{
    if (secs != nullptr) *secs = -1;
    if (pct != nullptr)  *pct  = -1;
    return SDL_POWERSTATE_NO_BATTERY;
}

// One audio backend, named for what it is.
int SDL_GetNumAudioDrivers(void) { return 1; }

const char *SDL_GetAudioDriver(int index)
{
    return index == 0 ? "circle" : nullptr;
}

// SDL's allocator is the C library's here, and the C library's is Circle's.
// Replacing it would put the game's allocator underneath the shim's own
// allocations, which are made on a different core.
int SDL_SetMemoryFunctions(SDL_malloc_func, SDL_calloc_func, SDL_realloc_func,
                           SDL_free_func)
{
    SDL_SetError("the memory allocator is not replaceable here");
    return -1;
}

void SDL_GetMemoryFunctions(SDL_malloc_func *m, SDL_calloc_func *c,
                            SDL_realloc_func *r, SDL_free_func *f)
{
    if (m != nullptr) *m = SDL_malloc;
    if (c != nullptr) *c = SDL_calloc;
    if (r != nullptr) *r = SDL_realloc;
    if (f != nullptr) *f = SDL_free;
}

// Every core here has one thing to do and does it at one priority. There is
// nothing to raise or lower.
int SDL_SetThreadPriority(SDL_ThreadPriority) { return 0; }

} // extern "C"
