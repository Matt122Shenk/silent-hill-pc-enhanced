/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * pc_ammo_hud.c - real-time ammo readout (magazine / reserve) for firearms.
 *
 * Reads the live combat state (currentWeaponAmmo / totalWeaponAmmo, updated
 * on fire and reload) and draws a small text readout from the post-capture
 * hook. Shown while aiming; with ammo_always it stays up whenever a gun is
 * equipped. After release it holds a second, then fades.
 */
#include "game.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include <SDL.h>
#include <PsyX/common/glad.h>
#include <PsyX/PsyX_backend.h>

#include "bodyprog/items.h"
#include "sh_log.h"
#include "pc_config.h"
#include "pc_ammo_hud.h"

#include "stb_truetype.h"

extern u16 g_Player_IsAiming;

static GLuint s_prog, s_vao, s_vbo;
static GLint  s_locTex, s_locColor;
static int    s_glReady;

static GLuint s_texText;
static int    s_texTextW, s_texTextH;
static float  s_bakedScale = 0.0f;
static int    s_bakedMag = -1, s_bakedRes = -1;

static float  s_alpha = 0.0f;
static Uint32 s_hideAt = 0;
static Uint32 s_lastDraw = 0;

#define AMMO_GARBAGE_MAX 8
static GLuint s_garbage[AMMO_GARBAGE_MAX];
static int    s_garbageCount;

static stbtt_fontinfo s_font;
static unsigned char* s_fontData;
static int            s_fontOk;
static int            s_fontsTried;

static GLuint ammo_make_shader(GLenum type, const char* src, const char* what)
{
    GLuint sh = glCreateShader(type);
    GLint  ok = 0;
    const char* parts[2] = { PsyX_Shader_Preamble(type == GL_FRAGMENT_SHADER, PSYX_GLSL_LEGACY), src };
    glShaderSource(sh, 2, parts, NULL);
    glCompileShader(sh);
    glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
    if (!ok)
    {
        char log[512];
        glGetShaderInfoLog(sh, (GLsizei)sizeof(log), NULL, log);
        SH_DBG("[AMMOHUD] %s shader failed: %s", what, log);
        glDeleteShader(sh);
        return 0;
    }
    return sh;
}

static void ammo_gl_init(void)
{
    static const char* vs_src =
        "attribute vec2 a_pos;\n"
        "attribute vec2 a_uv;\n"
        "varying vec2 v_uv;\n"
        "void main() {\n"
        "    v_uv = a_uv;\n"
        "    gl_Position = vec4(a_pos, 0.0, 1.0);\n"
        "}\n";
    static const char* fs_src =
        "#ifdef GL_ES\n"
        "precision mediump float;\n"
        "#endif\n"
        "varying vec2 v_uv;\n"
        "uniform sampler2D u_tex;\n"
        "uniform vec4 u_color;\n"
        "void main() {\n"
        "    gl_FragColor = texture2D(u_tex, v_uv) * u_color;\n"
        "}\n";

    GLuint vs, fs;
    GLint  ok = 0;
    GLint  initPrevVao = 0, initPrevBuf = 0;

    s_glReady = -1;

    vs = ammo_make_shader(GL_VERTEX_SHADER, vs_src, "vertex");
    fs = ammo_make_shader(GL_FRAGMENT_SHADER, fs_src, "fragment");
    if (!vs || !fs)
        return;

    s_prog = glCreateProgram();
    glAttachShader(s_prog, vs);
    glAttachShader(s_prog, fs);
    glBindAttribLocation(s_prog, 0, "a_pos");
    glBindAttribLocation(s_prog, 1, "a_uv");
    glLinkProgram(s_prog);
    glGetProgramiv(s_prog, GL_LINK_STATUS, &ok);
    glDeleteShader(vs);
    glDeleteShader(fs);
    if (!ok)
    {
        char log[512];
        glGetProgramInfoLog(s_prog, (GLsizei)sizeof(log), NULL, log);
        SH_DBG("[AMMOHUD] program link failed: %s", log);
        glDeleteProgram(s_prog);
        s_prog = 0;
        return;
    }

    s_locTex   = glGetUniformLocation(s_prog, "u_tex");
    s_locColor = glGetUniformLocation(s_prog, "u_color");

    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &initPrevVao);
    glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &initPrevBuf);

    glGenVertexArrays(1, &s_vao);
    glBindVertexArray(s_vao);
    glGenBuffers(1, &s_vbo);
    glBindBuffer(GL_ARRAY_BUFFER, s_vbo);
    glBufferData(GL_ARRAY_BUFFER, 6 * 4 * sizeof(float), NULL, GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));

    glBindVertexArray((GLuint)initPrevVao);
    glBindBuffer(GL_ARRAY_BUFFER, (GLuint)initPrevBuf);

    s_glReady = 1;
}

