/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * pc_horde_hud.c - top-center horde run panel (wave / alive / kills / threat)
 * and the inter-wave reward draft.
 *
 * Own GL program + VAO, full state save/restore, drawn from the post-capture
 * hook next to the other HUDs. Text is CPU-composed RGBA (dark pill +
 * outlined text), re-baked only when a field changes.
 *
 * GL rules copied deliberately from pc_quick_options.c (read its comments for
 * the why; every one of these was a real corruption bug there):
 *   - ONE glyph atlas texture, claimed at startup (Pc_HordeHud_PreloadGL) and
 *     never deleted. No glGenTextures / glDeleteTextures during a frame: a
 *     freed name is recycled by PsyCross and a stale framebuffer attachment
 *     can render the scene into it. Bakes are glTexSubImage2D into slots.
 *   - glIsTexture check before the first bind each draw; the atlas is
 *     recreated if something else freed its name.
 *   - full state save/restore, vertex layout re-asserted every draw, and
 *     g_lastBoundTexture invalidated so PsyCross re-binds its own texture.
 *   - the mouse cursor is drawn here, in GL, as the last quad of the draft:
 *     the PSX-drawn one composites under every post-capture overlay.
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
#include "pc_horde.h"
#include "pc_horde_hud.h"
#include "pc_item_toast.h"
#include "pc_mouse_cursor.h"

#include "bodyprog/sound/sound_system.h"
#include "bodyprog/sound/sfx_id_enum.h"

#include "stb_truetype.h"

extern u8 g_Player_DisableControl;
extern unsigned int g_lastBoundTexture; /* PsyX TextureID */

static GLuint s_prog, s_vao, s_vbo;
static GLint  s_locTex, s_locColor;
static int    s_glReady;

/* Run panel. Texture values below are atlas SLOT handles (index + 1). */
static GLuint s_texPanel;
static int    s_texW, s_texH;
static GLuint s_texDmg;
static int    s_dmgW, s_dmgH;
static GLuint s_texLife;
static int    s_lifeW, s_lifeH;

static unsigned int s_bakedWave, s_bakedKills, s_bakedThreat, s_bakedDmg, s_bakedResist;
static int    s_bakedAlive, s_bakedCds, s_bakedVpH, s_bakedLang;
static int    s_bakedOnce;

/* Draft. */
static GLuint s_texHead, s_texHint, s_texCursor;
static int    s_headW, s_headH, s_hintW, s_hintH;
static GLuint s_texTitle[3], s_texSub[3];
static int    s_titleW[3], s_titleH[3], s_subW[3], s_subH[3];

static unsigned int s_draftWave;
static int    s_draftVpW, s_draftVpH;
static int    s_draftBaked;
static int    s_draftLang;

/* HUD language: Spanish when the PC language is Spanish (s_LangIds order:
 * en de fr es it pl), English for every other language -- same rule as the
 * item toast. */
static int horde_lang_es(void)
{
    return g_PcConfig.language == 3;
}

static stbtt_fontinfo s_font;
static unsigned char* s_fontData;
static int            s_fontOk;
static int            s_fontsTried;

static GLuint horde_make_shader(GLenum type, const char* src, const char* what)
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
        SH_DBG("[HORDEHUD] %s shader failed: %s", what, log);
        glDeleteShader(sh);
        return 0;
    }
    return sh;
}

/* ------------------------------------------------------------------ */
/* Glyph atlas (same design as pc_quick_options.c)                     */
/* ------------------------------------------------------------------ */

#define HORDE_ATLAS_W   2048
#define HORDE_ATLAS_H   1024
#define HORDE_ATLAS_PAD 2
#define HORDE_SLOT_MAX  64

/* Slot 0 is a permanent opaque white texel: panel bodies, borders, dim. */
#define HORDE_TEX_WHITE ((GLuint)1)

static GLuint s_atlasTex;
static int    s_atlasReady;
static int    s_atlasX, s_atlasY, s_atlasRowH;
static struct { int x, y, w, h, cw, ch, free; } s_slot[HORDE_SLOT_MAX];
static int    s_slotCount;
static int    s_atlasExhausted;

static void horde_atlas_reset(void)
{
    s_atlasX         = HORDE_ATLAS_PAD;
    s_atlasY         = HORDE_ATLAS_PAD;
    s_atlasRowH      = 0;
    s_slotCount      = 0;
    s_atlasExhausted = 0;
    if (s_atlasTex)
    {
        s_slot[0].x = HORDE_ATLAS_PAD; s_slot[0].y = HORDE_ATLAS_PAD;
        s_slot[0].w = 1;               s_slot[0].h = 1;
        s_slot[0].cw = 1;              s_slot[0].ch = 1;
        s_slot[0].free = 0;
        s_slotCount = 1;
        s_atlasX    = HORDE_ATLAS_PAD + 1 + HORDE_ATLAS_PAD;
        s_atlasRowH = 1;
    }
}

