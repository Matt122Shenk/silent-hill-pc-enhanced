/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * pc_harry_hud.c - Harry's health HUD: classic bar or heartbeat pulse line.
 *
 * Mirrors pc_ammo_hud.c (own GL program + VAO, full state save/restore, corner
 * / scale / opacity knobs, alpha fade with hold timer). The widget is one
 * CPU-composed RGBA quad: backdrop, fill or waveform in the HP band color,
 * optional numeric readout at the right. Re-composed when HP, pulse phase,
 * scale, style or number changes; the pulse re-composes every frame.
 */
#include "game.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include <SDL.h>
#include <PsyX/common/glad.h>
#include <PsyX/PsyX_backend.h>

#include "sh_log.h"
#include "pc_config.h"
#include "pc_harry_hud.h"

#include "stb_truetype.h"

static GLuint s_prog, s_vao, s_vbo;
static GLint  s_locTex, s_locColor;
static int    s_glReady;

static GLuint s_texWidget;
static int    s_texW, s_texH;

static int   s_bakedHp = -1, s_bakedPhase = -1, s_bakedStyle = -1, s_bakedNum = -1;
static float s_bakedScale = 0.0f;

static float  s_alpha = 0.0f;
static Uint32 s_hideAt = 0;
static Uint32 s_lastDraw = 0;
static int    s_lastHp = -1;

#define HH_HOLD_MS 3000u
#define HH_GARBAGE_MAX 8
static GLuint s_garbage[HH_GARBAGE_MAX];
static int    s_garbageCount;

static stbtt_fontinfo s_font;
static unsigned char* s_fontData;
static int            s_fontOk;
static int            s_fontsTried;

/* HP band colors (frac >= 0.6 / 0.35 / 0.15 / below). */
static const unsigned char s_bandCol[4][3] = {
    { 70, 220, 90 }, { 235, 210, 60 }, { 240, 140, 30 }, { 224, 24, 24 }
};

static int Hh_Band(float frac)
{
    if (frac >= 0.6f)  return 0;
    if (frac >= 0.35f) return 1;
    if (frac >= 0.15f) return 2;
    return 3;
}

static GLuint hh_make_shader(GLenum type, const char* src, const char* what)
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
        SH_DBG("[HARYHUD] %s shader failed: %s", what, log);
        glDeleteShader(sh);
        return 0;
    }
    return sh;
}

static void hh_gl_init(void)
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

    vs = hh_make_shader(GL_VERTEX_SHADER, vs_src, "vertex");
    fs = hh_make_shader(GL_FRAGMENT_SHADER, fs_src, "fragment");
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
        SH_DBG("[HARRYHUD] program link failed: %s", log);
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

static void hh_quad(GLuint tex, float x0, float yTop, float x1, float yBot, float alpha)
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

static unsigned char* hh_read_file(const char* path, size_t* outLen)
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

static void hh_fonts_init(void)
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
        unsigned char* data = hh_read_file(paths[i], &len);
        if (!data)
            continue;
        if (stbtt_InitFont(&s_font, data, stbtt_GetFontOffsetForIndex(data, 0)))
        {
            s_fontData = data;
            s_fontOk   = 1;
            SH_DBG("[HARRYHUD] font: %s", paths[i]);
            return;
        }
        free(data);
    }
    SH_DBG("[HARRYHUD] no usable font - number disabled");
}