static void ammo_quad(GLuint tex, float x0, float yTop, float x1, float yBot, float alpha)
{
    float v[6][4];
    if (!tex)
        return;
    v[0][0] = x0; v[0][1] = yTop; v[0][2] = 0.0f; v[0][3] = 0.0f;
    v[1][0] = x0; v[1][1] = yBot; v[1][2] = 0.0f; v[1][3] = 1.0f;
    v[2][0] = x1; v[2][1] = yTop; v[2][2] = 1.0f; v[2][3] = 0.0f;
    v[3][0] = x1; v[3][1] = yTop; v[3][2] = 1.0f; v[3][3] = 0.0f;
    v[4][0] = x0; v[4][1] = yBot; v[4][2] = 0.0f; v[4][3] = 1.0f;
    v[5][0] = x1; v[5][1] = yBot; v[5][2] = 1.0f; v[5][3] = 1.0f;

    glUniform4f(s_locColor, 1.0f, 1.0f, 1.0f, alpha);
    glBindTexture(GL_TEXTURE_2D, tex);
    glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(v), v);
    glDrawArrays(GL_TRIANGLES, 0, 6);
}

static GLuint ammo_upload_rgba(const unsigned char* rgba, int w, int h)
{
    GLuint t = 0;
    glGenTextures(1, &t);
    glBindTexture(GL_TEXTURE_2D, t);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    return t;
}

static unsigned char* ammo_read_file(const char* path, size_t* outLen)
{
    FILE*  f = fopen(path, "rb");
    long   n;
    unsigned char* buf;
    if (!f)
        return NULL;
    fseek(f, 0, SEEK_END);
    n = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (n <= 0) { fclose(f); return NULL; }
    buf = (unsigned char*)malloc((size_t)n);
    if (!buf) { fclose(f); return NULL; }
    if (fread(buf, 1, (size_t)n, f) != (size_t)n) { free(buf); fclose(f); return NULL; }
    fclose(f);
    if (outLen) *outLen = (size_t)n;
    return buf;
}

static void ammo_fonts_init(void)
{
    static const char* paths[] = {
        "gamedata/font/Oswald-Regular.ttf",
        "C:/Windows/Fonts/segoeui.ttf",
        "C:/Windows/Fonts/arial.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
    };
    int i;
    if (s_fontsTried)
        return;
    s_fontsTried = 1;
    for (i = 0; i < (int)(sizeof(paths) / sizeof(paths[0])); i++)
    {
        size_t len = 0;
        unsigned char* data = ammo_read_file(paths[i], &len);
        if (!data)
            continue;
        if (stbtt_InitFont(&s_font, data, stbtt_GetFontOffsetForIndex(data, 0)))
        {
            s_fontData = data;
            s_fontOk   = 1;
            SH_DBG("[AMMOHUD] font: %s", paths[i]);
            return;
        }
        free(data);
    }
    SH_DBG("[AMMOHUD] no usable font - hud disabled");
}

static int ammo_utf8_next(const char** p)
{
    const unsigned char* s = (const unsigned char*)*p;
    int cp;
    if (!*s) return 0;
    if (s[0] < 0x80)                       { cp = s[0];                                   *p += 1; }
    else if ((s[0] & 0xE0) == 0xC0 && s[1]) { cp = ((s[0] & 0x1F) << 6) | (s[1] & 0x3F);   *p += 2; }
    else if ((s[0] & 0xF0) == 0xE0 && s[1] && s[2])
                                             { cp = ((s[0] & 0x0F) << 12) | ((s[1] & 0x3F) << 6) | (s[2] & 0x3F); *p += 3; }
    else if ((s[0] & 0xF8) == 0xF0 && s[1] && s[2] && s[3])
                                             { cp = ((s[0] & 0x07) << 18) | ((s[1] & 0x3F) << 12) | ((s[2] & 0x3F) << 6) | (s[3] & 0x3F); *p += 4; }
    else                                     { cp = '?'; *p += 1; }
    return cp;
}