static int horde_atlas_ensure(void)
{
    GLint prevTex = 0, prevAlign = 4;

    if (s_atlasReady)
        return s_atlasTex != 0;

    s_atlasReady = 1;
    glGenTextures(1, &s_atlasTex);
    if (!s_atlasTex)
        return 0;

    glGetIntegerv(GL_TEXTURE_BINDING_2D, &prevTex);
    glGetIntegerv(GL_UNPACK_ALIGNMENT, &prevAlign);
    glBindTexture(GL_TEXTURE_2D, s_atlasTex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    /* No mip chain: an incomplete texture samples opaque black. */
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, HORDE_ATLAS_W, HORDE_ATLAS_H, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    {
        const unsigned char one[4] = { 255, 255, 255, 255 };
        glTexSubImage2D(GL_TEXTURE_2D, 0, HORDE_ATLAS_PAD, HORDE_ATLAS_PAD, 1, 1,
                        GL_RGBA, GL_UNSIGNED_BYTE, one);
    }
    glBindTexture(GL_TEXTURE_2D, (GLuint)prevTex);
    glPixelStorei(GL_UNPACK_ALIGNMENT, prevAlign);
    g_lastBoundTexture = (unsigned int)-1;

    horde_atlas_reset();
    SH_DBG("[HORDEHUD] glyph atlas %dx%d id=%u", HORDE_ATLAS_W, HORDE_ATLAS_H, (unsigned)s_atlasTex);
    return 1;
}

/* Returns a slot handle (index + 1), or 0 if it will not fit. Recycles the
 * best-fitting freed slot first: a re-baked counter is nearly always the size
 * of the one it replaces. */
static GLuint horde_upload_rgba(const unsigned char* rgba, int w, int h)
{
    int idx;

    if (!horde_atlas_ensure() || w <= 0 || h <= 0)
        return 0;
    if (w > HORDE_ATLAS_W - 2 * HORDE_ATLAS_PAD || h > HORDE_ATLAS_H - 2 * HORDE_ATLAS_PAD)
        return 0;

    {
        int bestIdx = -1, bestArea = 0;
        for (idx = 1; idx < s_slotCount; idx++)
        {
            int area;
            if (!s_slot[idx].free || s_slot[idx].cw < w || s_slot[idx].ch < h)
                continue;
            area = s_slot[idx].cw * s_slot[idx].ch;
            if (bestIdx < 0 || area < bestArea) { bestIdx = idx; bestArea = area; }
        }
        if (bestIdx >= 0)
        {
            s_slot[bestIdx].w    = w;
            s_slot[bestIdx].h    = h;
            s_slot[bestIdx].free = 0;
            glBindTexture(GL_TEXTURE_2D, s_atlasTex);
            glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
            glTexSubImage2D(GL_TEXTURE_2D, 0, s_slot[bestIdx].x, s_slot[bestIdx].y,
                            w, h, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
            return (GLuint)(bestIdx + 1);
        }
    }

    if (s_atlasX + w + HORDE_ATLAS_PAD > HORDE_ATLAS_W)
    {
        s_atlasX     = HORDE_ATLAS_PAD;
        s_atlasY    += s_atlasRowH + HORDE_ATLAS_PAD;
        s_atlasRowH  = 0;
    }
    /* Out of room: the next Draw drops every handle and re-bakes from an
     * empty atlas (one frame, always recovers). */
    if (s_slotCount >= HORDE_SLOT_MAX ||
        s_atlasY + h + HORDE_ATLAS_PAD > HORDE_ATLAS_H)
    {
        if (!s_atlasExhausted)
            SH_DBG("[HORDEHUD] atlas exhausted (%d slots) -- rebuilding", s_slotCount);
        s_atlasExhausted = 1;
        return 0;
    }

    idx = s_slotCount++;
    s_slot[idx].x  = s_atlasX;
    s_slot[idx].y  = s_atlasY;
    s_slot[idx].w  = w;
    s_slot[idx].h  = h;
    s_slot[idx].cw = w;
    s_slot[idx].ch = h;
    s_slot[idx].free = 0;

    glBindTexture(GL_TEXTURE_2D, s_atlasTex);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexSubImage2D(GL_TEXTURE_2D, 0, s_atlasX, s_atlasY, w, h,
                    GL_RGBA, GL_UNSIGNED_BYTE, rgba);

    s_atlasX += w + HORDE_ATLAS_PAD;
    if (h > s_atlasRowH)
        s_atlasRowH = h;

    return (GLuint)(idx + 1);
}

/* Never glDeleteTextures: the slot just goes back to the allocator. */
static void horde_retire(GLuint tex)
{
    int idx = (int)tex - 1;

    if (idx > 0 && idx < s_slotCount)
        s_slot[idx].free = 1;
}

/* Every cached handle refers to the atlas layout; drop them all together. */
static void horde_drop_all(void)
{
    int i;

    s_texPanel = 0;
    s_texDmg   = 0;
    s_texLife  = 0;
    s_bakedOnce = 0;
    s_texHead = 0;
    s_texHint = 0;
    s_texCursor = 0;
    for (i = 0; i < 3; i++)
    {
        s_texTitle[i] = 0;
        s_texSub[i]   = 0;
    }
    s_draftBaked = 0;
    horde_atlas_reset();
}

/* Before anything binds the atlas: a freed name would be re-created EMPTY by
 * the bind and glIsTexture could no longer tell. */
static int horde_atlas_check(void)
{
    if (s_atlasTex != 0 && !glIsTexture(s_atlasTex))
    {
        static int s_lostLogs = 0;
        if (s_lostLogs < 8)
        {
            s_lostLogs++;
            SH_DBG("[HORDEHUD] atlas texture %u was deleted by something else -- recreating",
                   (unsigned)s_atlasTex);
        }
        s_atlasTex   = 0;
        s_atlasReady = 0;
        horde_drop_all();
    }
    if (!horde_atlas_ensure())
        return 0;
    if (s_atlasExhausted)
        horde_drop_all();
    return 1;
}

static void horde_gl_init(void)
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

    vs = horde_make_shader(GL_VERTEX_SHADER, vs_src, "vertex");
    fs = horde_make_shader(GL_FRAGMENT_SHADER, fs_src, "fragment");
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
        SH_DBG("[HORDEHUD] program link failed: %s", log);
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

    horde_atlas_ensure(); /* one texture, created here, never recreated */

    s_glReady = 1;
}

/* Claim the program and the atlas on the first frame the GL context exists,
 * before map loads churn PsyCross framebuffer targets (see
 * Pc_QuickOptions_PreloadGL for the failure this avoids). */
void Pc_HordeHud_PreloadGL(void)
{
    if (!s_glReady) horde_gl_init();
}

/* Quad in NDC (yTop > yBot). `tex` is an atlas slot handle. */
static void horde_quad(GLuint tex, float x0, float yTop, float x1, float yBot,
                       float r, float g, float b, float alpha)
{
    float v[6][4];
    float u0, v0, u1, v1;
    int   idx = (int)tex - 1;

    if (tex == 0 || idx < 0 || idx >= s_slotCount || !s_atlasTex)
        return;

    /* Half-texel inset keeps LINEAR taps out of the neighbours' padding. */
    u0 = ((float)s_slot[idx].x + 0.5f) / (float)HORDE_ATLAS_W;
    v0 = ((float)s_slot[idx].y + 0.5f) / (float)HORDE_ATLAS_H;
    u1 = ((float)(s_slot[idx].x + s_slot[idx].w) - 0.5f) / (float)HORDE_ATLAS_W;
    v1 = ((float)(s_slot[idx].y + s_slot[idx].h) - 0.5f) / (float)HORDE_ATLAS_H;

    v[0][0] = x0; v[0][1] = yTop; v[0][2] = u0; v[0][3] = v0;
    v[1][0] = x0; v[1][1] = yBot; v[1][2] = u0; v[1][3] = v1;
    v[2][0] = x1; v[2][1] = yTop; v[2][2] = u1; v[2][3] = v0;
    v[3][0] = x1; v[3][1] = yTop; v[3][2] = u1; v[3][3] = v0;
    v[4][0] = x0; v[4][1] = yBot; v[4][2] = u0; v[4][3] = v1;
    v[5][0] = x1; v[5][1] = yBot; v[5][2] = u1; v[5][3] = v1;

    glUniform4f(s_locColor, r, g, b, alpha);
    glBindTexture(GL_TEXTURE_2D, s_atlasTex);
    glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(v), v);
    glDrawArrays(GL_TRIANGLES, 0, 6);
}

