/* GPU time of each image (only with --frame-timing): Direct3D 11 timestamps (the SDL renderer on Windows), read a
 * few images later so as not to wait for the GPU. Outside Direct3D 11, nothing. */
#ifndef PCE_APP_GPU_TIME_H
#define PCE_APP_GPU_TIME_H

#include <SDL3/SDL.h>

/* 1 = measures (the renderer is Direct3D 11) */
int gpu_time_init(SDL_Renderer *r);
/* Mark k of the image: 0 = start, 1 = frame assembled (before the screen pass: scaling, CRT, layers), 2 = before present */
void gpu_time_mark(SDL_Renderer *r, int k);
/* After present: target_ns = the time (SDL_GetTicksNS) at which the clock planned for it to appear; compares it with
 * the time it actually appeared (swap chain statistics) */
void gpu_time_presented(SDL_Renderer *r, uint64_t target_ns);
void gpu_time_report(void);
void gpu_time_quit(void);

#endif
