/* Launcher user images: JPG to ARGB */
#ifndef PCE_APP_LAUNCHER_IMAGE_H
#define PCE_APP_LAUNCHER_IMAGE_H

#include <stdint.h>

/* Reads the JPG (UTF-8 path). *argb = w x h ARGB pixels (free with free). 0 = ok */
int launcher_load_jpg(const char *path, uint32_t **argb, int *w, int *h);

#endif