/* Saved GL state, shared by both draws. */
typedef struct
{
    GLint prevProg, prevVao, prevBuf, prevTex, prevUnit, prevAlign;
    GLint prevSrcRgb, prevDstRgb, prevSrcA, prevDstA, prevEqRgb, prevEqA;
    GLboolean prevBlend, prevDepth, prevCull;
} s_HordeGlState;

static void horde_gl_save(s_HordeGlState* st)
{
    glGetIntegerv(GL_CURRENT_PROGRAM, &st->prevProg);
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &st->prevVao);
    glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &st->prevBuf);
    glGetIntegerv(GL_ACTIVE_TEXTURE, &st->prevUnit);
    glActiveTexture(GL_TEXTURE0);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &st->prevTex);
    glGetIntegerv(GL_UNPACK_ALIGNMENT, &st->prevAlign);
    glGetIntegerv(GL_BLEND_SRC_RGB,   &st->prevSrcRgb);
    glGetIntegerv(GL_BLEND_DST_RGB,   &st->prevDstRgb);
    glGetIntegerv(GL_BLEND_SRC_ALPHA, &st->prevSrcA);
    glGetIntegerv(GL_BLEND_DST_ALPHA, &st->prevDstA);
    glGetIntegerv(GL_BLEND_EQUATION_RGB,   &st->prevEqRgb);
    glGetIntegerv(GL_BLEND_EQUATION_ALPHA, &st->prevEqA);
    st->prevBlend = glIsEnabled(GL_BLEND);
    st->prevDepth = glIsEnabled(GL_DEPTH_TEST);
    st->prevCull  = glIsEnabled(GL_CULL_FACE);
}

static void horde_gl_restore(const s_HordeGlState* st)
{
    glBindVertexArray((GLuint)st->prevVao);
    glBindBuffer(GL_ARRAY_BUFFER, (GLuint)st->prevBuf);
    glBindTexture(GL_TEXTURE_2D, (GLuint)st->prevTex);
    glActiveTexture((GLenum)st->prevUnit);
    glUseProgram((GLuint)st->prevProg);
    glPixelStorei(GL_UNPACK_ALIGNMENT, st->prevAlign);
    if (st->prevBlend) glEnable(GL_BLEND); else glDisable(GL_BLEND);
    if (st->prevDepth) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST);
    if (st->prevCull) glEnable(GL_CULL_FACE); else glDisable(GL_CULL_FACE);
    glBlendFuncSeparate((GLenum)st->prevSrcRgb, (GLenum)st->prevDstRgb,
                        (GLenum)st->prevSrcA,   (GLenum)st->prevDstA);
    glBlendEquationSeparate((GLenum)st->prevEqRgb, (GLenum)st->prevEqA);

    /* PsyCross skips GR_SetTexture binds it believes are current; we bound
     * our atlas behind that cache, so force its next bind to be real. */
    g_lastBoundTexture = (unsigned int)-1;
}

