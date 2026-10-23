#ifndef WEB_PLATFORM_H
#define WEB_PLATFORM_H

#if defined(__EMSCRIPTEN__)
/* 1 when the page found a coarse pointer (phone/tablet), 0 on a desktop
 * browser. Decided once by the page; a touchscreen laptop reports 0 and gets
 * the touch controls only after a finger actually lands. */
int Web_IsTouchDevice(void);

/* The canvas's CSS box, which is the only size a page's game window can
 * have: the browser owns the window, and fullscreen is just a bigger box.
 * Leaves w/h untouched if the canvas cannot be measured. */
void Web_CanvasSize(int* w, int* h);

/* Keeps g_PcConfig.windowWidth/Height equal to the canvas as it resizes. The
 * per-poly cull bounds and the 2D overlays divide by those, so a stale value
 * draws effects over only part of the screen. */
void Web_TrackWindowSize(void);
#endif

#endif
