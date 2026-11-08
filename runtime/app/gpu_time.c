/* GPU time of each image: see gpu_time.h. */
#include "app/gpu_time.h"

#include <math.h>
#include <stdio.h>

#ifdef _WIN32
#define COBJMACROS
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>

#define SLOTS 16 /* images in flight: read when the slot is reused */
#define BINS 32  /* per 0.5 ms; the last one gathers 15.5 ms or more */

typedef struct Slot {
    ID3D11Query *disjoint, *ts[3];
    int marks; /* bits of the marks made */
    int busy;
} Slot;

static ID3D11DeviceContext *g_ctx;
static Slot g_slot[SLOTS];
static int g_at = -1;
static long g_hist[BINS], g_count, g_lost, g_split;
static double g_sum, g_max, g_sum_part[2];

/* Time the image appeared minus the planned time, per 0.5 ms from -10 to +20 ms */
#define ERR_BINS 60
#define ERR_RING 64
static uint64_t g_target[ERR_RING];
static UINT g_seen;
static long g_err[ERR_BINS], g_err_n, g_err_fail;

static void collect(Slot *s)
{
    D3D11_QUERY_DATA_TIMESTAMP_DISJOINT dj;
    UINT64 t[3];
    s->busy = 0;
    if ((s->marks & 5) != 5 ||
        ID3D11DeviceContext_GetData(g_ctx, (ID3D11Asynchronous *)s->disjoint, &dj, sizeof(dj),
                                    D3D11_ASYNC_GETDATA_DONOTFLUSH) != S_OK ||
        dj.Disjoint || !dj.Frequency) {
        g_lost++;
        return;
    }
    for (int k = 0; k < 3; k++)
        if ((s->marks >> k & 1) && ID3D11DeviceContext_GetData(g_ctx, (ID3D11Asynchronous *)s->ts[k], &t[k], sizeof(t[k]),
                                                               D3D11_ASYNC_GETDATA_DONOTFLUSH) != S_OK) {
            g_lost++;
            return;
        }
    double ms = (double)(t[2] - t[0]) * 1000.0 / (double)dj.Frequency;
    int b = (int)(ms * 2.0);
    g_hist[b >= BINS ? BINS - 1 : b < 0 ? 0 : b]++;
    g_sum += ms;
    g_max = ms > g_max ? ms : g_max;
    g_count++;
    if (s->marks & 2) {
        g_sum_part[0] += (double)(t[1] - t[0]) * 1000.0 / (double)dj.Frequency;
        g_sum_part[1] += (double)(t[2] - t[1]) * 1000.0 / (double)dj.Frequency;
        g_split++;
    }
}

int gpu_time_init(SDL_Renderer *r)
{
    ID3D11Device *dev = SDL_GetPointerProperty(SDL_GetRendererProperties(r), SDL_PROP_RENDERER_D3D11_DEVICE_POINTER, NULL);
    if (!dev)
        return 0;
    ID3D11Device_GetImmediateContext(dev, &g_ctx);
    D3D11_QUERY_DESC dj = { D3D11_QUERY_TIMESTAMP_DISJOINT, 0 }, ts = { D3D11_QUERY_TIMESTAMP, 0 };
    for (int i = 0; i < SLOTS; i++) {
        if (FAILED(ID3D11Device_CreateQuery(dev, &dj, &g_slot[i].disjoint)))
            return 0;
        for (int k = 0; k < 3; k++)
            if (FAILED(ID3D11Device_CreateQuery(dev, &ts, &g_slot[i].ts[k])))
                return 0;
    }
    return 1;
}