/* Bind our program and re-assert the vertex layout (never trust the VAO). */
static void horde_gl_begin(void)
{
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
}

static unsigned char* horde_read_file(const char* path, size_t* outLen)
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

static void horde_fonts_init(void)
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
        unsigned char* data = horde_read_file(paths[i], &len);
        if (!data)
            continue;
        if (stbtt_InitFont(&s_font, data, stbtt_GetFontOffsetForIndex(data, 0)))
        {
            s_fontData = data;
            s_fontOk   = 1;
            SH_DBG("[HORDEHUD] font: %s", paths[i]);
            return;
        }
        free(data);
    }
    SH_DBG("[HORDEHUD] no usable font - hud disabled");
}

static int horde_utf8_next(const char** p)
{
    const unsigned char* s = (const unsigned char*)*p;
    int cp;
    if (!*s) return 0;
    if (s[0] < 0x80)                       { cp = s[0];                                   *p += 1; }
    else if ((s[0] & 0xE0) == 0xC0 && s[1]) { cp = ((s[0] & 0x1F) << 6) | (s[1] & 0x3F);   *p += 2; }
    else if ((s[0] & 0xF0) == 0xE0 && s[1] && s[2])
                                              { cp = ((s[0] & 0x0F) << 12) | ((s[1] & 0x3F) << 6) | (s[2] & 0x3F); *p += 3; }
    else                                     { cp = '?'; *p += 1; }
    return cp;
}

/* White outlined text composited over a dark pill. Straight-alpha "over":
 * out = text*a + pill*(1-a) so the standard blend reads it back exactly.
 * Returns an atlas slot handle. Call only between horde_gl_save/restore. */
static GLuint horde_bake(const char* text, float px, int* outW, int* outH)
{
    float scale, widest = 0.0f;
    const char* p;
    int prev = 0, cp, outline, pad, W, H, x, y, ox, oy, i;
    unsigned char *cov, *dil, *rgba, *scratch;
    GLuint tex;
    int asc, desc, gap;
    float penX, baseY;
    const float pillA = 200.0f / 255.0f;

    if (!s_fontOk)
        return 0;
    scale = stbtt_ScaleForPixelHeight(&s_font, px);
    p = text;
    while ((cp = horde_utf8_next(&p)) != 0)
    {
        int adv, lsb;
        stbtt_GetCodepointHMetrics(&s_font, cp, &adv, &lsb);
        if (prev)
            widest += stbtt_GetCodepointKernAdvance(&s_font, prev, cp) * scale;
        widest += adv * scale;
        prev = cp;
    }
    if (widest < 1.0f)
        return 0;

    outline = (int)(px / 22.0f);
    if (outline < 2) outline = 2;
    pad = outline + 8;

    stbtt_GetFontVMetrics(&s_font, &asc, &desc, &gap);
    W = (int)ceilf(widest) + 2 * pad;
    H = (int)ceilf(px * 1.2f) + 2 * pad;
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

    penX = (float)pad;
    baseY = (float)pad + asc * scale;
    p = text;
    prev = 0;
    while ((cp = horde_utf8_next(&p)) != 0)
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
        if (penX > (float)(W - pad))
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
        float w  = (a > 0.0f) ? aT / a : 0.0f;
        float g  = 10.0f + (255.0f - 10.0f) * w;
        float b  = 10.0f + (255.0f - 10.0f) * w;
        float r  = 12.0f + (255.0f - 12.0f) * w;
        float oa = a + pillA * (1.0f - a);
        rgba[i * 4 + 0] = (unsigned char)(r + 0.5f);
        rgba[i * 4 + 1] = (unsigned char)(g + 0.5f);
        rgba[i * 4 + 2] = (unsigned char)(b + 0.5f);
        rgba[i * 4 + 3] = (unsigned char)(255.0f * oa + 0.5f);
    }

    tex = horde_upload_rgba(rgba, W, H);
    if (tex)
    {
        if (outW) *outW = W;
        if (outH) *outH = H;
    }

    free(cov); free(dil); free(rgba); free(scratch);
    return tex;
}

static void horde_retire_all(void)
{
    horde_retire(s_texPanel);
    s_texPanel = 0;
    horde_retire(s_texDmg);
    s_texDmg = 0;
    horde_retire(s_texLife);
    s_texLife = 0;
}

/* Resistance as the player reads it: effective max health, pct (100/res). */
static unsigned int horde_life_pct(unsigned int resistPct)
{
    if (resistPct == 0)
        return 100;
    return (10000u + resistPct / 2u) / resistPct;
}

