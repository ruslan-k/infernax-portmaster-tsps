/* Terminator 2D 1.0.7: SDL APIs newer than the Framework V6 universal floor (SDL 2.0.4) are reached
 * through the pinned nxcompat optional-SDL resolver (framework/nxcompat/src/nxcompat_sdl_optional.c),
 * never by direct import (nxrelease validate_symbol_floor / nxabi sdl-floor). The resolver accepts a
 * symbol only from the SDL module the process already loaded. On SDL >= 2.0.18 every call below is the
 * same SDL function as before; an older SDL starts the game instead of failing in the dynamic linker. */
#ifndef T2_SDL_OPTIONAL_H
#define T2_SDL_OPTIONAL_H
#include <SDL2/SDL.h>
#include <dlfcn.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "nxcompat_sdl_optional.h"

static struct {
    SDL_JoystickID (*device_instance)(int);                          /* SDL 2.0.6 */
    int (*rumble)(SDL_GameController *, Uint16, Uint16, Uint32);     /* SDL 2.0.9 */
    SDL_bool (*has_rumble)(SDL_GameController *);                    /* SDL 2.0.18 */
} t2_sdl_optional;
static pthread_once_t t2_sdl_optional_once = PTHREAD_ONCE_INIT;

static void *t2_sdl_optional_lookup(const nxcompat_sdl_optional_options *options, const char *name) {
    void *address = NULL;
    if (nxcompat_sdl_optional_symbol(options, name, &address) != NXCOMPAT_OK) return NULL;
    return address;
}

/* Caller-supplied lookup (nxcompat_sdl_optional_resolver): the address `name` has in the SDL module this
 * process already loaded. MEASURED on the dArkOS G31 (27/09, 1.0.7 proof run): the default anchor lookup
 * returned NULL for all three symbols, because this executable is not position independent and the
 * address of an imported function (the anchor) is its canonical PLT entry inside the executable, so the
 * anchor module never equals libSDL2. RTLD_NOLOAD returns the already loaded SONAME only; it never loads,
 * searches or selects another SDL. */
static void *t2_sdl_optional_loaded_lookup(void *userdata, const char *name) {
    return userdata ? dlsym(userdata, name) : NULL;
}

static void t2_sdl_optional_resolve(void) {
    nxcompat_sdl_optional_options options;
    memset(&options, 0, sizeof options);
    options.api_version = NXCOMPAT_API_VERSION_V2;
    options.struct_size = sizeof options;
    options.provider_anchor = (const void *)(uintptr_t)SDL_NumJoysticks; /* baseline SDL import of this ELF */
    void *loaded = dlopen("libSDL2-2.0.so.0", RTLD_LAZY | RTLD_NOLOAD);
    if (loaded) {
        options.resolver = t2_sdl_optional_loaded_lookup;
        options.resolver_userdata = loaded;
    }
    void *p = t2_sdl_optional_lookup(&options, "SDL_JoystickGetDeviceInstanceID");
    memcpy(&t2_sdl_optional.device_instance, &p, sizeof p);
    p = t2_sdl_optional_lookup(&options, "SDL_GameControllerRumble");
    memcpy(&t2_sdl_optional.rumble, &p, sizeof p);
    p = t2_sdl_optional_lookup(&options, "SDL_GameControllerHasRumble");
    memcpy(&t2_sdl_optional.has_rumble, &p, sizeof p);
    printf("T2_SDL_OPTIONAL device-instance=%d rumble=%d has-rumble=%d resolver=nxcompat lookup=%s\n",
           t2_sdl_optional.device_instance != NULL, t2_sdl_optional.rumble != NULL,
           t2_sdl_optional.has_rumble != NULL, loaded ? "loaded-soname" : "anchor");
}

static void t2_sdl_optional_init(void) { pthread_once(&t2_sdl_optional_once, t2_sdl_optional_resolve); }

/* SDL_JoystickGetDeviceInstanceID; before SDL 2.0.6 the instance is read from a (reference-counted)
 * open of the same index. */
static SDL_JoystickID t2_sdl_device_instance(int index) {
    t2_sdl_optional_init();
    if (t2_sdl_optional.device_instance) return t2_sdl_optional.device_instance(index);
    SDL_Joystick *joystick = SDL_JoystickOpen(index);
    if (!joystick) return -1;
    SDL_JoystickID instance = SDL_JoystickInstanceID(joystick);
    SDL_JoystickClose(joystick);
    return instance;
}

/* SDL_GameControllerRumble; -1 (no rumble) when the loaded SDL predates it. */
static int t2_sdl_rumble(SDL_GameController *pad, Uint16 low, Uint16 high, Uint32 ms) {
    t2_sdl_optional_init();
    return pad && t2_sdl_optional.rumble ? t2_sdl_optional.rumble(pad, low, high, ms) : -1;
}

/* SDL_GameControllerHasRumble; between SDL 2.0.9 and 2.0.17 a pad is treated as rumble capable when the
 * rumble call exists (the call itself reports failure harmlessly). */
static int t2_sdl_has_rumble(SDL_GameController *pad) {
    t2_sdl_optional_init();
    if (!pad) return 0;
    if (t2_sdl_optional.has_rumble) return t2_sdl_optional.has_rumble(pad) == SDL_TRUE;
    return t2_sdl_optional.rumble != NULL;
}
#endif