static GLuint ammo_bake(const char* text, float px, int maxW, int* outW, int* outH)
{
    float scale, widest = 0.0f;
    const char* p;
    int prev = 0, cp, outline, W, H, x, y, ox, oy, i;
    unsigned char *cov, *dil, *rgba, *scratch;
    GLuint tex;
    int asc, desc, gap;
    float penX, baseY;

    if (!s_fontOk)
        return 0;
    scale = stbtt_ScaleForPixelHeight(&s_font, px);
    p = text;
    while ((cp = ammo_utf8_next(&p)) != 0)
    {
        int adv, lsb;
        stbtt_GetCodepointHMetrics(&s_font, cp, &adv, &lsb);
        if (prev)
            widest += stbtt_GetCodepointKernAdvance(&s_font, prev, cp) * scale;
        widest += adv * scale;
        prev = cp;
    }
    if (maxW > 0 && widest > (float)maxW && widest > 1.0f)
    {
        float shrink = (float)maxW / widest;
        if (shrink < 0.55f) shrink = 0.55f;
        px *= shrink;
        scale = stbtt_ScaleForPixelHeight(&s_font, px);
    }
    if (widest < 1.0f)
        return 0;

    outline = (int)(px / 22.0f);
    if (outline < 2) outline = 2;

    stbtt_GetFontVMetrics(&s_font, &asc, &desc, &gap);
    W = (int)ceilf(widest) + 2 * outline + 2;
    H = (int)ceilf(px * 1.2f) + 2 * outline + 2;
    if (W <= 0 || H <= 0 || W > 2048 || H > 256)
        return 0;

    cov     = (unsigned char*)calloc((size_t)W * H, 1);
    dil     = (unsigned char*)calloc((size_t)W * H, 1);
    rgba    = (unsigned char*)calloc((size_t)W * H, 4);
    scratch = (unsigned char*)malloc((size_t)W * H);
    if (!cov || !dil || !rgba || !scratch)
    {
        free(cov); free(dil); free(rgba); free(scratch);
        return 0;
    }

    penX = (float)outline;
    baseY = (float)outline + asc * scale;
    p = text;
    prev = 0;
    while ((cp = ammo_utf8_next(&p)) != 0)
    {
        int gx0, gy0, gx1, gy1, gw, gh, adv, lsb, sx, sy;
        float shiftX;

        if (prev)
            penX += stbtt_GetCodepointKernAdvance(&s_font, prev, cp) * scale;
        shiftX = penX - floorf(penX);

        stbtt_GetCodepointBitmapBoxSubpixel(&s_font, cp, scale, scale,
                                            shiftX, 0.0f, &gx0, &gy0, &gx1, &gy1);
        gw = gx1 - gx0;
        gh = gy1 - gy0;
        if (gw > 0 && gh > 0 && gw <= W && gh <= H)
        {
            memset(scratch, 0, (size_t)gw * gh);
            stbtt_MakeCodepointBitmapSubpixel(&s_font, scratch,
                                              gw, gh, gw, scale, scale, shiftX, 0.0f, cp);
            for (sy = 0; sy < gh; sy++)
            {
                int dy = (int)baseY + gy0 + sy;
                if (dy < 0 || dy >= H) continue;
                for (sx = 0; sx < gw; sx++)
                {
                    int dx = (int)penX + gx0 + sx;
                    unsigned char v;
                    if (dx < 0 || dx >= W) continue;
                    v = scratch[sy * gw + sx];
                    if (v > cov[dy * W + dx])
                        cov[dy * W + dx] = v;
                }
            }
        }
        stbtt_GetCodepointHMetrics(&s_font, cp, &adv, &lsb);
        penX += adv * scale;
        prev = cp;
        if (penX > (float)(W - outline))
            break;
    }

    for (y = 0; y < H; y++)
    {
        for (x = 0; x < W; x++)
        {
            unsigned char best = 0;
            for (oy = -outline; oy <= outline; oy++)
            {
                int sy = y + oy;
                if (sy < 0 || sy >= H) continue;
                for (ox = -outline; ox <= outline; ox++)
                {
                    int sx = x + ox;
                    unsigned char v;
                    if (sx < 0 || sx >= W) continue;
                    if (ox * ox + oy * oy > outline * outline) continue;
                    v = cov[sy * W + sx];
                    if (v > best) best = v;
                }
            }
            dil[y * W + x] = best;
        }
    }

    for (i = 0; i < W * H; i++)
    {
        float aT = cov[i] / 255.0f;
        float aO = dil[i] / 255.0f;
        float a  = aT + aO * (1.0f - aT);
        unsigned char v = (a > 0.0f) ? (unsigned char)(255.0f * aT / a) : 0;
        rgba[i * 4 + 0] = v;
        rgba[i * 4 + 1] = v;
        rgba[i * 4 + 2] = v;
        rgba[i * 4 + 3] = (unsigned char)(255.0f * a);
    }

    tex = ammo_upload_rgba(rgba, W, H);
    if (outW) *outW = W;
    if (outH) *outH = H;

    free(cov); free(dil); free(rgba); free(scratch);
    return tex;
}