void Pc_HordeHud_Draw(void)
{
    GLint vp[4];
    float vpW, vpH;
    unsigned int wave, kills, threat, dmg, resist, life;
    int alive, cds, vph, es;
    char text[96];
    float left, top, tw, th;
    s_HordeGlState st;

    if (!Pc_Horde_Active() ||
        g_GameWork.gameState != GameState_InGame || g_SysWork.sysState != SysState_Gameplay)
        return;

    wave   = Pc_Horde_Wave();
    alive  = Pc_Horde_Alive();
    kills  = Pc_Horde_Kills();
    threat = Pc_Horde_ThreatPct();
    dmg    = Pc_Horde_PlayerDmgPct();
    resist = Pc_Horde_PlayerResistPct();
    cds    = Pc_Horde_CountdownSecs();
    es     = horde_lang_es();

    glGetIntegerv(GL_VIEWPORT, vp);
    vpW = (float)vp[2];
    vpH = (float)vp[3];
    if (vp[2] <= 0 || vp[3] <= 0)
        return;
    vph = vp[3];

    if (!s_glReady)
        horde_gl_init();
    if (s_glReady != 1)
        return;

    if (!s_fontsTried)
        horde_fonts_init();
    if (!s_fontOk)
        return;

    horde_gl_save(&st);
    if (!horde_atlas_check())
        goto restore;

    if (!s_bakedOnce || wave != s_bakedWave || alive != s_bakedAlive ||
        kills != s_bakedKills || threat != s_bakedThreat || cds != s_bakedCds ||
        dmg != s_bakedDmg || resist != s_bakedResist || es != s_bakedLang ||
        (vph < s_bakedVpH - 2 || vph > s_bakedVpH + 2))
    {
        horde_retire_all();
    }
    if (!s_texPanel)
    {
        if (cds >= 0)
            snprintf(text, sizeof(text),
                     es ? "OLEADA %u EN %d  /  KILLS %u  /  AMENAZA x%u.%02u"
                        : "WAVE %u IN %d  /  KILLS %u  /  THREAT x%u.%02u",
                     wave + 1, cds, kills, threat / 100, threat % 100);
        else
            snprintf(text, sizeof(text),
                     es ? "OLEADA %u  /  VIVOS %d  /  KILLS %u  /  AMENAZA x%u.%02u"
                        : "WAVE %u  /  ALIVE %d  /  KILLS %u  /  THREAT x%u.%02u",
                     wave, alive, kills, threat / 100, threat % 100);
        s_texPanel = horde_bake(text, 0.030f * vpH, &s_texW, &s_texH);
        snprintf(text, sizeof(text), es ? "DA\xC3\x91O x%u.%02u" : "DAMAGE x%u.%02u",
                 dmg / 100u, dmg % 100u);
        s_texDmg = horde_bake(text, 0.026f * vpH, &s_dmgW, &s_dmgH);
        life = horde_life_pct(resist);
        snprintf(text, sizeof(text), es ? "VIDA x%u.%02u" : "HEALTH x%u.%02u",
                 life / 100u, life % 100u);
        s_texLife = horde_bake(text, 0.026f * vpH, &s_lifeW, &s_lifeH);
        if (s_texPanel && s_texDmg && s_texLife)
        {
            s_bakedWave = wave; s_bakedAlive = alive; s_bakedKills = kills;
            s_bakedThreat = threat; s_bakedCds = cds; s_bakedVpH = vph;
            s_bakedDmg = dmg; s_bakedResist = resist; s_bakedLang = es;
            s_bakedOnce = 1;
        }
    }
    if (!s_texPanel || !s_texDmg || !s_texLife)
        goto restore;

    left = (vpW - (float)s_texW) * 0.5f;
    top  = vpH * 0.965f;

    horde_gl_begin();

#define NX(px_) (-1.0f + 2.0f * (px_) / vpW)
#define NY(py_) (-1.0f + 2.0f * (py_) / vpH)

    tw = (float)s_texW;
    th = (float)s_texH;
    horde_quad(s_texPanel, NX(left), NY(top), NX(left + tw), NY(top - th),
               1.0f, 1.0f, 1.0f, 0.95f);

    {
        float dl = vpW * 0.035f;
        float dt = vpH * 0.5f + (float)s_dmgH * 0.5f;
        float dw = (float)s_dmgW;
        float dh = (float)s_dmgH;
        horde_quad(s_texDmg, NX(dl), NY(dt), NX(dl + dw), NY(dt - dh),
                   1.0f, 0.85f, 0.40f, 0.95f);
        /* Effective max health right under the damage line. */
        dt -= dh;
        horde_quad(s_texLife, NX(dl), NY(dt), NX(dl + (float)s_lifeW), NY(dt - (float)s_lifeH),
                   0.55f, 0.95f, 0.60f, 0.95f);
    }

#undef NX
#undef NY

restore:
    horde_gl_restore(&st);
}

/* ------------------------------------------------------------------ */
/* Inter-wave reward draft. Cards reuse the single-line bake above     */
/* (white text, tinted per-frame at draw) plus solid atlas quads for   */
/* bodies and borders. Input follows the F10 panel (pc_quick_options). */
/* ------------------------------------------------------------------ */

static float s_draftX[3], s_draftY[3], s_draftW[3], s_draftH[3];
static int   s_draftGeo;

static int s_draftSel = 1;
static int s_draftWas;
static Uint32 s_draftOpenTick;
static int s_draftFrozen;

/* No pick before this: the shot that killed the last enemy, or a mashed
 * attack button, must never choose a card blind. */
#define DRAFT_ARM_MS 500u