void gpu_time_mark(SDL_Renderer *r, int k)
{
    if (!g_ctx || (k > 0 && g_at < 0))
        return;
    SDL_FlushRenderer(r); /* what SDL batched goes before the mark */
    if (k == 0) {
        g_at = (g_at + 1) % SLOTS;
        Slot *s = &g_slot[g_at];
        if (s->busy)
            collect(s);
        s->marks = 0;
        ID3D11DeviceContext_Begin(g_ctx, (ID3D11Asynchronous *)s->disjoint);
    }
    Slot *s = &g_slot[g_at];
    ID3D11DeviceContext_End(g_ctx, (ID3D11Asynchronous *)s->ts[k]);
    s->marks |= 1 << k;
    if (k == 2) {
        ID3D11DeviceContext_End(g_ctx, (ID3D11Asynchronous *)s->disjoint);
        s->busy = 1;
    }
}

void gpu_time_presented(SDL_Renderer *r, uint64_t target_ns)
{
    IDXGISwapChain *sc;
    UINT id;
    DXGI_FRAME_STATISTICS st;
    if (!g_ctx || !target_ns ||
        !(sc = SDL_GetPointerProperty(SDL_GetRendererProperties(r), SDL_PROP_RENDERER_D3D11_SWAPCHAIN_POINTER, NULL)))
        return;
    if (IDXGISwapChain_GetLastPresentCount(sc, &id) == S_OK)
        g_target[id % ERR_RING] = target_ns;
    if (IDXGISwapChain_GetFrameStatistics(sc, &st) != S_OK) {
        g_err_fail++;
        return;
    }
    if (st.PresentCount == g_seen || !g_target[st.PresentCount % ERR_RING])
        return;
    g_seen = st.PresentCount;
    double freq = (double)SDL_GetPerformanceFrequency();
    double shown = (double)SDL_GetTicksNS() - (double)(SDL_GetPerformanceCounter() - (Uint64)st.SyncQPCTime.QuadPart) * 1e9 / freq;
    double ms = (shown - (double)g_target[st.PresentCount % ERR_RING]) / 1e6;
    int b = (int)floor((ms + 10.0) * 2.0);
    g_err[b < 0 ? 0 : b >= ERR_BINS ? ERR_BINS - 1 : b]++;
    g_err_n++;
}

void gpu_time_report(void)
{
    if (!g_ctx)
        return;
    printf("Image on screen minus planned time (swap chain statistics): %ld measurements, %ld failures\n  per 0.5 ms:",
           g_err_n, g_err_fail);
    for (int b = 0; b < ERR_BINS; b++)
        if (g_err[b])
            printf(" %.1f: %ld", b * 0.5 - 10.0, g_err[b]);
    printf("\n");
    printf("GPU (Direct3D 11 timestamps): %ld images, average %.2f ms, max %.2f ms (%ld unread)",
           g_count, g_count ? g_sum / g_count : 0.0, g_max, g_lost);
    if (g_split)
        printf("; assembly %.2f ms + screen %.2f ms", g_sum_part[0] / g_split, g_sum_part[1] / g_split);
    printf("\n  per 0.5 ms:");
    for (int b = 0; b < BINS; b++)
        if (g_hist[b])
            printf(" %s%.1f: %ld", b == BINS - 1 ? ">=" : "", b * 0.5, g_hist[b]);
    printf("\n");
}

void gpu_time_quit(void)
{
    for (int i = 0; i < SLOTS; i++) {
        if (g_slot[i].disjoint)
            ID3D11Query_Release(g_slot[i].disjoint);
        for (int k = 0; k < 3; k++)
            if (g_slot[i].ts[k])
                ID3D11Query_Release(g_slot[i].ts[k]);
    }
    if (g_ctx)
        ID3D11DeviceContext_Release(g_ctx);
    g_ctx = NULL;
}
#else
int gpu_time_init(SDL_Renderer *r)
{
    (void)r;
    return 0;
}
void gpu_time_mark(SDL_Renderer *r, int k)
{
    (void)r;
    (void)k;
}
void gpu_time_presented(SDL_Renderer *r, uint64_t target_ns)
{
    (void)r;
    (void)target_ns;
}
void gpu_time_report(void)
{
}
void gpu_time_quit(void)
{
}
#endif