static void ammo_retire(GLuint tex)
{
    if (!tex)
        return;
    if (s_garbageCount < AMMO_GARBAGE_MAX)
        s_garbage[s_garbageCount++] = tex;
    else
        glDeleteTextures(1, &tex);
}

static void ammo_gl_pump(void)
{
    int i;
    for (i = 0; i < s_garbageCount; i++)
        glDeleteTextures(1, &s_garbage[i]);
    s_garbageCount = 0;
}

void Pc_AmmoHud_Draw(void)
{
    GLint vp[4];
    float vpW, vpH;
    float scale, opac;
    int   corner, mag, reserve, idx, gun;
    int   aiming, want;
    float alpha, k, left, top, tw, th;
    char  text[32];
    Uint32 now = SDL_GetTicks();
    Uint32 dtms;
    float  dt;
    static Uint32 s_lastTick = 0;

    GLint  prevProg = 0, prevVao = 0, prevBuf = 0, prevTex = 0;
    GLint  prevUnit = GL_TEXTURE0, prevAlign = 4;
    GLint  prevSrcRgb = GL_ONE, prevDstRgb = GL_ZERO;
    GLint  prevSrcA = GL_ONE, prevDstA = GL_ZERO;
    GLint  prevEqRgb = GL_FUNC_ADD, prevEqA = GL_FUNC_ADD;
    GLboolean prevBlend, prevDepth, prevCull;

    dtms = (s_lastTick != 0 && now >= s_lastTick) ? now - s_lastTick : 16;
    if (dtms > 100) dtms = 100;
    dt = (float)dtms / 1000.0f;
    s_lastTick = now;

    if (!g_PcConfig.ammoHud ||
        g_GameWork.gameState != GameState_InGame || g_SysWork.sysState != SysState_Gameplay)
    {
        s_alpha = 0.0f;
        s_hideAt = 0;
        return;
    }

    idx = g_SysWork.playerCombat.weaponInventoryIdx;
    gun = (idx >= 0 && idx < INV_ITEM_COUNT_MAX &&
           g_SavegamePtr->items[idx].id_0 >= InvItemId_Handgun &&
           g_SavegamePtr->items[idx].id_0 <= InvItemId_HyperBlaster);
    mag = (int)g_SysWork.playerCombat.currentWeaponAmmo;
    reserve = (int)g_SysWork.playerCombat.totalWeaponAmmo;

    aiming = (g_SysWork.playerCombat.isAiming || g_Player_IsAiming) ? 1 : 0;
    want = (gun && (aiming || g_PcConfig.ammoHudAlways)) ? 1 : 0;

    if (want)
    {
        s_hideAt = 0;
        k = dt * 12.0f;
        if (k > 1.0f) k = 1.0f;
        s_alpha += (1.0f - s_alpha) * k;
    }
    else
    {
        if (s_hideAt == 0)
            s_hideAt = now + 1000;
        if (now < s_hideAt)
        {
            if (s_alpha < 1.0f) s_alpha = 1.0f;
        }
        else
        {
            k = dt * 2.5f;
            if (k > 1.0f) k = 1.0f;
            s_alpha += (0.0f - s_alpha) * k;
        }
    }
    if (s_alpha < 0.01f)
        return;

    corner = g_PcConfig.ammoHudCorner;
    if (corner < 0 || corner > 3) corner = 2;
    scale = g_PcConfig.ammoHudScale / 100.0f;
    if (scale < 0.6f) scale = 0.6f;
    if (scale > 1.3f) scale = 1.3f;
    opac = g_PcConfig.ammoHudOpacity / 100.0f;
    if (opac < 0.2f) opac = 0.2f;
    if (opac > 1.0f) opac = 1.0f;
    alpha = s_alpha * opac;

    glGetIntegerv(GL_VIEWPORT, vp);
    vpW = (float)vp[2];
    vpH = (float)vp[3];
    if (vp[2] <= 0 || vp[3] <= 0)
        return;

    if (!s_glReady)
        ammo_gl_init();
    if (s_glReady != 1)
        return;

    if (!s_fontsTried)
        ammo_fonts_init();

    if (mag != s_bakedMag || reserve != s_bakedRes || fabsf(s_bakedScale - scale) > 0.001f)
    {
        ammo_retire(s_texText);
        s_texText = 0;
    }
    if (!s_texText && s_fontOk)
    {
        snprintf(text, sizeof(text), "%d / %d", mag, reserve);
        s_texText = ammo_bake(text, 0.034f * vpH * scale, (int)(0.20f * vpW), &s_texTextW, &s_texTextH);
        if (s_texText)
        {
            s_bakedMag = mag;
            s_bakedRes = reserve;
            s_bakedScale = scale;
        }
    }
    if (!s_texText)
        return;

    if (corner == 0 || corner == 2)
        left = vpW * 0.035f;
    else
        left = vpW * 0.965f - (float)s_texTextW;
    if (corner < 2)
        top = vpH * 0.94f;
    else
        top = vpH * 0.06f + (float)s_texTextH;

    glGetIntegerv(GL_CURRENT_PROGRAM, &prevProg);
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &prevVao);
    glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &prevBuf);
    glGetIntegerv(GL_ACTIVE_TEXTURE, &prevUnit);
    glActiveTexture(GL_TEXTURE0);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &prevTex);
    glGetIntegerv(GL_UNPACK_ALIGNMENT, &prevAlign);
    glGetIntegerv(GL_BLEND_SRC_RGB,   &prevSrcRgb);
    glGetIntegerv(GL_BLEND_DST_RGB,   &prevDstRgb);
    glGetIntegerv(GL_BLEND_SRC_ALPHA, &prevSrcA);
    glGetIntegerv(GL_BLEND_DST_ALPHA, &prevDstA);
    glGetIntegerv(GL_BLEND_EQUATION_RGB,   &prevEqRgb);
    glGetIntegerv(GL_BLEND_EQUATION_ALPHA, &prevEqA);
    prevBlend = glIsEnabled(GL_BLEND);
    prevDepth = glIsEnabled(GL_DEPTH_TEST);
    prevCull  = glIsEnabled(GL_CULL_FACE);

    ammo_gl_pump();

    glUseProgram(s_prog);
    glBindVertexArray(s_vao);
    glBindBuffer(GL_ARRAY_BUFFER, s_vbo);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));
    glUniform1i(s_locTex, 0);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glBlendEquation(GL_FUNC_ADD);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);