static GLuint draft_bake_fit(const char* text, float px, float maxW, int* outW, int* outH)
{
    GLuint t = 0;
    int tries = 0;

    while (tries < 5)
    {
        t = horde_bake(text, px, outW, outH);
        if (!t || (float)*outW <= maxW)
            break;
        horde_retire(t);
        t = 0;
        px *= 0.9f;
        tries++;
    }
    return t;
}

/* Hands the text slots back. The cursor sprite slot is kept: it never
 * changes. */
static void draft_free_all(void)
{
    int i;
    horde_retire(s_texHead);
    s_texHead = 0;
    horde_retire(s_texHint);
    s_texHint = 0;
    for (i = 0; i < 3; i++)
    {
        horde_retire(s_texTitle[i]); s_texTitle[i] = 0;
        horde_retire(s_texSub[i]);   s_texSub[i]   = 0;
    }
    s_draftBaked = 0;
    s_draftGeo   = 0;
}

/* Directions arrive as HELD state and repeat on the wall clock (same timing
 * as the F10 panel). One lane per direction over pad + keyboard. */
#define DRAFT_REPEAT_FIRST_MS 380u
#define DRAFT_REPEAT_MS       85u

static int s_repWasHeld[2];
static Uint32 s_repNextAt[2];
static int s_prevConfirm;

static int draft_repeat(int idx, int held, Uint32 now)
{
    int fire = 0;

    if (held && !s_repWasHeld[idx])
    {
        fire = 1;
        s_repNextAt[idx] = now + DRAFT_REPEAT_FIRST_MS;
    }
    else if (held && (Sint32)(now - s_repNextAt[idx]) >= 0)
    {
        fire = 1;
        s_repNextAt[idx] = now + DRAFT_REPEAT_MS;
    }
    s_repWasHeld[idx] = held;
    return fire;
}

void Pc_HordeDraft_Update(int up, int down, int left, int right, int confirm, int cancel)
{
    float mx, my;
    int i, armed, moved = 0, conf, confEdge, wheel;
    Uint32 now = SDL_GetTicks();
    const Uint8* keys = SDL_GetKeyboardState(NULL);
    int kbLeft, kbRight, kbConfirm;

    (void)up; (void)down; (void)cancel;

    if (!Pc_Horde_DraftActive())
    {
        s_draftWas = 0;
        return;
    }
    if (!s_draftWas)
    {
        s_draftWas      = 1;
        s_draftOpenTick = now;
        s_draftSel      = 1;
        /* A confirm already held when the draft opens (attack button) must
         * be released and pressed again before it can pick. */
        s_prevConfirm   = 1;
    }
    armed = (now - s_draftOpenTick) >= DRAFT_ARM_MS;

    /* Keyboard never reaches the pad flags the caller passes: read it raw.
     * A/D + arrows move, C/Enter/Space confirm. */
    kbLeft    = (keys && (keys[SDL_SCANCODE_A] || keys[SDL_SCANCODE_LEFT])) ? 1 : 0;
    kbRight   = (keys && (keys[SDL_SCANCODE_D] || keys[SDL_SCANCODE_RIGHT])) ? 1 : 0;
    kbConfirm = (keys && (keys[SDL_SCANCODE_C] || keys[SDL_SCANCODE_RETURN] || keys[SDL_SCANCODE_SPACE])) ? 1 : 0;

    if (draft_repeat(0, left || kbLeft, now))
    {
        s_draftSel = (s_draftSel + 2) % 3;
        moved = 1;
    }
    if (draft_repeat(1, right || kbRight, now))
    {
        s_draftSel = (s_draftSel + 1) % 3;
        moved = 1;
    }

    /* Wheel steps the selection: up = left, down = right. */
    wheel = Pc_MouseCursor_WheelStep();
    if (wheel)
    {
        s_draftSel = (s_draftSel + (wheel > 0 ? 2 : 1)) % 3;
        moved = 1;
    }

    /* Mouse: hover selects only when the pointer actually MOVES, so a
     * pointer resting over a card never fights the pad or the keyboard;
     * left click on a card picks it. */
    if (s_draftGeo && s_draftVpW > 0 && s_draftVpH > 0 && Pc_MouseCursor_ViewportPos(&mx, &my))
    {
        float px = mx * (float)s_draftVpW;
        float py = (1.0f - my) * (float)s_draftVpH;
        int hit = -1;

        for (i = 0; i < 3; i++)
        {
            if (px >= s_draftX[i] && px < s_draftX[i] + s_draftW[i] &&
                py >= s_draftY[i] && py < s_draftY[i] + s_draftH[i])
            {
                hit = i;
                break;
            }
        }
        if (hit >= 0)
        {
            if (Pc_MouseCursor_Moved() && s_draftSel != hit)
            {
                s_draftSel = hit;
                moved = 1;
            }
            if (armed && Pc_MouseCursor_LeftClicked())
            {
                s_draftSel = hit;
                Pc_Horde_DraftPick(hit);
                return;
            }
        }
    }

    if (moved)
        Sd_PlaySfx(Sfx_MenuMove, 0, 64);

    /* Confirm is a rising edge over pad + keyboard, tracked every frame
     * (armed or not): held confirm never picks, and never double-picks. */
    conf     = (confirm || kbConfirm) ? 1 : 0;
    confEdge = conf && !s_prevConfirm;
    s_prevConfirm = conf;
    if (armed && confEdge)
        Pc_Horde_DraftPick(s_draftSel);
}