/* Blit one white-coverage glyph row into the widget buffer. */
static void hh_blt_glyph(unsigned char* rgba, int W, int H, int cp, float scale,
                         int penX, int baseY, unsigned char r, unsigned char g, unsigned char b)
{
    int gx0, gy0, gx1, gy1, gw, gh, sx, sy;
    float shiftX = (float)penX - floorf((float)penX);
    static unsigned char scratch[256 * 256];

    stbtt_GetCodepointBitmapBoxSubpixel(&s_font, cp, scale, scale, shiftX, 0.0f, &gx0, &gy0, &gx1, &gy1);
    gw = gx1 - gx0;
    gh = gy1 - gy0;
    if (gw <= 0 || gh <= 0 || gw > 256 || gh > 256)
        return;
    memset(scratch, 0, (size_t)gw * gh);
    stbtt_MakeCodepointBitmapSubpixel(&s_font, scratch, gw, gh, gw, scale, scale, shiftX, 0.0f, cp);
    for (sy = 0; sy < gh; sy++)
    {
        int dy = baseY + gy0 + sy;
        unsigned char* row;
        if (dy < 0 || dy >= H)
            continue;
        row = rgba + (size_t)dy * W * 4;
        for (sx = 0; sx < gw; sx++)
        {
            int dx = penX + gx0 + sx;
            unsigned char v;
            unsigned char* px;
            if (dx < 0 || dx >= W)
                continue;
            v = scratch[sy * gw + sx];
            if (v == 0)
                continue;
            px = row + (size_t)dx * 4;
            px[0] = r; px[1] = g; px[2] = b;
            if (v > px[3])
                px[3] = v;
        }
    }
}

static void hh_fill_rect(unsigned char* rgba, int W, int H,
                         int x0, int y0, int x1, int y1,
                         unsigned char r, unsigned char g, unsigned char b, unsigned char a)
{
    int x, y;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > W) x1 = W;
    if (y1 > H) y1 = H;
    for (y = y0; y < y1; y++)
    {
        unsigned char* row = rgba + (size_t)y * W * 4;
        for (x = x0; x < x1; x++)
        {
            unsigned char* px = row + (size_t)x * 4;
            px[0] = r; px[1] = g; px[2] = b; px[3] = a;
        }
    }
}

/* ECG spike height (0..1) for a cycle phase in [0,1): flat baseline with a
 * narrow QRS-like up/down spike plus a small T bump. */
static float hh_ecg(float ph)
{
    if (ph < 0.10f) return 0.0f;
    if (ph < 0.14f) return (ph - 0.10f) / 0.04f * 0.25f;
    if (ph < 0.18f) return 0.25f - (ph - 0.14f) / 0.04f * 0.25f;
    if (ph < 0.22f) return 0.0f;
    if (ph < 0.26f) return (ph - 0.22f) / 0.04f;
    if (ph < 0.34f) return 1.0f - (ph - 0.26f) / 0.08f;
    if (ph < 0.44f) return -(ph - 0.34f) / 0.10f * 0.3f;
    if (ph < 0.54f) return -0.3f + (ph - 0.44f) / 0.10f * 0.3f;
    if (ph < 0.60f) return (ph - 0.54f) / 0.06f * 0.2f;
    if (ph < 0.66f) return 0.2f - (ph - 0.60f) / 0.06f * 0.2f;
    return 0.0f;
}

/* Compose the widget: backdrop, bar fill or scrolling pulse in the band
 * color, optional number at the right. */
