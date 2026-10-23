/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Browser-only platform queries. The page decides these before main() runs
 * (web_port/www/shell.js) and hands them over as environment variables, which
 * is the one channel Emscripten fills before the C runtime starts. */
#if defined(__EMSCRIPTEN__)

#include <stdlib.h>
#include <SDL.h>
#include <emscripten/html5.h>

#include "pc_config.h"
#include "web_platform.h"

int Web_IsTouchDevice(void)
{
    const char* v = getenv("SH_WEB_TOUCH");

    return v != NULL && v[0] == '1';
}

void Web_CanvasSize(int* w, int* h)
{
    double cw = 0.0, ch = 0.0;

    if (emscripten_get_element_css_size("#canvas", &cw, &ch) == EMSCRIPTEN_RESULT_SUCCESS &&
        cw >= 1.0 && ch >= 1.0)
    {
        *w = (int)cw;
        *h = (int)ch;
    }
}

/* Runs as the event is queued, ahead of PsyCross's own handling, so it reads
 * the event rather than g_windowWidth. */
static int SDLCALL Web_ResizeWatch(void* userdata, SDL_Event* ev)
{
    (void)userdata;
    if (ev->type == SDL_WINDOWEVENT && ev->window.event == SDL_WINDOWEVENT_RESIZED &&
        ev->window.data1 > 0 && ev->window.data2 > 0)
    {
        g_PcConfig.windowWidth  = ev->window.data1;
        g_PcConfig.windowHeight = ev->window.data2;
    }
    return 1;
}

void Web_TrackWindowSize(void)
{
    SDL_AddEventWatch(Web_ResizeWatch, NULL);
}

#endif
