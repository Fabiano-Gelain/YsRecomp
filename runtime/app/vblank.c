/* Time of the next screen refresh, from Windows (app/pacing.h): DwmGetCompositionTimingInfo gives the compositor's
 * refresh grid on the high-resolution clock (the same as SDL_GetPerformanceCounter). Measured on the user's
 * monitor (120 Hz): period 8.3335 ms, the time is that of the next refresh, with no jitter. */
#include "app/vblank.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <dwmapi.h>

int vblank_query(uint64_t *next_qpc, uint64_t *period_qpc)
{
    DWM_TIMING_INFO ti;
    ZeroMemory(&ti, sizeof(ti));
    ti.cbSize = sizeof(ti);
    if (FAILED(DwmGetCompositionTimingInfo(NULL, &ti)) || ti.qpcVBlank == 0)
        return 0;
    /* the period from the nominal rate (rateRefresh): qpcRefreshPeriod fluctuates when the machine is busy (114-120 Hz
     * measured in the fast-forward intro test) */
    LARGE_INTEGER freq;
    QueryPerformanceFrequency(&freq);
    if (ti.rateRefresh.uiNumerator && ti.rateRefresh.uiDenominator)
        *period_qpc = (uint64_t)((double)freq.QuadPart * ti.rateRefresh.uiDenominator / ti.rateRefresh.uiNumerator + 0.5);
    else if (ti.qpcRefreshPeriod)
        *period_qpc = ti.qpcRefreshPeriod;
    else
        return 0;
    *next_qpc = ti.qpcVBlank;
    return 1;
}

void vblank_raise_priority(void)
{
    SetPriorityClass(GetCurrentProcess(), ABOVE_NORMAL_PRIORITY_CLASS);
}
#else
void vblank_raise_priority(void)
{
}

int vblank_query(uint64_t *next_qpc, uint64_t *period_qpc)
{
    (void)next_qpc;
    (void)period_qpc;
    return 0;
}
#endif
