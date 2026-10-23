/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * pc_item_toast.c - fast item-pickup toast (RE4R/SH2R style side panel).
 *
 * Only for takes that skip the animated take screen. Wall-clock timing, own
 * GL program/VAO/VBO/textures with full state save/restore, drawn from the
 * post-capture hook next to the achievement toast.
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
#include "bodyprog/sound/sound_system.h"
#include "bodyprog/sound/sfx_id_enum.h"
#include "sh_log.h"
#include "pc_config.h"
#include "pc_item_toast.h"
#include "pc_ui_sound.h"

#include "stb_truetype.h"

typedef enum
{
    IP_IN = 0,
    IP_HOLD,
    IP_OUT,
    IP_COUNT
} e_ItemPhase;

static const Uint32 IP_MS[IP_COUNT] = { 250, 2600, 300 };

#define ITEM_NAME_MAX  96
#define ITEM_SUB_MAX   64

typedef struct
{
    char name[ITEM_NAME_MAX];
    char sub[ITEM_SUB_MAX];
} s_ItemToast;

#define ITEM_STACK_MAX 7
#define ITEM_VISIBLE_MAX 3

typedef struct
{
    s_ItemToast item;
    int         active;
    int         fading;
    Uint32      start;
    GLuint      texText;
    int         texTextW, texTextH;
    GLuint      texPanel;
    int         panelW, panelH;
    float       bakedScale;
    float       yCur;
} s_ItemSlot;

static s_ItemSlot s_slots[ITEM_STACK_MAX];

static GLuint s_prog, s_vao, s_vbo;
static GLint  s_locTex, s_locColor;
static int    s_glReady;

#define ITEM_GARBAGE_MAX 16
static GLuint s_garbage[ITEM_GARBAGE_MAX];
static int    s_garbageCount;

#define IFONT_NAME 0
#define IFONT_BODY 1
#define IFONT_COUNT 2

static stbtt_fontinfo s_font[IFONT_COUNT];
static unsigned char* s_fontData[IFONT_COUNT];
static int            s_fontOk[IFONT_COUNT];
static int            s_fontsTried;

static GLuint item_make_shader(GLenum type, const char* src, const char* what)
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
        SH_DBG("[ITEMTOAST] %s shader failed: %s", what, log);
        glDeleteShader(sh);
        return 0;
    }
    return sh;
}

static void item_gl_init(void)
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

    vs = item_make_shader(GL_VERTEX_SHADER, vs_src, "vertex");
    fs = item_make_shader(GL_FRAGMENT_SHADER, fs_src, "fragment");
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
        SH_DBG("[ITEMTOAST] program link failed: %s", log);
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

static void item_quad(GLuint tex, float x0, float yTop, float x1, float yBot, float alpha)
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

static GLuint item_upload_rgba(const unsigned char* rgba, int w, int h)
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

static unsigned char* item_read_file(const char* path, size_t* outLen)
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

static void item_load_font(int idx, const char* const* paths, int pathCount)
{
    int i;
    for (i = 0; i < pathCount; i++)
    {
        size_t len = 0;
        unsigned char* data = item_read_file(paths[i], &len);
        if (!data)
            continue;
        if (stbtt_InitFont(&s_font[idx], data, stbtt_GetFontOffsetForIndex(data, 0)))
        {
            s_fontData[idx] = data;
            s_fontOk[idx]   = 1;
            SH_DBG("[ITEMTOAST] font %d: %s", idx, paths[i]);
            return;
        }
        free(data);
    }
    SH_DBG("[ITEMTOAST] font %d: no usable file - text disabled", idx);
}

static void item_fonts_init(void)
{
    static const char* namePaths[] = {
        "gamedata/font/Oswald-Regular.ttf",
        "C:/Windows/Fonts/segoeui.ttf",
        "C:/Windows/Fonts/arial.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
    };
    static const char* bodyPaths[] = {
        "gamedata/font/BarlowSemiCondensed-Regular.ttf",
        "C:/Windows/Fonts/segoeui.ttf",
        "C:/Windows/Fonts/arial.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
    };
    if (s_fontsTried)
        return;
    s_fontsTried = 1;
    item_load_font(IFONT_NAME, namePaths, (int)(sizeof(namePaths) / sizeof(namePaths[0])));
    item_load_font(IFONT_BODY, bodyPaths, (int)(sizeof(bodyPaths) / sizeof(bodyPaths[0])));
}

