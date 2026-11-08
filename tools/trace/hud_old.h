/* The HUD read from the image colors, the old way: only to check the new one (pce_trace --crop-log) */
#ifndef PCE_TRACE_HUD_OLD_H
#define PCE_TRACE_HUD_OLD_H
#include "app/crop.h"

/* The console image (1 pixel per point, width src_w) */
void hud_old_layout(const uint32_t *src, int src_w, CropHudLayout *o);

#endif
