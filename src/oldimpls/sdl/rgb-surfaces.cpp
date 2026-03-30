#include "include.h"

// taken from: https://wiki.libsdl.org/SDL3/README-migration#sdl_surfaceh

static SDL_Surface *SDL_CreateRGBSurface(Uint32 flags, int width, int height, int depth, Uint32 Rmask, Uint32 Gmask, Uint32 Bmask, Uint32 Amask)
{
    return SDL_CreateSurface(width, height,
            SDL_GetPixelFormatForMasks(depth, Rmask, Gmask, Bmask, Amask));
}

static SDL_Surface *SDL_CreateRGBSurfaceWithFormat(Uint32 flags, int width, int height, int depth, Uint32 format)
{
    return SDL_CreateSurface(width, height, (SDL_PixelFormat)format);
}

static SDL_Surface *SDL_CreateRGBSurfaceFrom(void *pixels, int width, int height, int depth, int pitch, Uint32 Rmask, Uint32 Gmask, Uint32 Bmask, Uint32 Amask)
{
    return SDL_CreateSurfaceFrom(width, height,
                                 SDL_GetPixelFormatForMasks(depth, Rmask, Gmask, Bmask, Amask),
                                 pixels, pitch);
}

static SDL_Surface *SDL_CreateRGBSurfaceWithFormatFrom(void *pixels, int width, int height, int depth, int pitch, Uint32 format)
{
    return SDL_CreateSurfaceFrom(width, height, (SDL_PixelFormat)format, pixels, pitch);
}