static int item_utf8_next(const char** p)
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

static float item_measure(int fontIdx, const char* text, float px)
{
    const char* p = text;
    float scale = stbtt_ScaleForPixelHeight(&s_font[fontIdx], px);
    float w = 0.0f;
    int   prev = 0, cp;
    while ((cp = item_utf8_next(&p)) != 0)
    {
        int adv, lsb;
        stbtt_GetCodepointHMetrics(&s_font[fontIdx], cp, &adv, &lsb);
        if (prev)
            w += stbtt_GetCodepointKernAdvance(&s_font[fontIdx], prev, cp) * scale;
        w += adv * scale;
        prev = cp;
    }
    return w;
}

typedef struct
{
    const char*   text;
    int           fontIdx;
    float         px;
    unsigned char tint;
} s_ItemLine;

static GLuint item_bake(const s_ItemLine* lines, int count, int maxW, int* outW, int* outH)
{
    int   i, x, y, ox, oy;
    int   outline;
    float totalH = 0.0f, widest = 0.0f;
    int   W, H;
    unsigned char *cov, *dil, *rgba, *scratch;
    GLuint tex;
    float  lineTop[4];
    float  linePx[4];

    if (count > 4) count = 4;
    for (i = 0; i < count; i++)
    {
        float px = lines[i].px;
        float w;
        if (!s_fontOk[lines[i].fontIdx])
            return 0;
        w = item_measure(lines[i].fontIdx, lines[i].text, px);
        if (maxW > 0 && w > (float)maxW && w > 1.0f)
        {
            float shrink = (float)maxW / w;
            if (shrink < 0.55f) shrink = 0.55f;
            px *= shrink;
            w = item_measure(lines[i].fontIdx, lines[i].text, px);
        }
        linePx[i]  = px;
        lineTop[i] = totalH;
        totalH += px * 1.20f;
        if (w > widest) widest = w;
    }
    if (widest < 1.0f || totalH < 1.0f)
        return 0;
    if (maxW > 0 && widest > (float)maxW) widest = (float)maxW;

    outline = (int)(lines[0].px / 22.0f);
    if (outline < 2) outline = 2;

    W = (int)ceilf(widest) + 2 * outline + 2;
    H = (int)ceilf(totalH) + 2 * outline + 2;
    if (W <= 0 || H <= 0 || W > 4096 || H > 1024)
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

    for (i = 0; i < count; i++)
    {
        const char* p     = lines[i].text;
        float       px    = linePx[i];
        float       scale = stbtt_ScaleForPixelHeight(&s_font[lines[i].fontIdx], px);
        int         asc, desc, gap, cp, prev = 0;
        float       penX = (float)outline;
        float       baseY;

        stbtt_GetFontVMetrics(&s_font[lines[i].fontIdx], &asc, &desc, &gap);
        baseY = (float)outline + lineTop[i] + asc * scale;

        while ((cp = item_utf8_next(&p)) != 0)
        {
            int   gx0, gy0, gx1, gy1, gw, gh, adv, lsb, sx, sy;
            float shiftX;

            if (prev)
                penX += stbtt_GetCodepointKernAdvance(&s_font[lines[i].fontIdx], prev, cp) * scale;
            shiftX = penX - floorf(penX);

            stbtt_GetCodepointBitmapBoxSubpixel(&s_font[lines[i].fontIdx], cp, scale, scale,
                                                shiftX, 0.0f, &gx0, &gy0, &gx1, &gy1);
            gw = gx1 - gx0;
            gh = gy1 - gy0;
            if (gw > 0 && gh > 0 && gw <= W && gh <= H)
            {
                memset(scratch, 0, (size_t)gw * gh);
                stbtt_MakeCodepointBitmapSubpixel(&s_font[lines[i].fontIdx], scratch,
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
            stbtt_GetCodepointHMetrics(&s_font[lines[i].fontIdx], cp, &adv, &lsb);
            penX += adv * scale;
            prev = cp;
            if (penX > (float)(W - outline))
                break;
        }
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

    tex = item_upload_rgba(rgba, W, H);
    if (outW) *outW = W;
    if (outH) *outH = H;

    free(cov); free(dil); free(rgba); free(scratch);
    return tex;
}

static void item_build_panel(int w, int h, GLuint* tex, int* pw, int* ph)
{
    unsigned char* px;
    int x, y;
    const float r = (float)h * 0.22f;

    if (*tex && *pw == w && *ph == h)
        return;
    if (*tex) { glDeleteTextures(1, tex); *tex = 0; }
    if (w <= 0 || h <= 0) return;

    px = (unsigned char*)calloc((size_t)w * h, 4);
    if (!px) return;

    for (y = 0; y < h; y++)
    {
        float fy = (float)y / (float)(h - 1);
        for (x = 0; x < w; x++)
        {
            float dx = 0.0f, dy = 0.0f, d, a;
            int   i = (y * w + x) * 4;
            unsigned char cr, cg, cb, ca;

            if (x < r)          dx = r - (float)x;
            else if (x > w - r) dx = (float)x - (w - r);
            if (y < r)          dy = r - (float)y;
            else if (y > h - r) dy = (float)y - (h - r);
            d = sqrtf(dx * dx + dy * dy);
            a = (d <= r) ? 1.0f : 0.0f;
            if (d > r - 1.0f && d <= r) a = r - d;

            cr = (unsigned char)(10 - 6 * fy);
            cg = (unsigned char)(10 - 6 * fy);
            cb = (unsigned char)(12 - 6 * fy);
            ca = (unsigned char)((205 + 18 * fy) * a);

            px[i + 0] = cr; px[i + 1] = cg; px[i + 2] = cb; px[i + 3] = ca;
        }
    }
    *tex = item_upload_rgba(px, w, h);
    *pw = w; *ph = h;
    free(px);
}

static PcUiSound* s_cueCustom;
static int        s_cueCustomTried;
static Uint32     s_lastCue;

static void item_sound_play(void)
{
    Uint32 now = SDL_GetTicks();
    /* Custom cues bypass the SPU, so they must scale by the native effects
     * level themselves (g_GameWork.config.volumeSe, 0..128) for coherence
     * with the rest of the game's audio. The Sd_PlaySfx fallback below
     * already scales through gSDVolConfig. */
    float seGain = (float)g_GameWork.config.volumeSe / 128.0f;
    if (now - s_lastCue < 90)
        return;
    s_lastCue = now;
    if (g_PcConfig.toastCue)
    {
        if (!s_cueCustomTried)
        {
            s_cueCustomTried = 1;
            s_cueCustom = PcUiSound_Load("gamedata/sound/custom/toast_cue.wav");
        }
        if (s_cueCustom)
        {
            PcUiSound_PlayGain(s_cueCustom, seGain);
            return;
        }
    }
    Sd_PlaySfx(Sfx_MenuConfirm, 0, 64);
}

static void item_retire(GLuint tex)
{
    if (!tex)
        return;
    if (s_garbageCount < ITEM_GARBAGE_MAX)
        s_garbage[s_garbageCount++] = tex;
    else
        glDeleteTextures(1, &tex);
}

static void item_gl_pump(void)
{
    int i;
    for (i = 0; i < s_garbageCount; i++)
        glDeleteTextures(1, &s_garbage[i]);
    s_garbageCount = 0;
}

static void item_clear_slot(s_ItemSlot* s)
{
    item_retire(s->texText);  s->texText  = 0;
    item_retire(s->texPanel); s->texPanel = 0;
    s->panelW = 0; s->panelH = 0;
    s->bakedScale = 0.0f;
}

static void item_free_slot(s_ItemSlot* s)
{
    item_clear_slot(s);
    s->active = 0;
    s->fading = 0;
}

static int item_slot_phase(const s_ItemSlot* s, Uint32 now, float* t)
{
    Uint32 age = now - s->start;
    Uint32 acc = 0;
    int ph = 0;
    while (ph < IP_COUNT && age >= acc + IP_MS[ph]) { acc += IP_MS[ph]; ph++; }
    if (t) *t = (ph < IP_COUNT) ? (float)(age - acc) / (float)IP_MS[ph] : 1.0f;
    return ph;
}

/* Toast language: Spanish when the PC language is Spanish (s_LangIds order:
 * en de fr es it pl), English for every other language. */
static int item_lang_es(void)
{
    return g_PcConfig.language == 3;
}

/* Short display names for the items the toast (and the horde draft) shows
 * most. The disc's own text is long or odd for several of these: the PAL
 * Spanish first aid kit is "Maletin de primeros\nauxilios", the English heals
 * carry a "(s)" plural. UTF-8. */
typedef struct { unsigned char id; const char* en; const char* es; } s_ItemToastName;

static const s_ItemToastName ITEM_TOAST_NAMES[] = {
    { InvItemId_HealthDrink,    "Health Drink",    "Bebida t\xC3\xB3nica" },
    { InvItemId_FirstAidKit,    "First Aid Kit",   "Botiqu\xC3\xADn" },
    { InvItemId_Ampoule,        "Ampoule",         "Ampolla" },
    { InvItemId_KitchenKnife,   "Kitchen Knife",   "Cuchillo de cocina" },
    { InvItemId_SteelPipe,      "Steel Pipe",      "Tubo de acero" },
    { InvItemId_RockDrill,      "Rock Drill",      "Taladro neum\xC3\xA1tico" },
    { InvItemId_Hammer,         "Hammer",          "Martillo" },
    { InvItemId_Chainsaw,       "Chainsaw",        "Motosierra" },
    { InvItemId_Katana,         "Katana",          "Katana" },
    { InvItemId_Axe,            "Axe",             "Hacha" },
    { InvItemId_Handgun,        "Handgun",         "Pistola" },
    { InvItemId_HuntingRifle,   "Hunting Rifle",   "Rifle de caza" },
    { InvItemId_Shotgun,        "Shotgun",         "Escopeta" },
    { InvItemId_HyperBlaster,   "Hyper Blaster",   "Hiperexterminador" },
    { InvItemId_HandgunBullets, "Handgun Bullets", "Balas de pistola" },
    { InvItemId_RifleShells,    "Rifle Shells",    "Balas de rifle" },
    { InvItemId_ShotgunShells,  "Shotgun Shells",  "Cartuchos de escopeta" },
};

/* The compiled (English) item names, for the non-Spanish fallback. */
extern const char* INVENTORY_ITEM_NAMES[];
#define ITEM_NAME_COUNT 195

static int item_is_utf8(const unsigned char* s)
{
    while (*s)
    {
        int n = (*s < 0x80) ? 0 : ((*s & 0xE0) == 0xC0) ? 1 : ((*s & 0xF0) == 0xE0) ? 2 :
                ((*s & 0xF8) == 0xF0) ? 3 : -1;
        if (n < 0)
            return 0;
        s++;
        while (n-- > 0)
        {
            if ((*s & 0xC0) != 0x80)
                return 0;
            s++;
        }
    }
    return 1;
}

/* Disc text -> one display line: '_' and newlines become spaces, a "(s)"
 * plural is dropped, and PAL Latin-1 accents (0xED for i-acute...) become
 * UTF-8 so the renderer stops eating the letters after them. */
static void item_clean(const char* src, char* dst, size_t dstsz)
{
    const unsigned char* s = (const unsigned char*)src;
    int latin1;
    size_t i = 0;

    if (dstsz == 0)
        return;
    if (!src || !src[0])
    {
        dst[0] = '\0';
        return;
    }
    latin1 = !item_is_utf8(s);
    while (*s && i + 1 < dstsz)
    {
        if (s[0] == '(' && s[1] == 's' && s[2] == ')')
        {
            s += 3;
            continue;
        }
        if (*s == '_' || *s == '\n' || *s == '\t')
        {
            if (i > 0 && dst[i - 1] != ' ')
                dst[i++] = ' ';
            s++;
            continue;
        }
        if (latin1 && *s >= 0x80)
        {
            if (i + 2 >= dstsz)
                break;
            dst[i++] = (char)(0xC0 | (*s >> 6));
            dst[i++] = (char)(0x80 | (*s & 0x3F));
            s++;
            continue;
        }
        dst[i++] = (char)*s++;
    }
    while (i > 0 && dst[i - 1] == ' ')
        i--;
    dst[i] = '\0';
}

const char* Pc_ItemToast_Name(unsigned char itemId, char* buf, int bufsz)
{
    int i;
    int es = item_lang_es();

    if (buf == NULL || bufsz <= 0)
        return "";
    buf[0] = '\0';
    for (i = 0; i < (int)(sizeof(ITEM_TOAST_NAMES) / sizeof(ITEM_TOAST_NAMES[0])); i++)
    {
        if (ITEM_TOAST_NAMES[i].id == itemId)
        {
            snprintf(buf, (size_t)bufsz, "%s", es ? ITEM_TOAST_NAMES[i].es : ITEM_TOAST_NAMES[i].en);
            return buf;
        }
    }
    if (es)
    {
        item_clean(Pc_ItemDisplayName(itemId), buf, (size_t)bufsz);
    }
    else
    {
        int idx = (int)itemId - InvItemId_HealthDrink;
        if (idx >= 0 && idx < ITEM_NAME_COUNT && INVENTORY_ITEM_NAMES[idx] != NULL)
            item_clean(INVENTORY_ITEM_NAMES[idx], buf, (size_t)bufsz);
    }
    return buf;
}

int Pc_ItemToast_IsCommon(unsigned char itemId)
{
    return (itemId >= InvItemId_HealthDrink && itemId <= InvItemId_Ampoule) ||
           (itemId >= InvItemId_HandgunBullets);
}

static void item_push(const s_ItemToast* item);

void Pc_ItemToast_Show(unsigned char itemId, unsigned char itemCount)
{
    s_ItemToast item;
    int stackable;
    const char* got = item_lang_es() ? "Obtenido" : "Obtained";

    memset(&item, 0, sizeof(item));
    Pc_ItemToast_Name(itemId, item.name, (int)sizeof(item.name));
    if (!item.name[0])
        return;

    stackable = Pc_ItemToast_IsCommon(itemId);
    if (stackable && itemCount > 1)
        snprintf(item.sub, sizeof(item.sub), "%s x%d", got, (int)itemCount);
    else
        snprintf(item.sub, sizeof(item.sub), "%s", got);

    item_push(&item);
}

void Pc_ItemToast_ShowConsumed(unsigned char itemId)
{
    s_ItemToast item;

    memset(&item, 0, sizeof(item));
    Pc_ItemToast_Name(itemId, item.name, (int)sizeof(item.name));
    if (!item.name[0])
        return;
    snprintf(item.sub, sizeof(item.sub), "%s", item_lang_es() ? "Usado" : "Used");

    item_push(&item);
}

static void item_push(const s_ItemToast* item)
{
        int i, slot = -1, oldest = -1, nActive = 0;
        Uint32 now = SDL_GetTicks();
        for (i = 0; i < ITEM_STACK_MAX; i++)
        {
            if (!s_slots[i].active && !s_slots[i].fading) { slot = i; break; }
            if (s_slots[i].active && !s_slots[i].fading)
            {
                nActive++;
                if (oldest < 0 || s_slots[i].start < s_slots[oldest].start)
                    oldest = i;
            }
        }
        if (slot < 0)
        {
            for (i = 0; i < ITEM_STACK_MAX; i++)
                if (s_slots[i].fading && (slot < 0 || s_slots[i].start < s_slots[slot].start))
                    slot = i;
            if (slot >= 0)
                item_free_slot(&s_slots[slot]);
        }
        if (nActive >= ITEM_VISIBLE_MAX && oldest >= 0)
        {
            s_slots[oldest].start = now - (IP_MS[IP_IN] + IP_MS[IP_HOLD]);
            s_slots[oldest].fading = 1;
        }
        if (slot < 0)
        {
            for (i = 0; i < ITEM_STACK_MAX; i++)
                if (!s_slots[i].active && !s_slots[i].fading) { slot = i; break; }
        }
        if (slot < 0)
            return;
        item_free_slot(&s_slots[slot]);
        s_slots[slot].item = *item;
        s_slots[slot].active = 1;
        s_slots[slot].fading = 0;
        s_slots[slot].start = now;
        s_slots[slot].yCur = -1.0f;
        item_sound_play();
}

static float e_outCubic(float t) { float u = 1.0f - t; return 1.0f - u * u * u; }
static float e_inCubic (float t) { return t * t * t; }

static void item_slot_bake(s_ItemSlot* s, float scale, float vpW, float vpH, float padX, float padY, float textWMax)
{
    if (s->texText && fabsf(s->bakedScale - scale) > 0.001f)
    {
        item_retire(s->texText);
        s->texText = 0;
    }
    if (!s->texText && (s_fontOk[IFONT_NAME] || s_fontOk[IFONT_BODY]))
    {
        s_ItemLine ln[2];
        ln[0].text = s->item.name; ln[0].fontIdx = IFONT_NAME; ln[0].px = 0.042f * vpH * scale; ln[0].tint = 255;
        ln[1].text = s->item.sub;  ln[1].fontIdx = IFONT_BODY; ln[1].px = 0.026f * vpH * scale; ln[1].tint = 200;
        s->texText = item_bake(ln, 2, (int)textWMax, &s->texTextW, &s->texTextH);
        s->bakedScale = scale;
    }
    s->panelW = s->texTextW + (int)(3.2f * padX);
    s->panelH = s->texTextH + (int)(2.0f * padY);
    if ((float)s->panelW > 0.42f * vpW) s->panelW = (int)(0.42f * vpW);
    item_build_panel(s->panelW, s->panelH, &s->texPanel, &s->panelW, &s->panelH);
}

void Pc_ItemToast_Draw(void)
{
    GLint vp[4];
    float vpW, vpH;
    float padX, padY, textWMax;
    float scale, opac, bgA;
    int   corner, i, any = 0;
    Uint32 now = SDL_GetTicks();
    Uint32 dtms;
    float  dt;
    static Uint32 s_lastDraw = 0;

    GLint  prevProg = 0, prevVao = 0, prevBuf = 0, prevTex = 0;
    GLint  prevUnit = GL_TEXTURE0, prevAlign = 4;
    GLint  prevSrcRgb = GL_ONE, prevDstRgb = GL_ZERO;
    GLint  prevSrcA = GL_ONE, prevDstA = GL_ZERO;
    GLint  prevEqRgb = GL_FUNC_ADD, prevEqA = GL_FUNC_ADD;
    GLboolean prevBlend, prevDepth, prevCull;

    for (i = 0; i < ITEM_STACK_MAX; i++)
        if (s_slots[i].active || s_slots[i].fading) { any = 1; break; }
    if (!any)
    {
        s_lastDraw = now;
        return;
    }

    glGetIntegerv(GL_VIEWPORT, vp);
    vpW = (float)vp[2];
    vpH = (float)vp[3];
    if (vp[2] <= 0 || vp[3] <= 0)
        return;

    if (!s_glReady)
        item_gl_init();
    if (s_glReady != 1)
        return;

    dtms = (s_lastDraw != 0 && now >= s_lastDraw) ? now - s_lastDraw : 16;
    if (dtms > 100) dtms = 100;
    dt = (float)dtms / 1000.0f;
    s_lastDraw = now;

    corner = g_PcConfig.itemToastCorner;
    if (corner < 0 || corner > 3) corner = 3;
    scale = g_PcConfig.itemToastScale / 100.0f;
    if (scale < 0.6f) scale = 0.6f;
    if (scale > 1.3f) scale = 1.3f;
    opac = g_PcConfig.itemToastOpacity / 100.0f;
    if (opac < 0.2f) opac = 0.2f;
    if (opac > 1.0f) opac = 1.0f;
    {
        float bg = (float)g_PcConfig.itemToastBg;
        if (bg < 0.0f) bg = 0.0f;
        if (bg > 100.0f) bg = 100.0f;
        bgA = bg / 100.0f;
    }

    item_fonts_init();

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

    item_gl_pump();

    padX     = 0.022f * vpH * scale;
    padY     = 0.018f * vpH * scale;
    textWMax = 0.34f * vpW;

    {
        int order[ITEM_STACK_MAX];
        int nOrder = 0, k;
        float gap = 0.015f * vpH;
        float kk = dt * 10.0f;
        if (kk > 1.0f) kk = 1.0f;

        for (i = 0; i < ITEM_STACK_MAX; i++)
        {
            s_ItemSlot* s = &s_slots[i];
            float t;
            int ph, j;
            if (!s->active && !s->fading)
                continue;
            ph = item_slot_phase(s, now, &t);
            if (ph >= IP_COUNT) { item_free_slot(s); continue; }
            if (s->fading)
                continue;
            j = nOrder;
            while (j > 0 && s_slots[order[j - 1]].start > s->start)
            {
                order[j] = order[j - 1];
                j--;
            }
            order[j] = i;
            nOrder++;
            item_slot_bake(s, scale, vpW, vpH, padX, padY, textWMax);
        }

        if (corner < 2)
        {
            float y = vpH * 0.94f;
            for (k = nOrder - 1; k >= 0; k--)
            {
                s_ItemSlot* s = &s_slots[order[k]];
                float target = y - (float)s->panelH;
                if (s->yCur < 0.0f) s->yCur = target + (float)s->panelH * 0.75f + gap;
                s->yCur += (target - s->yCur) * kk;
                y = s->yCur - gap;
            }
        }
        else
        {
            float y = vpH * 0.30f;
            for (k = nOrder - 1; k >= 0; k--)
            {
                s_ItemSlot* s = &s_slots[order[k]];
                float target = y;
                if (s->yCur < 0.0f) s->yCur = target - ((float)s->panelH * 0.75f + gap);
                s->yCur += (target - s->yCur) * kk;
                y = s->yCur + (float)s->panelH + gap;
            }
        }
    }

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

    for (i = 0; i < ITEM_STACK_MAX; i++)
    {
        s_ItemSlot* s = &s_slots[i];
        float t, eSlide, alpha, left, bottom, top, textL, textTY;
        int ph;
        if (!s->active && !s->fading)
            continue;
        ph = item_slot_phase(s, now, &t);
        switch (ph)
        {
        case IP_IN:   eSlide = e_outCubic(t); alpha = t; break;
        case IP_HOLD: eSlide = 1.0f;          alpha = 1.0f; break;
        default:      eSlide = 1.0f;          alpha = 1.0f - e_inCubic(t); break;
        }
        alpha *= opac;
        if (corner == 0 || corner == 2)
            left = vpW * 0.035f - (1.0f - eSlide) * ((float)s->panelW + 2.0f);
        else
            left = vpW * 0.965f - (float)s->panelW + (1.0f - eSlide) * ((float)s->panelW + 2.0f);
        bottom = s->yCur;
        top    = bottom + (float)s->panelH;
        textL  = left + 2.1f * padX;
        textTY = top - padY;
        if (bgA > 0.0f)
            item_quad(s->texPanel, NX(left), NY(top), NX(left + (float)s->panelW), NY(bottom), alpha * bgA);
        if (s->texText && alpha > 0.01f)
        {
            float tw = (float)s->texTextW, th = (float)s->texTextH;
            item_quad(s->texText, NX(textL), NY(textTY), NX(textL + tw), NY(textTY - th), alpha);
        }
    }

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