#define NX(px_) (-1.0f + 2.0f * (px_) / vpW)
#define NY(py_) (-1.0f + 2.0f * (py_) / vpH)

    tw = (float)s_texTextW;
    th = (float)s_texTextH;
    ammo_quad(s_texText, NX(left), NY(top), NX(left + tw), NY(top - th), alpha);

#undef NX
#undef NY

    glBindVertexArray((GLuint)prevVao);
    glBindBuffer(GL_ARRAY_BUFFER, (GLuint)prevBuf);
    glBindTexture(GL_TEXTURE_2D, (GLuint)prevTex);
    glActiveTexture((GLenum)prevUnit);
    glUseProgram((GLuint)prevProg);
    glPixelStorei(GL_UNPACK_ALIGNMENT, prevAlign);
    if (prevBlend) glEnable(GL_BLEND); else glDisable(GL_BLEND);
    if (prevDepth) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST);
    if (prevCull) glEnable(GL_CULL_FACE); else glDisable(GL_CULL_FACE);
    glBlendFuncSeparate((GLenum)prevSrcRgb, (GLenum)prevDstRgb,
                        (GLenum)prevSrcA,   (GLenum)prevDstA);
    glBlendEquationSeparate((GLenum)prevEqRgb, (GLenum)prevEqA);
}
