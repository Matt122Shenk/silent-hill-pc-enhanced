/* SPDX-License-Identifier: GPL-3.0-or-later */
/* RetroAchievements transport and sign-in UI for the browser build.
 *
 * Transport: a page has no threads, so instead of pc_ra_http.c's blocking
 * worker every job becomes a fetch(); its completion is handed back to
 * pc_retroachievements.c's completed queue, which Pc_Ra_Update already drains
 * on the main thread. retroachievements.org sends no CORS headers, so API
 * calls go to the site's own /ra/ relay (web_port/firebase/functions), which
 * forwards them; badge art is served with CORS and is fetched directly.
 *
 * UI: the game's 2D screens have no text entry, so the Achievements row opens
 * the page's own dialog (web_port/www/shell.js) -- sign-in when signed out, a
 * sign-out confirmation when signed in. */
#if defined(__EMSCRIPTEN__) && defined(SH_RETROACHIEVEMENTS)

#include <stdlib.h>
#include <emscripten.h>

#include "pc_config.h"
#include "pc_retroachievements.h"
#include "web_platform.h"

void Pc_RaWebJobDone(void* job, int status, char* body, size_t len);

EMSCRIPTEN_KEEPALIVE void Web_RaFetchDone(void* job, int status, char* body, int len)
{
    Pc_RaWebJobDone(job, status, body, (size_t)(len > 0 ? len : 0));
}

/* status 0 = no response at all; rc_client treats it as a network error. */
EM_JS(void, Web_RaFetchJs, (void* job, const char* url, const char* post, const char* ctype), {
    var u = UTF8ToString(url);
    var opts = { method: "GET" };
    if (post) {
        opts.method = "POST";
        opts.body = UTF8ToString(post);
        opts.headers = { "Content-Type": ctype ? UTF8ToString(ctype) : "application/x-www-form-urlencoded" };
    }
    /* String ops, not a regex: a slash pair inside EM_JS reads as a C comment. */
    var host = u.indexOf("retroachievements.org/");
    if (host >= 0 && u.indexOf("media.") < 0)
        u = "/ra/" + u.substring(host + "retroachievements.org/".length);
    fetch(u, opts).then(function (r) {
        return r.arrayBuffer().then(function (ab) { return { status: r.status, bytes: new Uint8Array(ab) }; });
    }).catch(function () {
        return { status: 0, bytes: new Uint8Array(0) };
    }).then(function (res) {
        var ptr = 0;
        if (res.bytes.length) {
            ptr = _malloc(res.bytes.length + 1);
            HEAPU8.set(res.bytes, ptr);
            HEAPU8[ptr + res.bytes.length] = 0;
        }
        _Web_RaFetchDone(job, res.status, ptr, res.bytes.length);
    });
});

void Web_RaFetch(void* job, const char* url, const char* post, const char* contentType)
{
    Web_RaFetchJs(job, url, post, contentType);
}

EM_JS(void, Web_RaDialogJs, (int signedIn, const char* user), {
    if (window.SHWeb && SHWeb.raDialog)
        SHWeb.raDialog(!!signedIn, UTF8ToString(user));
});

void Web_ShowRaDialog(void)
{
    Web_RaDialogJs(Pc_Ra_IsSignedIn(), g_PcConfig.raUsername);
}

EMSCRIPTEN_KEEPALIVE void Web_RaLogin(const char* username, const char* password)
{
    Pc_Ra_BeginPasswordLogin(username, password);
}

EMSCRIPTEN_KEEPALIVE void Web_RaLogout(void)
{
    Pc_Ra_SignOut();
}

#endif