void Pc_HordeDraft_Draw(void)
{
    GLint vp[4];
    float vpW, vpH;
    int vph, i, es;
    unsigned int wave;
    float cardW, cardH, gap, totalW, x0, y0;
    char head[64];
    s_HordeGlState st;

    if (!Pc_Horde_DraftActive())
    {
        if (s_draftBaked || s_texHead || s_texHint || s_texTitle[0] || s_texTitle[1] || s_texTitle[2])
            draft_free_all();
        s_draftWas = 0;
        if (s_draftFrozen)
        {
            s_draftFrozen = 0;
            g_Player_DisableControl = 0;
        }
        return;
    }
    if (!s_draftFrozen)
    {
        s_draftFrozen = 1;
        g_Player_DisableControl = 1;
    }

    if (g_GameWork.gameState != GameState_InGame || g_SysWork.sysState != SysState_Gameplay)
        return;

    glGetIntegerv(GL_VIEWPORT, vp);
    vpW = (float)vp[2];
    vpH = (float)vp[3];
    if (vp[2] <= 0 || vp[3] <= 0)
        return;
    vph = vp[3];

    if (!s_glReady)
        horde_gl_init();
    if (s_glReady != 1)
        return;

    if (!s_fontsTried)
        horde_fonts_init();
    if (!s_fontOk)
        return;

    horde_gl_save(&st);
    if (!horde_atlas_check())
        goto restore;

    cardW = vpW * 0.26f;
    cardH = vpH * 0.30f;

    wave = Pc_Horde_Wave();
    es   = horde_lang_es();
    if (!s_draftBaked || wave != s_draftWave || es != s_draftLang ||
        (vph < s_draftVpH - 2 || vph > s_draftVpH + 2))
    {
        draft_free_all();

        snprintf(head, sizeof(head),
                 es ? "OLEADA %u SUPERADA - ELIGE RECOMPENSA" : "WAVE %u CLEARED - CHOOSE A REWARD", wave);
        s_texHead = draft_bake_fit(head, vpH * 0.028f, vpW * 0.7f, &s_headW, &s_headH);
        s_texHint = draft_bake_fit(es ? "IZQ / DER  ELEGIR     ENTER / CLIC  CONFIRMAR"
                                      : "LEFT / RIGHT  SELECT     ENTER / CLICK  CONFIRM",
                                   vpH * 0.020f, vpW * 0.7f, &s_hintW, &s_hintH);
        for (i = 0; i < 3; i++)
        {
            int kind, item;
            unsigned int pct;
            char title[64], sub[96];
            unsigned int cur, res;

            if (!Pc_Horde_DraftOffer(i, &kind, &item, &pct))
                continue;
            if (kind == HordeOffer_Dmg)
            {
                cur = Pc_Horde_PlayerDmgPct();
                res = cur * pct / 100u;
                snprintf(title, sizeof(title), "%s", es ? "MEJORA DE DA\xC3\x91O" : "DAMAGE UP");
                snprintf(sub, sizeof(sub), es ? "x%u.%02u  (AHORA x%u.%02u)" : "x%u.%02u  (NOW x%u.%02u)",
                         pct / 100u, pct % 100u, res / 100u, res % 100u);
            }
            else if (kind == HordeOffer_Resist)
            {
                unsigned int after;

                cur = Pc_Horde_PlayerResistPct();
                res = cur * pct / 100u;
                if (res < 20u)
                    res = 20u;
                after = horde_life_pct(res);
                snprintf(title, sizeof(title), "%s", es ? "VIDA M\xC3\x81XIMA" : "MAX HEALTH");
                snprintf(sub, sizeof(sub), es ? "+%u%%  (AHORA x%u.%02u)" : "+%u%%  (NOW x%u.%02u)",
                         horde_life_pct(pct) - 100u, after / 100u, after % 100u);
            }
            else
            {
                Pc_ItemToast_Name((unsigned char)item, title, (int)sizeof(title));
                if (!title[0])
                    snprintf(title, sizeof(title), "?");
                if (kind == HordeOffer_Gun)
                    snprintf(sub, sizeof(sub), "%s", es ? "FUEGO + CARGADOR" : "GUN + AMMO");
                else if (kind == HordeOffer_Heal)
                    snprintf(sub, sizeof(sub), es ? "CURACI\xC3\x93N  x%u" : "HEALING  x%u", pct);
                else if (kind == HordeOffer_Ammo)
                    snprintf(sub, sizeof(sub), es ? "MUNICI\xC3\x93N  +%u" : "AMMO  +%u", pct);
                else
                    snprintf(sub, sizeof(sub), "%s", es ? "CUERPO A CUERPO" : "MELEE");
            }
            s_texTitle[i] = draft_bake_fit(title, cardH * 0.13f, cardW - 32.0f,
                                           &s_titleW[i], &s_titleH[i]);
            s_texSub[i]   = draft_bake_fit(sub, cardH * 0.085f, cardW - 32.0f,
                                           &s_subW[i], &s_subH[i]);
        }
        {
            int ok = (s_texHead != 0);
            for (i = 0; i < 3; i++)
                if (!s_texTitle[i] || !s_texSub[i]) ok = 0;
            if (ok)
            {
                s_draftWave = wave; s_draftVpH = vph; s_draftLang = es;
                s_draftBaked = 1;
            }
        }
    }
    if (!s_draftBaked)
        goto restore;

    gap = vpW * 0.02f;
    totalW = cardW * 3.0f + gap * 2.0f;
    x0 = (vpW - totalW) * 0.5f;
    y0 = (vpH - cardH) * 0.5f;
    s_draftVpW = vp[2];
    s_draftVpH = vp[3];
    for (i = 0; i < 3; i++)
    {
        s_draftX[i] = x0 + (float)i * (cardW + gap);
        s_draftY[i] = y0;
        s_draftW[i] = cardW;
        s_draftH[i] = cardH;
    }
    s_draftGeo = 1;

    horde_gl_begin();

#define NX(px_) (-1.0f + 2.0f * (px_) / vpW)
#define NY(py_) (-1.0f + 2.0f * (py_) / vpH)

    horde_quad(HORDE_TEX_WHITE, -1.0f, 1.0f, 1.0f, -1.0f, 0.0f, 0.0f, 0.0f, 0.55f);
    if (s_texHead)
    {
        float hx = (vpW - (float)s_headW) * 0.5f;
        float hy = y0 - (float)s_headH - vpH * 0.03f;
        horde_quad(s_texHead, NX(hx), NY(hy + (float)s_headH), NX(hx + (float)s_headW), NY(hy),
                   1.0f, 1.0f, 1.0f, 1.0f);
    }
    for (i = 0; i < 3; i++)
    {
        float cx = s_draftX[i], cy = s_draftY[i];
        float bt = (vpH * 0.004f < 2.0f) ? 2.0f : vpH * 0.004f;
        float br = (i == s_draftSel) ? 0.92f : 0.47f;
        float bg = (i == s_draftSel) ? 0.82f : 0.47f;
        float bb = (i == s_draftSel) ? 0.35f : 0.51f;
        float tx, ty, sx, sy;

        horde_quad(HORDE_TEX_WHITE, NX(cx), NY(cy + cardH), NX(cx + cardW), NY(cy),
                   0.039f, 0.039f, 0.055f, 0.88f);
        if (i == s_draftSel)
            horde_quad(HORDE_TEX_WHITE, NX(cx), NY(cy + cardH), NX(cx + cardW), NY(cy),
                       0.42f, 0.16f, 0.12f, 0.35f);
        horde_quad(HORDE_TEX_WHITE, NX(cx), NY(cy + bt), NX(cx + cardW), NY(cy), br, bg, bb, 1.0f);
        horde_quad(HORDE_TEX_WHITE, NX(cx), NY(cy + cardH), NX(cx + cardW), NY(cy + cardH - bt), br, bg, bb, 1.0f);
        horde_quad(HORDE_TEX_WHITE, NX(cx), NY(cy + cardH), NX(cx + bt), NY(cy), br, bg, bb, 1.0f);
        horde_quad(HORDE_TEX_WHITE, NX(cx + cardW - bt), NY(cy + cardH), NX(cx + cardW), NY(cy), br, bg, bb, 1.0f);

        if (s_texTitle[i])
        {
            tx = cx + (cardW - (float)s_titleW[i]) * 0.5f;
            ty = cy + cardH * 0.30f;
            if (i == s_draftSel)
                horde_quad(s_texTitle[i], NX(tx), NY(ty + (float)s_titleH[i]), NX(tx + (float)s_titleW[i]), NY(ty),
                           1.0f, 0.86f, 0.40f, 1.0f);
            else
                horde_quad(s_texTitle[i], NX(tx), NY(ty + (float)s_titleH[i]), NX(tx + (float)s_titleW[i]), NY(ty),
                           1.0f, 1.0f, 1.0f, 1.0f);
        }
        if (s_texSub[i])
        {
            sx = cx + (cardW - (float)s_subW[i]) * 0.5f;
            sy = cy + cardH * 0.62f;
            horde_quad(s_texSub[i], NX(sx), NY(sy + (float)s_subH[i]), NX(sx + (float)s_subW[i]), NY(sy),
                       0.66f, 0.66f, 0.68f, 1.0f);
        }
    }
    if (s_texHint)
    {
        float hx = (vpW - (float)s_hintW) * 0.5f;
        float hy = y0 - (float)s_headH - vpH * 0.03f - (float)s_hintH - vpH * 0.01f;
        horde_quad(s_texHint, NX(hx), NY(hy + (float)s_hintH), NX(hx + (float)s_hintW), NY(hy),
                   0.75f, 0.75f, 0.80f, 1.0f);
    }

    /* Mouse cursor last, so it rides above the cards. Uploaded once into the
     * atlas from the game's own BG_ETC arrow sprite. */
    if (!s_texCursor)
    {
        unsigned char rgba[32 * 32 * 4];
        if (Pc_MouseCursor_SpriteRgba(rgba))
            s_texCursor = horde_upload_rgba(rgba, 32, 32);
    }
    if (s_texCursor)
    {
        float cx, cy, cw, ch;
        if (Pc_MouseCursor_GlRect(vpW, vpH, &cx, &cy, &cw, &ch))
            horde_quad(s_texCursor, NX(cx), NY(cy), NX(cx + cw), NY(cy - ch), 1.0f, 1.0f, 1.0f, 1.0f);
    }

#undef NX
#undef NY

restore:
    horde_gl_restore(&st);
}
