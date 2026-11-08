#include "app/pacing.h"
#include "app/video.h"

#include <math.h>

#define PACE_RESTART_NS 250000000.0 /* more than this since the previous image (window stalled, menu): restarts the grid */
#define PACE_DRAW_MARGIN_NS 500000.0 /* margin over the longest recent draw */

void pace_setup(PaceClock *c, int vsync, double hz, int smooth)
{
    if (hz < 20.0)
        hz = 60.0;
    int n = video_refreshes_per_frame((float)hz);
    c->vsync = vsync;
    c->refresh_ns = 1e9 / hz;
    if (vsync) {
        c->per_frame = n;
        c->image_ns = c->refresh_ns;
        c->tau_step = n ? 1.0 / n : c->refresh_ns / PACE_FRAME_NS;
    } else {
        int m = !smooth ? 1 : n ? n : (int)(hz / 60.05);
        m = m < 1 ? 1 : m;
        c->per_frame = m;
        c->image_ns = PACE_FRAME_NS / m;
        if (c->image_ns < c->refresh_ns * PACE_VRR_MARGIN)
            c->image_ns = c->refresh_ns * PACE_VRR_MARGIN;
        c->tau_step = 1.0 / m;
    }
    c->target = 0;
}

static void plan_image(PaceClock *c, uint64_t now, uint64_t vblank);

double pace_plan(PaceClock *c, uint64_t now, uint64_t vblank, long accepted)
{
    /* Planned as light; if it asks for a frame the window does not have yet, again as heavy */
    PaceClock light = *c;
    light.draw_ns = c->draw_est[0];
    plan_image(&light, now, vblank);
    if (accepted >= 0 && pace_need(&light) <= accepted) {
        *c = light;
        return c->tau;
    }
    c->draw_ns = c->draw_est[1];
    plan_image(c, now, vblank);
    return c->tau;
}

static void plan_image(PaceClock *c, uint64_t now, uint64_t vblank)
{
    double T = c->image_ns, prev = (double)c->target, t;
    if (prev > 0.0 && (double)now - prev > PACE_RESTART_NS)
        prev = 0.0;
    if (c->vsync) {
        /* the first refresh for which the drawing finishes by the one before it, the present refresh (pacing.h). If
         * the drawing goes past that, it goes out as soon as it is ready. */
        double base = vblank ? (double)vblank : prev > 0.0 ? prev : (double)now;
        double ready = (double)now + c->draw_ns + PACE_DRAW_MARGIN_NS + T;
        t = base + ceil((ready - base) / T) * T;
        if (prev > 0.0 && t < prev + T / 2)
            t += ceil((prev + T / 2 - t) / T) * T;
        double at = t - T;
        c->present_at = at > (double)now ? (uint64_t)at : now;
        c->intervals = prev > 0.0 ? (int)floor((t - prev) / T + 0.5) : 0;
        c->tau += c->intervals * c->tau_step;
    } else {
        /* at equal intervals; late drawing: as soon as possible (the position follows the time) */
        /* no slack after the time (appears when presented): estimated draw 25% longer */
        double ready = (double)now + c->draw_ns * 1.25 + PACE_DRAW_MARGIN_NS;
        t = prev > 0.0 ? prev + T : ready;
        if (t < ready)
            t = ready;
        c->present_at = (uint64_t)t;
        c->intervals = prev > 0.0 ? (int)floor((t - prev) / T + 0.5) : 0;
        if (prev > 0.0)
            c->tau += (t - prev) / T * c->tau_step;
    }
    if (c->present_at < now)
        c->present_at = now;
    c->target = (uint64_t)t;
    c->images++;
}

long pace_sync(PaceClock *c, long latest, long accepted, double *f)
{
    long need = pace_need(c), b;
    if (accepted >= 0 && need <= accepted) {
        b = accepted; /* B is already in the window */
        if (c->tau < accepted - 1)
            c->tau = (double)(accepted - 1);
    } else if (latest < need) {
        b = latest; /* the console has not made the frame yet: stays on the last one */
        if (latest >= 0) {
            c->tau = (double)latest;
            c->holds++;
        }
    } else if (latest == need || accepted < 0) {
        b = latest;
        if (latest != need)
            c->tau = (double)(latest - 1);
    } else {
        b = latest; /* the requested frame has already passed (window stalled, fast-forward): restarts from the last one */
        c->tau = (double)(latest - 1);
        c->jumps++;
    }
    double x = c->tau - (double)(b - 1);
    *f = x < 0.0 ? 0.0 : x > 1.0 ? 1.0 : x;
    return b;
}

void pace_presented(PaceClock *c, uint64_t now, uint64_t draw_ns, int heavy)
{
    int k = heavy != 0;
    c->recent[k][c->recent_at[k]] = (double)draw_ns;
    c->recent_at[k] = (c->recent_at[k] + 1) % PACE_DRAW_RECENT;
    double top[PACE_DRAW_RANK] = { 0 }; /* the largest, in order */
    for (int i = 0; i < PACE_DRAW_RECENT; i++) {
        double d = c->recent[k][i];
        for (int j = 0; j < PACE_DRAW_RANK; j++)
            if (d > top[j]) {
                for (int m = PACE_DRAW_RANK - 1; m > j; m--)
                    top[m] = top[m - 1];
                top[j] = d;
                break;
            }
    }
    double robust = top[PACE_DRAW_RANK - 1];
    c->draw_est[k] = top[1] > robust * PACE_DRAW_SPIKE ? robust : top[1];
    /* the heavy one never below the light one (at the start, with no measurements of it) */
    c->draw_est[1] = c->draw_est[1] > c->draw_est[0] ? c->draw_est[1] : c->draw_est[0];
    /* with V-Sync, presented more than 1/4 period after the refresh before the predicted one (with a composed screen,
     * it appears one refresh later); without V-Sync, more than 1/4 interval after the time */
    double limit = (double)c->target + (c->vsync ? -c->image_ns * 3 / 4 : c->image_ns / 4);
    if (c->target && (double)now > limit)
        c->late++;
}

double pace_rate_hz(const PaceRateWatch *w)
{
    return w->peak > w->peak_prev ? w->peak : w->peak_prev;
}

int pace_rate_changed(PaceRateWatch *w, double hz, double in_use, uint64_t now)
{
    if (hz > 0.0) {
        if (!w->bucket_start || (double)(now - w->bucket_start) >= PACE_RATE_PEAK_NS) {
            w->peak_prev = w->bucket_start ? w->peak : hz;
            w->peak = 0.0;
            w->bucket_start = now ? now : 1;
        }
        if (hz > w->peak)
            w->peak = hz;
        hz = pace_rate_hz(w);
    }
    if (hz <= 0.0 || fabs(hz - in_use) <= 0.01 * in_use) {
        w->off_since = 0;
        return 0;
    }
    if (!w->off_since)
        w->off_since = now ? now : 1;
    if ((double)(now - w->off_since) < PACE_RATE_HOLD_NS)
        return 0;
    w->off_since = 0;
    return 1;
}

long pace_need(const PaceClock *c)
{
    return (long)ceil(c->tau - 1e-6);
}
