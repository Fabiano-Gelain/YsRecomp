/* Time of the next screen refresh and the period, on the SDL_GetPerformanceCounter clock. 0 = unknown
 * (outside Windows, or the compositor did not answer). */
#ifndef PCE_APP_VBLANK_H
#define PCE_APP_VBLANK_H

#include <stdint.h>

int vblank_query(uint64_t *next_qpc, uint64_t *period_qpc);

/* The whole process above normal priority (also the video driver and sound threads): with all cores
 * busy (12 Python loops), at 180 Hz 4977 images came out instead of ~7100; this way, 7021. Outside Windows, nothing. */
void vblank_raise_priority(void);

#endif