static GLuint hh_compose(int hp, float frac, int band, int style, int number,
                         float pxScale, int* outW, int* outH, float phase)
{
    const unsigned char* col = s_bandCol[band];
    int   pad = (int)(6.0f * pxScale + 0.5f);
    int   barW, barH, gap, numW;
    int   W, H;
    unsigned char* rgba;
    GLuint tex;
    int asc, desc, gapm;
    char numbuf[16];
    int x;

    if (pad < 1) pad = 1;
    barW = (int)(190.0f * pxScale + 0.5f);
    barH = (int)(20.0f * pxScale + 0.5f);
    if (barW < 8) barW = 8;
    if (barH < 4) barH = 4;
    gap = (int)(8.0f * pxScale + 0.5f);
    numW = number ? (int)(52.0f * pxScale + 0.5f) : 0;
    W = pad + barW + (number ? gap + numW : 0) + pad;
    H = pad + barH + pad;

    if (W <= 0 || H <= 0 || W > 1024 || H > 256)
        return 0;
    rgba = (unsigned char*)calloc((size_t)W * H, 4);
    if (!rgba)
        return 0;

    /* Backdrop. */
    hh_fill_rect(rgba, W, H, 0, 0, W, H, 8, 8, 10, 200);
    /* Bar bed. */
    hh_fill_rect(rgba, W, H, pad, pad, pad + barW, pad + barH, 24, 24, 26, 220);

    if (style == 0)
    {
        int fw = (int)(barW * frac + 0.5f);
        if (fw > 0)
            hh_fill_rect(rgba, W, H, pad, pad, pad + fw, pad + barH, col[0], col[1], col[2], 255);
    }
    else
    {
        int lx = pad;
        int edge = (int)(3.0f * pxScale + 0.5f);
        int margin = (int)(6.0f * pxScale + 0.5f);
        int thick = (int)(pxScale + 0.5f);
        int baseY, amp;
        if (edge < 1) edge = 1;
        if (margin < 1) margin = 1;
        if (thick < 1) thick = 1;
        baseY = pad + barH - edge;
        amp = (barH - margin) > 4 ? (barH - margin) : 4;
        for (x = 0; x < barW; x++)
        {
            float ph = (float)x / (float)barW - phase;
            float h;
            int y0, y1, yy;
            ph -= floorf(ph);
            h = hh_ecg(ph);
            y0 = baseY - (int)(h * amp + 0.5f);
            y1 = baseY + 1;
            if (y0 > y1) { int t = y0; y0 = y1; y1 = t; }
            for (yy = y0; yy <= y1 + thick; yy++)
            {
                unsigned char* px;
                if (yy < pad || yy >= pad + barH)
                    continue;
                px = rgba + ((size_t)yy * W + (lx + x)) * 4;
                px[0] = col[0]; px[1] = col[1]; px[2] = col[2]; px[3] = 255;
            }
        }
    }

    if (number && s_fontOk)
    {
        float fscale;
        int penX, baseY, prev = 0;
        const char* p;
        snprintf(numbuf, sizeof(numbuf), "%d", hp);
        fscale = stbtt_ScaleForPixelHeight(&s_font, (float)barH + 2.0f * pxScale);
        stbtt_GetFontVMetrics(&s_font, &asc, &desc, &gapm);
        penX  = pad + barW + gap;
        baseY = pad + (int)(asc * fscale + 0.5f) + (int)(pxScale + 0.5f);
        p = numbuf;
        while (*p)
        {
            int cp = (unsigned char)*p++;
            int adv, lsb;
            if (prev)
                penX += (int)(stbtt_GetCodepointKernAdvance(&s_font, prev, cp) * fscale);
            hh_blt_glyph(rgba, W, H, cp, fscale, penX, baseY, col[0], col[1], col[2]);
            stbtt_GetCodepointHMetrics(&s_font, cp, &adv, &lsb);
            penX += (int)(adv * fscale);
            prev = cp;
        }
    }

    tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, W, H, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    free(rgba);

    if (outW) *outW = W;
    if (outH) *outH = H;
    return tex;
}

static void hh_retire(GLuint tex)
{
    if (!tex)
        return;
    if (s_garbageCount < HH_GARBAGE_MAX)
        s_garbage[s_garbageCount++] = tex;
    else
        glDeleteTextures(1, &tex);
}

static void hh_gl_pump(void)
{
    int i;
    for (i = 0; i < s_garbageCount; i++)
        glDeleteTextures(1, &s_garbage[i]);
    s_garbageCount = 0;
}

