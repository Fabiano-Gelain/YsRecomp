#include "app/intro_skip.h"

#include "pce/pce.h"

void intro_init(IntroSkip *s, int enabled, int to_menu)
{
    s->enabled = enabled;
    s->active = enabled;
    s->to_menu = enabled && to_menu;
    s->fast = s->to_menu;
    s->screen = INTRO_SCREEN_LOADING;
    s->frames = 0;
}

int intro_active(const IntroSkip *s)
{
    return s->active;
}

int intro_skip(IntroSkip *s)
{
    if (!s->active || s->fast)
        return 0;
    s->fast = 1;
    return 1;
}

uint8_t intro_auto_pad(const IntroSkip *s, int run_accepted)
{
    return s->active && s->to_menu && s->screen >= INTRO_SCREEN_COPYRIGHT && run_accepted ? PCE_PAD_RUN : 0;
}

int intro_frame(IntroSkip *s, IntroScreen screen)
{
    if (!s->active)
        return 0;
    int changed = screen != s->screen;
    s->screen = screen;
    s->frames++;
    IntroScreen last = s->to_menu ? INTRO_SCREEN_MENU : INTRO_SCREEN_COPYRIGHT;
    if (screen >= last || s->frames >= INTRO_MAX_FRAMES)
        s->active = 0;
    if (s->to_menu) {
        if (s->active)
            return 0;
        s->fast = 0;
        return 1;
    }
    if (s->fast && (changed || !s->active)) {
        s->fast = 0;
        return 1;
    }
    return 0;
}
