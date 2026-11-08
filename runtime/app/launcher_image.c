/* Loads the launcher's user images (JPG) with the stb_image (public domain) that ships in the SDL source
 * (build/_deps/sdl3-src/src/video), JPG part only and with internal (static) functions */
#include "app/launcher_image.h"

#include <stdio.h>
#include <stdlib.h>

#include "util/u8file.h"

#include <SDL3/SDL.h> /* the SDL copy uses its own types (Uint8...) */

#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_THREAD_LOCALS
#define STBI_ASSERT SDL_assert
#define STB_IMAGE_STATIC
#define STBI_ONLY_JPEG
#define STBI_NO_STDIO
#define STBI_NO_LINEAR
#define STBI_NO_HDR
#if defined(_MSC_VER)
#pragma warning(push, 0)
#endif
#include "stb_image.h"
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

int launcher_load_jpg(const char *path, uint32_t **argb, int *w, int *h)
{
    *argb = NULL;
    FILE *f = u8_fopen(path, "rb");
    if (!f)
        return -1;
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    unsigned char *data = size > 0 ? malloc((size_t)size) : NULL;
    int ok = data && fread(data, 1, (size_t)size, f) == (size_t)size;
    fclose(f);
    int comp = 0;
    unsigned char *rgb = ok ? stbi_load_from_memory(data, (int)size, w, h, &comp, 3) : NULL;
    free(data);
    if (!rgb)
        return -1;
    uint32_t *out = malloc((size_t)*w * *h * sizeof(uint32_t));
    if (out)
        for (size_t i = 0; i < (size_t)*w * *h; i++)
            out[i] = 0xFF000000u | (uint32_t)rgb[i * 3] << 16 | (uint32_t)rgb[i * 3 + 1] << 8 | rgb[i * 3 + 2];
    stbi_image_free(rgb);
    *argb = out;
    return out ? 0 : -1;
}