void Pc_HarryHud_Draw(void)
{
    GLint vp[4];
    float vpW, vpH;
    float scale, opac;
    int   corner, hp, style, number;
    float frac;
    int   want;
    float alpha, k, left, top, tw, th;
    float phase = 0.0f;
    int   phaseBucket = 0;
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

    if (!g_PcConfig.hpHud ||
        g_GameWork.gameState != GameState_InGame || g_SysWork.sysState != SysState_Gameplay)
    {
        s_alpha = 0.0f;
        s_hideAt = 0;
        s_lastHp = -1;
        return;
    }

    hp = (int)(g_SysWork.playerWork.player.health >> Q12_SHIFT);
    if (hp < 0) hp = 0;
    if (hp > 100) hp = 100;
    frac = (float)hp / 100.0f;

    if (s_lastHp < 0)
        s_lastHp = hp;
    if (hp != s_lastHp)
    {
        s_lastHp = hp;
        s_hideAt = now + HH_HOLD_MS;
    }

    want = g_PcConfig.hpAlways ? 1 : (s_hideAt != 0 && now < s_hideAt) ? 1 : 0;

    if (want)
    {
        if (g_PcConfig.hpAlways)
            s_hideAt = 0;
        k = dt * 12.0f;
        if (k > 1.0f) k = 1.0f;
        s_alpha += (1.0f - s_alpha) * k;
    }
    else
    {
        k = dt * 2.5f;
        if (k > 1.0f) k = 1.0f;
        s_alpha += (0.0f - s_alpha) * k;
    }
    if (s_alpha < 0.01f)
        return;

    style  = (g_PcConfig.hpStyle == 1) ? 1 : 0;
    number = g_PcConfig.hpNumber ? 1 : 0;

    corner = g_PcConfig.hpCorner;
    if (corner < 0 || corner > 3) corner = 1;
    scale = g_PcConfig.hpScale / 100.0f;
    if (scale < 0.6f) scale = 0.6f;
    if (scale > 1.3f) scale = 1.3f;
    opac = g_PcConfig.hpOpacity / 100.0f;
    if (opac < 0.2f) opac = 0.2f;
    if (opac > 1.0f) opac = 1.0f;
    alpha = s_alpha * opac;

    glGetIntegerv(GL_VIEWPORT, vp);
    vpW = (float)vp[2];
    vpH = (float)vp[3];
    if (vp[2] <= 0 || vp[3] <= 0)
        return;

    if (!s_glReady)
        hh_gl_init();
    if (s_glReady != 1)
        return;

    if (!s_fontsTried)
        hh_fonts_init();

    /* Pulse phase: scroll speed rises as HP falls (calm ~0.6 Hz full,
     * frantic ~2.5 Hz near zero), quantized so the widget re-composes only
     * when the wave visibly moves. */
    if (style == 1)
    {
        static float s_phase = 0.0f;
        float rate = 0.6f + (1.0f - frac) * 1.9f;
        s_phase += dt * rate;
        s_phase -= floorf(s_phase);
        phase = s_phase;
        phaseBucket = (int)(phase * 48.0f);
    }

    if (hp != s_bakedHp || phaseBucket != s_bakedPhase || style != s_bakedStyle ||
        number != s_bakedNum || fabsf(s_bakedScale - scale) > 0.001f)
    {
        hh_retire(s_texWidget);
        s_texWidget = 0;
    }
    if (!s_texWidget)
    {
        float pxScale = (vpH / 1080.0f) * scale;
        if (pxScale < 0.5f) pxScale = 0.5f;
        s_texWidget = hh_compose(hp, frac, Hh_Band(frac), style, (number && s_fontOk) ? 1 : 0,
                                 pxScale, &s_texW, &s_texH, phase);
        if (s_texWidget)
        {
            s_bakedHp = hp;
            s_bakedPhase = phaseBucket;
            s_bakedStyle = style;
            s_bakedNum = number;
            s_bakedScale = scale;
        }
    }
    if (!s_texWidget)
        return;

    if (corner == 0 || corner == 2)
        left = vpW * 0.035f;
    else
        left = vpW * 0.965f - (float)s_texW;
    if (corner < 2)
        top = vpH * 0.94f;
    else
        top = vpH * 0.06f + (float)s_texH;

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

    hh_gl_pump();

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

    tw = (float)s_texW;
    th = (float)s_texH;
    hh_quad(s_texWidget, NX(left), NY(top), NX(left + tw), NY(top - th), alpha);

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
