#ifndef WEB_PLATFORM_H
#define WEB_PLATFORM_H

#if defined(__EMSCRIPTEN__)
/* 1 when the page found a coarse pointer (phone/tablet), 0 on a desktop
 * browser. Decided once by the page; a touchscreen laptop reports 0 and gets
 * the touch controls only after a finger actually lands. */
int Web_IsTouchDevice(void);
#endif

#endif
