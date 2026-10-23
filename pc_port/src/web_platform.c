/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Browser-only platform queries. The page decides these before main() runs
 * (web_port/www/shell.js) and hands them over as environment variables, which
 * is the one channel Emscripten fills before the C runtime starts. */
#if defined(__EMSCRIPTEN__)

#include <stdlib.h>

#include "web_platform.h"

int Web_IsTouchDevice(void)
{
    const char* v = getenv("SH_WEB_TOUCH");

    return v != NULL && v[0] == '1';
}

#endif
