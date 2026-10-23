/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * pc_quick_options.c - in-game quick options overlay (key_quick_options, default F10).
 *
 * A sibling of pc_rando_settings.c and it repeats that file's hard-won GL
 * constraints deliberately rather than sharing code:
 *   - its OWN GL program / VAO / textures, with full state save+restore, so it
 *     never clobbers PsyCross's or the console's programs.
 *   - LEGACY GLSL (attribute / varying / gl_FragColor, no #version).
 *   - all GL work deferred to Draw (post-capture); the game thread only reads
 *     input and toggles state.
 *   - wall-clock timing (the draw hook runs after the frame is captured).
 *
 * Unlike the randomizer panel there is NO full-screen dim and the panel is
 * translucent: the point of this menu is to see a change land in the live
 * scene behind it. Text is stb_truetype, baked to small textures; the toast
 * owns the only stb_truetype implementation in the build.
 */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL.h>
#include <PsyX/common/glad.h>
#include <PsyX/PsyX_backend.h>

#include "stb_truetype.h"

#include "pc_quick_options.h"
#include "pc_mouse_cursor.h"
#include "pc_config.h"
#include "pc_cheats.h"
#include "sh_log.h"

/* options.c: the live-applying rows of the PC Options screen, by config key. */
extern const void* PcOpt_QuickFind(const char* key);
extern const char* PcOpt_QuickName(const void* h);
extern const char* PcOpt_QuickLabel(const void* h, char* buf, int bufsz);
extern void        PcOpt_QuickAdjust(const void* h, int dir);
extern int         PcOpt_QuickRealtime(const void* h);
/* options.c: rows that are not in that table. Keep in step with the identical
 * enum there -- these are indices into its switch, nothing more. */
enum { QO_X_SHADOW = 0, QO_X_SPEAKERS, QO_X_BGM, QO_X_SFX,
       QO_X_ASPECT, QO_X_CRTTRIM, QO_X_HFOV, QO_X_VFOV, QO_X_PAR, QO_X_VSHIFT,
       QO_X_CUTSHIFT, QO_X_AMMOHUD, QO_X_AUTORELOAD,
       QO_X_DPAD_ON, QO_X_DPAD_UP, QO_X_DPAD_DOWN, QO_X_DPAD_LEFT, QO_X_DPAD_RIGHT,
       QO_X_QUICKHEAL, QO_X_TOASTBG, QO_X_HEALFLASH,
       QO_X_AIMCAM, QO_X_TRWEAPON, QO_X_TRHEAL, QO_X_TRMELEE, QO_X_IMMWEAPONS, QO_X_IMMFEAR, QO_X_IMMBLOOD, QO_X_IMMFLICKER, QO_X_MANRELOAD,
       QO_X_DIFFAMMO, QO_X_DIFFHEALS, QO_X_DIFFENEMY, QO_X_HPBARS,
       QO_X_HHSTYLE, QO_X_HHNUMBER, QO_X_HHCORNER, QO_X_HHSCALE, QO_X_HHOPACITY, QO_X_HHALWAYS, QO_X_HHHUD,
       QO_X_MELEEAIMWALK,
       QO_X_SHHURT, QO_X_SHFIRE, QO_X_SHDEALT, QO_X_SHHURTI, QO_X_SHFIREI, QO_X_SHDEALTI,
       QO_X_HITMARKER, QO_X_KILLFLASH, QO_X_KILLMODE,
       QO_X_LOWOP, QO_X_HURTFLASH, QO_X_HURTOP };
extern const char* PcOpt_QuickExtraLabel(int which, char* buf, int bufsz);
extern void        PcOpt_QuickExtraAdjust(int which, int dir);
extern void        PcOpt_QuickViewReset(void);
extern const char* PcOpt_TrainerWeaponLabel(char* buf, int bufsz);
extern const char* PcOpt_TrainerHealLabel(char* buf, int bufsz);
extern void        PcOpt_TrainerWeaponStep(int dir);
extern void        PcOpt_TrainerHealStep(int dir);
extern void        PcOpt_TrainerMeleeStep(int dir);
extern void        PcOpt_TrainerGiveWeapon(void);
extern void        PcOpt_TrainerGiveHeal(void);
extern void        PcOpt_TrainerGiveMelee(void);

#define QO_GARBAGE  48
#define QO_MAX_ROWS 19
#define QO_PAGES    11
#define QO_DD_MAX     64  /* dropdown entries cached */
#define QO_DD_VISIBLE 8

/* ------------------------------------------------------------------ */
/* Rows                                                                */
/* ------------------------------------------------------------------ */

/* ROW_ACTION fires from Confirm/click ONLY -- unlike every other row kind,
 * Left/Right must not trigger it, or scrolling past "Reset View Settings"
 * with the arrows would undo the player's tuning. */
enum { ROW_OPT = 0, ROW_EXTRA, ROW_PAGE, ROW_PREV, ROW_CLOSE, ROW_CHEAT, ROW_ACTION };
enum { QO_A_VIEWRESET = 0, QO_A_GIVEWEAPON, QO_A_GIVEHEAL, QO_A_GIVEMELEE };

typedef struct
{
    int         kind;   /* ROW_* */
    const char* key;    /* ROW_OPT: config key looked up in options.c */
    int         extra;  /* ROW_EXTRA: QO_X_*; ROW_CHEAT: row index */
    const char* label;  /* ROW_EXTRA / ROW_PAGE / ROW_PREV / ROW_CLOSE display name */
    int         cpage;  /* ROW_CHEAT: PC_CHEATS_PAGE_* */
} QoRowDef;

#define QO_IS_VALUE_ROW(k) ((k) == ROW_OPT || (k) == ROW_EXTRA || (k) == ROW_CHEAT)

static const QoRowDef s_page0[] = {
    { ROW_OPT,   "psx_dither",           0, NULL },  /* Texture_Filter */
    { ROW_OPT,   "msaa",                 0, NULL },  /* Antialiasing (restart) */
    { ROW_OPT,   "post_process",         0, NULL },
    { ROW_OPT,   "tonemap",              0, NULL },
    { ROW_OPT,   "fog_strength",         0, NULL },
    { ROW_OPT,   "flashlight_mode",      0, NULL },
    { ROW_OPT,   "flashlight_intensity", 0, NULL },
    { ROW_OPT,   "flashlight_size",      0, NULL },
    { ROW_EXTRA, NULL, QO_X_SHADOW,         "Shadow Resolution" },
    { ROW_OPT,   "bullet_decals",        0, NULL },
    { ROW_EXTRA, NULL, QO_X_HPBARS,         "Enemy HP Bars" },
    { ROW_PREV,  NULL, 0,                   "Prev page  (Visual Feedback)" },
    { ROW_PAGE,  NULL, 0,                   "Next page  (HUD & Audio)" },
    { ROW_CLOSE, NULL, 0,                   "Close" },
};

static const QoRowDef s_page1[] = {
    { ROW_OPT,   "minimap",              0, NULL },
    { ROW_OPT,   "minimap_scale",        0, NULL },
    { ROW_OPT,   "minimap_corner",       0, NULL },
    { ROW_OPT,   "minimap_opacity",      0, NULL },
    { ROW_OPT,   "minimap_require_map",  0, NULL },
    { ROW_OPT,   "crosshair",            0, NULL },
    { ROW_OPT,   "item_toast_corner",  0, NULL },
    { ROW_OPT,   "item_toast_scale",   0, NULL },
    { ROW_OPT,   "item_toast_opacity", 0, NULL },
    { ROW_EXTRA, NULL, QO_X_SPEAKERS,       "Speaker Layout" },
    { ROW_EXTRA, NULL, QO_X_BGM,            "Music Volume" },
    { ROW_EXTRA, NULL, QO_X_SFX,            "Effects Volume" },
    { ROW_OPT,   "fmv_volume",           0, NULL },
    { ROW_PREV,  NULL, 0,                   "Prev page  (Graphics)" },
    { ROW_PAGE,  NULL, 0,                   "Next page  (HUD 2)" },
    { ROW_CLOSE, NULL, 0,                   "Close" },
};

static const QoRowDef s_pageHud2[] = {
    { ROW_OPT,   "ammo_corner",    0, NULL },
    { ROW_OPT,   "ammo_scale",     0, NULL },
    { ROW_OPT,   "ammo_opacity",   0, NULL },
    { ROW_OPT,   "ammo_always",    0, NULL },
    { ROW_EXTRA, NULL, QO_X_AMMOHUD,     "Ammo HUD" },
    { ROW_EXTRA, NULL, QO_X_AUTORELOAD,  "Auto Reload" },
    { ROW_EXTRA, NULL, QO_X_MANRELOAD,   "Manual Reload" },
    { ROW_EXTRA, NULL, QO_X_DPAD_ON,     "D-Pad Weapons" },
    { ROW_EXTRA, NULL, QO_X_DPAD_UP,     "D-Pad Up" },
    { ROW_EXTRA, NULL, QO_X_DPAD_DOWN,   "D-Pad Down" },
    { ROW_EXTRA, NULL, QO_X_DPAD_LEFT,   "D-Pad Left" },
    { ROW_EXTRA, NULL, QO_X_DPAD_RIGHT,  "D-Pad Right" },
    { ROW_EXTRA, NULL, QO_X_QUICKHEAL,   "Quick Heal" },
    { ROW_EXTRA, NULL, QO_X_TOASTBG,     "Toast Background" },
    { ROW_PREV,  NULL, 0,                   "Prev page  (HUD & Audio)" },
    { ROW_PAGE,  NULL, 0,                   "Next page  (View)" },
    { ROW_CLOSE, NULL, 0,                   "Close" },
};

static const QoRowDef s_pageTrainer[] = {
    { ROW_EXTRA,  NULL, QO_X_TRWEAPON,    "Weapon" },
    { ROW_ACTION, NULL, QO_A_GIVEWEAPON,  "Give Weapon + Ammo" },
    { ROW_EXTRA,  NULL, QO_X_TRMELEE,     "Melee" },
    { ROW_ACTION, NULL, QO_A_GIVEMELEE,   "Give Melee" },
    { ROW_EXTRA,  NULL, QO_X_TRHEAL,      "Heal" },
    { ROW_ACTION, NULL, QO_A_GIVEHEAL,    "Give Heals x3" },
    { ROW_PREV,   NULL, 0,                   "Prev page  (Debug)" },
    { ROW_PAGE,   NULL, 0,                   "Next page  (Immersion)" },
    { ROW_CLOSE, NULL, 0,                   "Close" },
};

static const QoRowDef s_pageImmersion[] = {
    { ROW_EXTRA,  NULL, QO_X_IMMWEAPONS,  "Weapon Immersion" },
    { ROW_EXTRA,  NULL, QO_X_IMMFEAR,     "Fear Panting" },
    { ROW_EXTRA,  NULL, QO_X_IMMBLOOD,    "Blood Walls" },
    { ROW_EXTRA,  NULL, QO_X_IMMFLICKER,   "Flicker" },
    { ROW_PREV,   NULL, 0,                   "Prev page  (Trainer)" },
    { ROW_PAGE,   NULL, 0,                   "Next page  (Difficulty)" },
    { ROW_CLOSE, NULL, 0,                   "Close" },
};

static const QoRowDef s_pageDifficulty[] = {
    { ROW_EXTRA,  NULL, QO_X_DIFFAMMO,    "Ammo" },
    { ROW_EXTRA,  NULL, QO_X_DIFFHEALS,   "Heals" },
    { ROW_EXTRA,  NULL, QO_X_DIFFENEMY,   "Enemy Damage" },
    { ROW_PREV,   NULL, 0,                   "Prev page  (Immersion)" },
    { ROW_PAGE,   NULL, 0,                   "Next page  (Health)" },
    { ROW_CLOSE, NULL, 0,                   "Close" },
};

static const QoRowDef s_pageHealth[] = {
    { ROW_EXTRA,  NULL, QO_X_HHHUD,        "Health HUD" },
    { ROW_EXTRA,  NULL, QO_X_HHSTYLE,     "Style" },
    { ROW_EXTRA,  NULL, QO_X_HHNUMBER,    "Number" },
    { ROW_EXTRA,  NULL, QO_X_HHCORNER,    "Corner" },
    { ROW_EXTRA,  NULL, QO_X_HHSCALE,     "Scale" },
    { ROW_EXTRA,  NULL, QO_X_HHOPACITY,   "Opacity" },
    { ROW_EXTRA,  NULL, QO_X_HHALWAYS,    "Always Show" },
    { ROW_PREV,   NULL, 0,                   "Prev page  (Difficulty)" },
    { ROW_PAGE,   NULL, 0,                   "Next page  (Visual Feedback)" },
    { ROW_CLOSE, NULL, 0,                   "Close" },
};

static const QoRowDef s_pageShake[] = {
    { ROW_EXTRA,  NULL, QO_X_SHHURT,      "Hurt Kick" },
    { ROW_EXTRA,  NULL, QO_X_SHHURTI,     "Hurt Strength" },
    { ROW_EXTRA,  NULL, QO_X_SHFIRE,      "Fire Kick" },
    { ROW_EXTRA,  NULL, QO_X_SHFIREI,     "Fire Strength" },
    { ROW_EXTRA,  NULL, QO_X_SHDEALT,     "Dealt Kick" },
    { ROW_EXTRA,  NULL, QO_X_SHDEALTI,    "Dealt Strength" },
    { ROW_EXTRA,  NULL, QO_X_HITMARKER,   "Hitmarker" },
    { ROW_EXTRA,  NULL, QO_X_KILLFLASH,   "Kill Flash" },
    { ROW_EXTRA,  NULL, QO_X_KILLMODE,    "Flash Style" },
    { ROW_EXTRA,  NULL, QO_X_HEALFLASH,   "Heal Flash" },
    { ROW_OPT,   "low_health_glow",      0, NULL },
    { ROW_EXTRA,  NULL, QO_X_LOWOP,       "Low Opacity" },
    { ROW_EXTRA,  NULL, QO_X_HURTFLASH,   "Hurt Flash" },
    { ROW_EXTRA,  NULL, QO_X_HURTOP,      "Hurt Opacity" },
    { ROW_PREV,   NULL, 0,                   "Prev page  (Health)" },
    { ROW_PAGE,   NULL, 0,                   "Next page  (Graphics)" },
    { ROW_CLOSE, NULL, 0,                   "Close" },
};

/* View & Aspect, in two shapes.
 *
 * Control Type picks how much rope the player gets, and the page then shows
 * ONLY the knobs that mode actually reads. Simple owns the shape with one
 * value (Aspect Trim) because its pixel-aspect solve divides hfov straight
 * back out and never reads par -- both rows would sit there doing nothing.
 * Advanced multiplies its own shape out of hfov x vfov / par instead, which is
 * what the released builds did, so there the trim is the inert one.
 *
 * FOV is the same knob in both (the world's vertical ortho); it is named for
 * what it does rather than for the axis it is implemented on, since in Simple
 * it zooms both axes together with the shape held fixed. */
static const QoRowDef s_page2Simple[] = {
    { ROW_EXTRA,  NULL, QO_X_ASPECT,       "Control Type" },
    { ROW_EXTRA,  NULL, QO_X_AIMCAM,       "Aim Camera" },
    { ROW_EXTRA,  NULL, QO_X_MELEEAIMWALK, "Melee Aim Walk" },
    { ROW_EXTRA,  NULL, QO_X_CRTTRIM,      "Aspect Trim" },
    { ROW_EXTRA,  NULL, QO_X_VFOV,         "FOV" },
    { ROW_EXTRA,  NULL, QO_X_VSHIFT,       "Vertical Shift" },
    { ROW_EXTRA,  NULL, QO_X_CUTSHIFT,     "Cutscene Shift" },
    { ROW_ACTION, NULL, QO_A_VIEWRESET,    "Reset View Settings" },
    { ROW_PREV,   NULL, 0,                 "Prev page  (HUD 2)" },
    { ROW_PAGE,   NULL, 0,                 "Next page  (Cheats)" },
    { ROW_CLOSE, NULL, 0,                 "Close" },
};

static const QoRowDef s_page2Advanced[] = {
    { ROW_EXTRA,  NULL, QO_X_ASPECT,       "Control Type" },
    { ROW_EXTRA,  NULL, QO_X_AIMCAM,       "Aim Camera" },
    { ROW_EXTRA,  NULL, QO_X_MELEEAIMWALK, "Melee Aim Walk" },
    { ROW_EXTRA,  NULL, QO_X_CRTTRIM,      "Aspect Trim" },
    { ROW_EXTRA,  NULL, QO_X_HFOV,         "Horizontal FOV" },
    { ROW_EXTRA,  NULL, QO_X_VFOV,         "Vertical FOV" },
    { ROW_EXTRA,  NULL, QO_X_VSHIFT,       "Vertical Shift" },
    { ROW_EXTRA,  NULL, QO_X_CUTSHIFT,     "Cutscene Shift" },
    { ROW_EXTRA,  NULL, QO_X_PAR,          "Pixel Aspect" },
    { ROW_ACTION, NULL, QO_A_VIEWRESET,    "Reset View Settings" },
    { ROW_PREV,   NULL, 0,                 "Prev page  (HUD 2)" },
    { ROW_PAGE,   NULL, 0,                 "Next page  (Cheats)" },
    { ROW_CLOSE, NULL, 0,                 "Close" },
};

/* Set when the row SET changes under the cached text (a Control Type switch,
 * or a reset that flips it back). Every label is cached by row index, so the
 * page has to re-bake or Advanced's extra rows would draw Simple's words. */
static int s_viewRowsDirty;

void Pc_QuickOptions_InvalidateRows(void)
{
    s_viewRowsDirty = 1;
}

static const QoRowDef* qo_view_page(int* count)
{
    if (g_PcConfig.aspectRaw)
    {
        *count = (int)(sizeof(s_page2Advanced) / sizeof(s_page2Advanced[0]));
        return s_page2Advanced;
    }
    *count = (int)(sizeof(s_page2Simple) / sizeof(s_page2Simple[0]));
    return s_page2Simple;
}

/* Pages 2/3 mirror pc_cheats.c's tables, built on first use. */
static QoRowDef s_cheatRows[2][QO_MAX_ROWS];
static int      s_cheatRowCount[2];

static const QoRowDef* qo_cheat_page(int cpage, const char* prevLabel, const char* nextLabel, int* count)
{
    if (s_cheatRowCount[cpage] == 0)
    {
        int n = Pc_Cheats_Count(cpage), i, k = 0;
        if (n > QO_MAX_ROWS - 3) n = QO_MAX_ROWS - 3;
        for (i = 0; i < n; i++)
        {
            s_cheatRows[cpage][k].kind  = ROW_CHEAT;
            s_cheatRows[cpage][k].key   = NULL;
            s_cheatRows[cpage][k].extra = i;
            s_cheatRows[cpage][k].label = NULL;
            s_cheatRows[cpage][k].cpage = cpage;
            k++;
        }
        s_cheatRows[cpage][k].kind = ROW_PREV;  s_cheatRows[cpage][k].label = prevLabel; k++;
        s_cheatRows[cpage][k].kind = ROW_PAGE;  s_cheatRows[cpage][k].label = nextLabel; k++;
        s_cheatRows[cpage][k].kind = ROW_CLOSE; s_cheatRows[cpage][k].label = "Close";   k++;
        s_cheatRowCount[cpage] = k;
    }
    *count = s_cheatRowCount[cpage];
    return s_cheatRows[cpage];
}

static const QoRowDef* qo_page_rows(int page, int* count)
{
    if (page == 1) { *count = (int)(sizeof(s_page1) / sizeof(s_page1[0])); return s_page1; }
    if (page == 2) { *count = (int)(sizeof(s_pageHud2) / sizeof(s_pageHud2[0])); return s_pageHud2; }
    if (page == 3) return qo_view_page(count);
    if (page == 4) return qo_cheat_page(PC_CHEATS_PAGE_CHEATS, "Prev page  (View)", "Next page  (Debug)",    count);
    if (page == 5) return qo_cheat_page(PC_CHEATS_PAGE_DEBUG,  "Prev page  (Cheats)", "Next page  (Trainer)", count);
    if (page == 6) { *count = (int)(sizeof(s_pageTrainer) / sizeof(s_pageTrainer[0])); return s_pageTrainer; }
    if (page == 7) { *count = (int)(sizeof(s_pageImmersion) / sizeof(s_pageImmersion[0])); return s_pageImmersion; }
    if (page == 8) { *count = (int)(sizeof(s_pageDifficulty) / sizeof(s_pageDifficulty[0])); return s_pageDifficulty; }
    if (page == 9) { *count = (int)(sizeof(s_pageHealth) / sizeof(s_pageHealth[0])); return s_pageHealth; }
    if (page == 10) { *count = (int)(sizeof(s_pageShake) / sizeof(s_pageShake[0])); return s_pageShake; }
    *count = (int)(sizeof(s_page0) / sizeof(s_page0[0]));
    return s_page0;
}

static const char* const s_pageTitles[QO_PAGES] = {
    "QUICK OPTIONS  -  GRAPHICS", "QUICK OPTIONS  -  HUD & AUDIO",
    "QUICK OPTIONS  -  HUD 2",     "QUICK OPTIONS  -  VIEW & ASPECT",
    "QUICK OPTIONS  -  CHEATS",   "QUICK OPTIONS  -  DEBUG",
    "QUICK OPTIONS  -  TRAINER", "QUICK OPTIONS  -  IMMERSION",
    "QUICK OPTIONS  -  DIFFICULTY", "QUICK OPTIONS  -  HEALTH",
    "QUICK OPTIONS  -  VISUAL FEEDBACK" };

/* ------------------------------------------------------------------ */
/* State                                                               */
/* ------------------------------------------------------------------ */

enum { QO_CLOSED = 0, QO_OPENING, QO_SHOWN, QO_CLOSING };
static int    s_phase;
static Uint32 s_phaseStart;
#define QO_OPEN_MS  160u
#define QO_CLOSE_MS 120u

int g_PcQuickOptionsActive = 0;

static int s_page;
static int s_sel;

/* GL */
static GLuint s_prog, s_vao, s_vbo;
static GLint  s_locColor;
static int    s_glReady;
static GLuint s_texWhite;
static GLuint s_texCursor;
static GLuint s_garbage[QO_GARBAGE];
static int    s_garbageCount;

/* Fonts (stb_truetype). */
static stbtt_fontinfo s_font;
static unsigned char* s_fontData;
static int            s_fontOk;
static int            s_fontsTried;

/* Baked text, re-baked when the pixel size or the page changes; values are
 * re-baked whenever their text changes. */
static GLuint s_texTitle, s_texHint;
static int    s_titleW, s_titleH, s_hintW, s_hintH;
static GLuint s_texLabel[QO_MAX_ROWS];
static int    s_labelW[QO_MAX_ROWS], s_labelH[QO_MAX_ROWS];
static GLuint s_texValue[QO_MAX_ROWS];
static int    s_valueW[QO_MAX_ROWS], s_valueH[QO_MAX_ROWS];
static char   s_valueText[QO_MAX_ROWS][48];
static int    s_bakedForPx;
static int    s_bakedForPage = -1;

/* Description panel (right-side help): baked wrapped lines for the selected
 * row, re-baked when selection, page or language changes. */
#define QO_DESC_LINES 8
#define QO_DESC_WRAP  44
static GLuint s_texDesc[QO_DESC_LINES];
static int    s_descW[QO_DESC_LINES], s_descH[QO_DESC_LINES];
static int    s_descN = 0;
static int    s_descSel = -1, s_descPage = -1, s_descLang = -1, s_descWrap = -1;
static int    s_descMaxW = 0;

/* Geometry published by Draw for Update's mouse hit-test (viewport px, y up). */
static float s_vpW = 1920.0f, s_vpH = 1080.0f;
static float s_geoListT, s_geoRowPitch, s_geoRowH, s_geoPanelL, s_geoPanelR;
/* Panel drag: grab the title bar and move it. Offsets are in viewport px and
 * survive close/reopen within a session. The title-bar rect is published for
 * Update's hit-test the same way the row geometry is. */
static float s_panelOfsX, s_panelOfsY;
static float s_geoTitleB, s_geoTitleT;
static int   s_dragging;
static float s_dragLastX, s_dragLastY;
static int   s_geoRows;

/* Dropdown over a list row (the Spawn row): open on clicking its value,
 * navigated with Up/Down / wheel / hover, picked with confirm / click. */
static int    s_ddRow = -1;   /* row index on the current page, -1 = closed */
static int    s_ddSel;
static int    s_ddScroll;
static GLuint s_ddTex[QO_DD_MAX];
static int    s_ddW[QO_DD_MAX], s_ddH[QO_DD_MAX];
static float  s_ddL, s_ddR, s_ddTop, s_ddRowH; /* published for the hit-test */
static int    s_ddShown;

static int qo_row_is_list(const QoRowDef* r)
{
    return r->kind == ROW_CHEAT && Pc_Cheats_ListCount(r->cpage, r->extra) > 0;
}

/* ------------------------------------------------------------------ */
/* GL primitives (mirrors pc_rando_settings.c)                         */
/* ------------------------------------------------------------------ */

static GLuint qo_make_shader(GLenum type, const char* src)
{
    GLuint sh = glCreateShader(type);
    GLint  ok = 0;
    glShaderSource(sh, 1, &src, NULL);
    glCompileShader(sh);
    glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
    if (!ok)
    {
        char log[512];
        glGetShaderInfoLog(sh, (GLsizei)sizeof(log), NULL, log);
        SH_DBG("[QUICKOPT] shader failed: %s", log);
        glDeleteShader(sh);
        return 0;
    }
    return sh;
}

static int qo_atlas_ensure(void);

static void qo_gl_init(void)
{
    static const char* vs_src =
        "attribute vec2 a_pos;\n"
        "attribute vec2 a_uv;\n"
        "varying vec2 v_uv;\n"
        "void main() { v_uv = a_uv; gl_Position = vec4(a_pos, 0.0, 1.0); }\n";
    static const char* fs_src =
        "#ifdef GL_ES\n"
        "precision mediump float;\n"
        "#endif\n"
        "varying vec2 v_uv;\n"
        "uniform sampler2D u_tex;\n"
        "uniform vec4 u_color;\n"
        "void main() { gl_FragColor = texture2D(u_tex, v_uv) * u_color; }\n";

    GLuint vs, fs;
    GLint  ok = 0, prevVao = 0, prevBuf = 0, prevProg = 0;

    s_glReady = -1;
    vs = qo_make_shader(GL_VERTEX_SHADER, vs_src);
    fs = qo_make_shader(GL_FRAGMENT_SHADER, fs_src);
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
        SH_DBG("[QUICKOPT] link failed: %s", log);
        glDeleteProgram(s_prog);
        s_prog = 0;
        return;
    }
    /* Restore whatever program was bound, the same way the VAO and array
     * buffer below are restored. PsyCross caches the bound program in
     * g_PreviousShader and skips glUseProgram when it believes the right one
     * is already current, so leaving ours bound here does not just affect the
     * next draw -- it persists until something asks for a DIFFERENT shader.
     * A screen whose every split shares one texture format never does, and
     * renders entirely through this program, which has no Projection uniform:
     * all geometry collapses and the screen is black. That is what happened to
     * the Konami and KCET logos once this init moved to startup; they draw only
     * 4-bit prims, so nothing ever forced the rebind that lets gameplay recover. */
    glGetIntegerv(GL_CURRENT_PROGRAM, &prevProg);
    glUseProgram(s_prog);
    glUniform1i(glGetUniformLocation(s_prog, "u_tex"), 0);
    qo_atlas_ensure(); /* one texture, created here, never recreated */
    s_locColor = glGetUniformLocation(s_prog, "u_color");
    glUseProgram((GLuint)prevProg);

    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &prevVao);
    glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &prevBuf);
    glGenVertexArrays(1, &s_vao);
    glBindVertexArray(s_vao);
    glGenBuffers(1, &s_vbo);
    glBindBuffer(GL_ARRAY_BUFFER, s_vbo);
    glBufferData(GL_ARRAY_BUFFER, 6 * 4 * sizeof(float), NULL, GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));
    glBindVertexArray((GLuint)prevVao);
    glBindBuffer(GL_ARRAY_BUFFER, (GLuint)prevBuf);
    s_glReady = 1;
}

static void qo_reg_set(GLuint id, int w, int h, unsigned hash, int nearest);
static unsigned qo_hash(const unsigned char* p, size_t n);

/* ---------------------------------------------------------------- *
 * Glyph atlas.
 *
 * Everything the panel draws -- every label, value, title, footer and the
 * solid-white quad behind the panel itself -- lives in ONE texture created
 * once, at init, and never recreated.
 *
 * This is a rewrite of a design that repeatedly drew its text as solid
 * coloured blocks. What forced it: the panel's own numbers make it
 * impossible for those blocks to come from our own pixels. A baked string
 * is white RGB with alpha = coverage, and real text measures 12-21% ink, so
 * texture2D(...) * u_color can never yield a fully opaque rectangle. The
 * blocks therefore meant we were not sampling our texture at all -- and
 * every check written to prove otherwise (name alive, dimensions intact,
 * pixels byte-identical, sampler state correct) passed, because the pixels
 * were never the problem.
 *
 * The one object that never once failed across every report is s_texWhite,
 * and what set it apart is that it was created at INIT. Everything that
 * failed was created lazily, mid-frame, inside the game's own GL stream --
 * where PsyCross keeps a redundant-bind cache (g_lastBoundTexture) that
 * early-outs and skips glBindTexture, and where texture names are being
 * created and destroyed constantly. So the fix is to stop making textures
 * during a frame at all: one atlas, allocated up front, sub-image updates
 * only.
 *
 * Slots are handed out by a shelf allocator and handed back as the same
 * GLuint the old code passed around, so every caller and all the layout
 * maths is untouched -- the value is now a slot index + 1 rather than a
 * texture name. Padding keeps LINEAR filtering from bleeding between
 * neighbours. */
#define QO_ATLAS_W    2048
#define QO_ATLAS_H    1024
#define QO_ATLAS_PAD  2
/* One open dropdown alone caches QO_DD_MAX (64) entries, and a 16-row page
 * holds a label and a value each: the old 96 could be exhausted by the panel
 * simply being open, never mind editing anything. */
#define QO_SLOT_MAX   256

static GLuint s_atlasTex;
static int    s_atlasReady;
static int    s_atlasX, s_atlasY, s_atlasRowH;
/* cw/ch is the rectangle the slot OWNS in the atlas; w/h is how much of it the
 * current image uses. They differ after a slot is recycled by a shorter string,
 * and only w/h feed the UVs, so the leftover pixels are never sampled. */
static struct { int x, y, w, h, cw, ch, free; } s_slot[QO_SLOT_MAX];
static int    s_slotCount;
static int    s_atlasExhausted;

/* Slot 0 is the solid white texel and is PERMANENT: the panel background,
 * the scrollbar and the row highlights all draw from it, and a reset that
 * handed slot 0 to the next label would repaint those with a word. */
static void qo_atlas_reset(void)
{
    s_atlasX         = QO_ATLAS_PAD;
    s_atlasY         = QO_ATLAS_PAD;
    s_atlasRowH      = 0;
    s_slotCount      = 0;
    s_atlasExhausted = 0;
    if (s_atlasTex)
    {
        /* re-claim slot 0 in place, exactly as qo_atlas_ensure laid it out */
        s_slot[0].x = QO_ATLAS_PAD; s_slot[0].y = QO_ATLAS_PAD;
        s_slot[0].w = 1;            s_slot[0].h = 1;
        s_slot[0].cw = 1;           s_slot[0].ch = 1;
        s_slot[0].free = 0;
        s_slotCount = 1;
        s_atlasX    = QO_ATLAS_PAD + 1 + QO_ATLAS_PAD;
        s_atlasRowH = 1;
    }
}

static int qo_atlas_ensure(void)
{
    if (s_atlasReady)
        return s_atlasTex != 0;

    s_atlasReady = 1;
    glGenTextures(1, &s_atlasTex);
    if (!s_atlasTex)
        return 0;

    glBindTexture(GL_TEXTURE_2D, s_atlasTex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    /* A MIN filter wanting mip levels that do not exist makes a texture
     * INCOMPLETE, and an incomplete texture samples as (0,0,0,1): opaque
     * black. Pinning the chain at level 0 puts that state out of reach no
     * matter what filter anything else sets. */
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, QO_ATLAS_W, QO_ATLAS_H, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    {
        /* Slot 0: one opaque white texel, written once and never reissued. */
        const unsigned char one[4] = { 255, 255, 255, 255 };
        glTexSubImage2D(GL_TEXTURE_2D, 0, QO_ATLAS_PAD, QO_ATLAS_PAD, 1, 1,
                        GL_RGBA, GL_UNSIGNED_BYTE, one);
    }
    qo_atlas_reset();
    SH_DBG("[QOTEX] glyph atlas %dx%d id=%u", QO_ATLAS_W, QO_ATLAS_H, (unsigned)s_atlasTex);
    return 1;
}

/* Returns a slot handle (index + 1), or 0 if it will not fit. */
static GLuint qo_upload_rgba(const unsigned char* rgba, int w, int h, int nearest)
{
    int idx;

    (void)nearest; /* one atlas, one filter; padding covers the bleed */

    if (!qo_atlas_ensure() || w <= 0 || h <= 0)
        return 0;
    if (w > QO_ATLAS_W - 2 * QO_ATLAS_PAD || h > QO_ATLAS_H - 2 * QO_ATLAS_PAD)
        return 0;

    /* Recycle first. Editing a value re-bakes it on EVERY step, so a held
     * arrow key used to burn one slot per step and never give it back -- the
     * atlas ran out, this returned 0, and the number the player was adjusting
     * simply stopped drawing. A re-baked value is nearly always the same size
     * as the one it replaces, so the slot it just released fits it exactly.
     * Best fit, so a short string does not claim a long string's rectangle. */
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

    if (s_atlasX + w + QO_ATLAS_PAD > QO_ATLAS_W)
    {
        s_atlasX     = QO_ATLAS_PAD;
        s_atlasY    += s_atlasRowH + QO_ATLAS_PAD;
        s_atlasRowH  = 0;
    }
    /* Out of slots or out of atlas. Never draw nothing and never say nothing:
     * the flag makes the next Draw drop every cached handle and re-bake the
     * page from an empty atlas, which costs one frame and always recovers. */
    if (s_slotCount >= QO_SLOT_MAX ||
        s_atlasY + h + QO_ATLAS_PAD > QO_ATLAS_H)
    {
        if (!s_atlasExhausted)
            SH_DBG("[QOTEX] atlas exhausted (%d slots, cursor %d,%d) -- rebuilding",
                   s_slotCount, s_atlasX, s_atlasY);
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

    s_atlasX    += w + QO_ATLAS_PAD;
    if (h > s_atlasRowH)
        s_atlasRowH = h;

    return (GLuint)(idx + 1);
}

/* Retiring a texture used to queue it for glDeleteTextures. It must not.
 *
 * A deleted GL name goes straight back to the driver's pool and the game takes
 * it on its next texture create -- after which these quads sample whatever the
 * game put there. That is the solid red/black block corruption: the [QOTEX]
 * probe showed every upload is clean (right size, first texel 255,255,255,0,
 * glErr 0), so the content is only wrong LATER, once the name is no longer
 * ours. It also explains why the colour varied by area (whatever texture
 * inherited the name) and why glIsTexture-based validation could not catch it
 * -- the name is still a perfectly valid texture, just not ours.
 *
 * Retired names are therefore kept alive and simply abandoned. The caller has
 * already zeroed its slot, so the next draw bakes a fresh texture. That leaks
 * one small texture per re-bake (page switches and value edits only), which is
 * a few MB across a long session -- the correct fix is to re-upload into the
 * same name instead of allocating a new one, but that is a wider change than
 * belongs in a release build. */
static void qo_retire(GLuint tex)
{
    int idx = (int)tex - 1;

    /* Slot 0 is the permanent white texel; freeing it would hand the panel
     * background to the next string that fits in one pixel. */
    if (idx > 0 && idx < s_slotCount)
        s_slot[idx].free = 1;
}

/* Nothing is queued for deletion any more (see qo_retire); kept so the draw
 * path is unchanged and a future in-place-reuse rewrite has a hook. */
static void qo_gl_pump(void)
{
    s_garbageCount = 0;
}

/* Quad in NDC (yTop > yBot). */
/* `tex` is an atlas SLOT handle (index + 1), not a texture name. */
static void qo_quad(GLuint tex, float x0, float yTop, float x1, float yBot,
                    float r, float g, float b, float a)
{
    float v[6][4];
    float u0, v0, u1, v1;
    int   idx = (int)tex - 1;

    if (tex == 0 || idx < 0 || idx >= s_slotCount || !s_atlasTex)
        return;

    /* Half-texel inset: with LINEAR filtering an edge tap would otherwise
     * reach into the padding between neighbouring slots. */
    u0 = ((float)s_slot[idx].x + 0.5f) / (float)QO_ATLAS_W;
    v0 = ((float)s_slot[idx].y + 0.5f) / (float)QO_ATLAS_H;
    u1 = ((float)(s_slot[idx].x + s_slot[idx].w) - 0.5f) / (float)QO_ATLAS_W;
    v1 = ((float)(s_slot[idx].y + s_slot[idx].h) - 0.5f) / (float)QO_ATLAS_H;

    v[0][0] = x0; v[0][1] = yTop; v[0][2] = u0; v[0][3] = v0;
    v[1][0] = x0; v[1][1] = yBot; v[1][2] = u0; v[1][3] = v1;
    v[2][0] = x1; v[2][1] = yTop; v[2][2] = u1; v[2][3] = v0;
    v[3][0] = x1; v[3][1] = yTop; v[3][2] = u1; v[3][3] = v0;
    v[4][0] = x0; v[4][1] = yBot; v[4][2] = u0; v[4][3] = v1;
    v[5][0] = x1; v[5][1] = yBot; v[5][2] = u1; v[5][3] = v1;

    glUniform4f(s_locColor, r, g, b, a);
    glBindTexture(GL_TEXTURE_2D, s_atlasTex);
    glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(v), v);
    glDrawArrays(GL_TRIANGLES, 0, 6);
}

/* ------------------------------------------------------------------ */
/* Fonts                                                               */
/* ------------------------------------------------------------------ */

static unsigned char* qo_read_file(const char* path, long* outSize)
{
    FILE* f = fopen(path, "rb");
    long  n;
    unsigned char* buf;
    if (!f)
        return NULL;
    fseek(f, 0, SEEK_END);
    n = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (n <= 0) { fclose(f); return NULL; }
    if (outSize) *outSize = n;
    buf = (unsigned char*)malloc((size_t)n);
    if (!buf) { fclose(f); return NULL; }
    if (fread(buf, 1, (size_t)n, f) != (size_t)n) { free(buf); fclose(f); return NULL; }
    fclose(f);
    return buf;
}

/* stb_truetype does not copy the font file -- it reads this buffer on every
 * glyph. If it is corrupted, glyphs rasterize as filled boxes, which is exactly
 * what the panel shows, and the texture checks all pass because the block is
 * uploaded faithfully. A strided signature is cheap enough to verify per frame
 * and catches a stray write into the buffer. */
static const char* s_fontPath;
static long        s_fontSize;
static unsigned    s_fontSig;

static unsigned qo_font_sig(const unsigned char* p, long n)
{
    unsigned h = 2166136261u;
    long     i;

    for (i = 0; i < n; i += 61)
    {
        h ^= p[i];
        h *= 16777619u;
    }
    return h ^ (unsigned)n;
}

/* Re-reads the font into a fresh buffer. The old one is abandoned, never freed:
 * stbtt may still hold pointers into it, and whatever scribbled on it will keep
 * doing so. */
static int qo_font_reload(void)
{
    long           sz    = 0;
    unsigned char* fresh = s_fontPath ? qo_read_file(s_fontPath, &sz) : NULL;

    if (fresh && stbtt_InitFont(&s_font, fresh, stbtt_GetFontOffsetForIndex(fresh, 0)))
    {
        s_fontData = fresh;
        s_fontSize = sz;
        s_fontSig  = qo_font_sig(fresh, sz);
        return 1;
    }
    if (fresh)
        free(fresh);
    return 0;
}

/* Returns non-zero when the font memory changed under us (and reloads it). */
static int qo_font_check(void)
{
    static int s_fontLogs = 0;
    unsigned   now;

    if (!s_fontOk || !s_fontData || s_fontSize <= 0)
        return 0;

    now = qo_font_sig(s_fontData, s_fontSize);
    if (now == s_fontSig)
        return 0;

    if (s_fontLogs < 8)
    {
        s_fontLogs++;
        SH_DBG("[QOFONT] font memory CORRUPTED: sig %08x -> %08x (%ld bytes at %p, %s) "
               "-- glyphs would rasterize as blocks; reloading",
               s_fontSig, now, s_fontSize, (void*)s_fontData,
               s_fontPath ? s_fontPath : "?");
    }

    if (qo_font_reload())
        return 1;
    s_fontSig = now; /* cannot reload; stop re-reporting every frame */
    return 1;
}

static void qo_fonts_init(void)
{
    static const char* paths[] = {
        "gamedata/font/Oswald-Regular.ttf",
        "gamedata/font/BarlowSemiCondensed-Regular.ttf",
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
        long           sz   = 0;
        unsigned char* data = qo_read_file(paths[i], &sz);
        if (!data)
            continue;
        if (stbtt_InitFont(&s_font, data, stbtt_GetFontOffsetForIndex(data, 0)))
        {
            s_fontData = data; /* stbtt keeps pointers into this - never free */
            s_fontOk   = 1;
            s_fontPath = paths[i];
            s_fontSize = sz;
            s_fontSig  = qo_font_sig(data, sz);
            return;
        }
        free(data);
    }
    SH_DBG("[QUICKOPT] no usable font - text disabled");
}

/* Rasterize one line of ASCII to a white-coverage RGBA texture, trimmed to
 * the ink so right-alignment and centring use the real text width. */
static GLuint qo_bake_once(const char* text, float px, int* outW, int* outH, int* outInk)
{
    const char* p;
    float scale, penX, baseY;
    int   asc, desc, gap, W, H, i, pad = 1, prev = 0, stride;
    unsigned char *cov, *rgba, *scratch;
    GLuint tex;

    if (!s_fontOk || !text || !text[0] || px < 4.0f)
        return 0;

    scale = stbtt_ScaleForPixelHeight(&s_font, px);
    stbtt_GetFontVMetrics(&s_font, &asc, &desc, &gap);

    W = (int)(px * 0.62f * (float)strlen(text)) + 8 * pad;
    H = (int)ceilf(px * 1.4f) + 2 * pad;
    if (W <= 0 || H <= 0 || W > 4096 || H > 512)
        return 0;
    stride = W;

    cov     = (unsigned char*)calloc((size_t)W * H, 1);
    rgba    = (unsigned char*)calloc((size_t)W * H, 4);
    scratch = (unsigned char*)malloc((size_t)W * H);
    if (!cov || !rgba || !scratch) { free(cov); free(rgba); free(scratch); return 0; }

    penX  = (float)pad;
    baseY = (float)pad + asc * scale;
    p     = text;
    while (*p)
    {
        int cp = (unsigned char)*p++;
        int gx0, gy0, gx1, gy1, gw, gh, adv, lsb, sx, sy;
        float shiftX;
        if (prev)
            penX += stbtt_GetCodepointKernAdvance(&s_font, prev, cp) * scale;
        shiftX = penX - floorf(penX);
        stbtt_GetCodepointBitmapBoxSubpixel(&s_font, cp, scale, scale, shiftX, 0.0f, &gx0, &gy0, &gx1, &gy1);
        gw = gx1 - gx0;
        gh = gy1 - gy0;
        if (gw > 0 && gh > 0 && gw <= W && gh <= H)
        {
            memset(scratch, 0, (size_t)gw * gh);
            stbtt_MakeCodepointBitmapSubpixel(&s_font, scratch, gw, gh, gw, scale, scale, shiftX, 0.0f, cp);
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
                    if (v > cov[dy * W + dx]) cov[dy * W + dx] = v;
                }
            }
        }
        stbtt_GetCodepointHMetrics(&s_font, cp, &adv, &lsb);
        penX += adv * scale;
        prev = cp;
        if (penX > (float)(W - pad)) break;
    }

    {
        int inkW = (int)ceilf(penX) + pad;
        if (inkW > 0 && inkW < W) W = inkW;
    }
    for (i = 0; i < W * H; i++)
    {
        rgba[i * 4 + 0] = 255; rgba[i * 4 + 1] = 255; rgba[i * 4 + 2] = 255;
        rgba[i * 4 + 3] = cov[(i / W) * stride + (i % W)];
    }
    {
        /* Ink coverage of what was actually rasterized. This separates a bake
         * that produced filled boxes (~100%) from a bake that is fine and a
         * draw that samples something opaque -- the texture checks cannot tell
         * those apart, because a faithfully-uploaded block passes every one. */
        int    n   = W * H;
        int    ink = 0;
        int    k;

        for (k = 0; k < n; k++)
        {
            if (rgba[k * 4 + 3] >= 32)
                ink++;
        }
        if (outInk)
            *outInk = n ? (ink * 100 / n) : -1;
    }
    tex = qo_upload_rgba(rgba, W, H, 0);
    if (outW) *outW = W;
    if (outH) *outH = H;
    free(cov); free(rgba); free(scratch);
    return tex;
}

/* Glyphs rasterized from bad font memory come out as filled boxes -- the solid
 * blotches users hit. That is measurable: real text inks 12-21% of its box, a
 * block ~100%. So a bake that comes back saturated is not shipped. The font is
 * re-read and the string baked again; if it is still saturated nothing is
 * returned, because a blank row is at least honest and the next frame retries.
 * Single characters are exempt -- a glyph like a full block legitimately fills
 * its box. */
#define QO_INK_MAX 55


static GLuint qo_bake(const char* text, float px, int* outW, int* outH)
{
    static int s_bakeLogs = 0;
    int    ink = -1;
    GLuint tex = qo_bake_once(text, px, outW, outH, &ink);

    if (tex == 0 || ink < QO_INK_MAX || !text || !text[1])
        return tex;

    if (s_bakeLogs < 8)
    {
        s_bakeLogs++;
        SH_DBG("[QOBAKE] \"%.24s\" baked SATURATED (ink=%d%%, normal is 12-21) -- "
               "glyph data is bad; reloading the font and re-baking", text, ink);
    }

    qo_retire(tex);
    if (!qo_font_reload())
        return 0;

    ink = -1;
    tex = qo_bake_once(text, px, outW, outH, &ink);
    if (tex && ink >= QO_INK_MAX)
    {
        qo_retire(tex);
        return 0;
    }
    return tex;
}

static void qo_build_white(void)
{
    if (qo_atlas_ensure())
        s_texWhite = 1; /* slot 0 handle */

}

static void qo_free_text(void)
{
    int i;

    /* Slots are recycled wholesale: the atlas itself is never freed, so
     * there is no texture lifetime left to get wrong.
     *
     * EVERY handle held outside this function has to be dropped with them,
     * or it keeps pointing at a slot index that now belongs to a different
     * image. The cursor is the one that is not in the loops below: after a
     * page change it drew a label's pixels, then vanished once the index
     * fell past the live slot count. Only slot 0, the white texel, survives
     * a reset, because qo_atlas_reset re-claims it in place. */
    qo_atlas_reset();
    s_texCursor = 0; /* re-uploaded on the next draw */
    qo_retire(s_texTitle); s_texTitle = 0;
    qo_retire(s_texHint);  s_texHint  = 0;
    for (i = 0; i < QO_DESC_LINES; i++)
    {
        qo_retire(s_texDesc[i]); s_texDesc[i] = 0;
    }
    s_descN = 0;
    s_descSel = -1; s_descPage = -1; s_descLang = -1; s_descWrap = -1; s_descMaxW = 0;
    for (i = 0; i < QO_MAX_ROWS; i++)
    {
        qo_retire(s_texLabel[i]); s_texLabel[i] = 0;
        qo_retire(s_texValue[i]); s_texValue[i] = 0;
        s_valueText[i][0] = 0;
    }
    for (i = 0; i < QO_DD_MAX; i++)
    {
        qo_retire(s_ddTex[i]); s_ddTex[i] = 0;
    }
}

/* The overlay's baked textures have twice been seen drawing as solid blocks
 * -- red once, black once. The COLOUR VARYING is the tell: the quads sample
 * whatever texture currently owns that GL name, i.e. the name was freed out
 * from under us and handed to the game (the 1x1 white panel texture, created
 * once at init, never shows it). Deletion is detectable, so verify every
 * cached name before use and rebuild any that went stale, and say so once. */
/* What we uploaded, per texture name. glIsTexture cannot tell that a name is
 * still OURS -- only that it is a live texture -- so the size we put there is
 * kept and checked against what the driver reports. Round-robin; a name that
 * ages out simply stops being checked. */
#define QO_TEXREG 256
static GLuint   s_regId[QO_TEXREG];
static GLint    s_regW[QO_TEXREG], s_regH[QO_TEXREG];
static unsigned s_regHash[QO_TEXREG];
static unsigned char s_regNearest[QO_TEXREG];
static int      s_regNext;
/* One texture is content-checked per frame, rotating, so the readback stall
 * stays off the per-frame path. */
static int s_deepIdx, s_passSlot;

static unsigned qo_hash(const unsigned char* p, size_t n)
{
    unsigned h = 2166136261u;
    size_t   i;

    for (i = 0; i < n; i++)
    {
        h ^= p[i];
        h *= 16777619u;
    }
    return h;
}

static void qo_reg_set(GLuint id, int w, int h, unsigned hash, int nearest)
{
    int i;

    if (id == 0)
        return;
    for (i = 0; i < QO_TEXREG; i++)
    {
        if (s_regId[i] == id)
        {
            s_regW[i] = w; s_regH[i] = h; s_regHash[i] = hash;
            s_regNearest[i] = (unsigned char)(nearest ? 1 : 0);
            return;
        }
    }
    s_regId[s_regNext]   = id;
    s_regW[s_regNext]    = w;
    s_regH[s_regNext]    = h;
    s_regHash[s_regNext] = hash;
    s_regNearest[s_regNext] = (unsigned char)(nearest ? 1 : 0);
    s_regNext = (s_regNext + 1) % QO_TEXREG;
}

/* A texture whose min filter wants mip levels it does not have is INCOMPLETE,
 * and an incomplete texture samples as (0,0,0,1) -- a solid opaque black quad,
 * exactly what the panel shows. This is why the 1x1 white background is immune
 * while every baked label is not: at 1x1, level 0 IS the whole mip chain, so no
 * filter can ever make it incomplete. Content, size and name all check out
 * clean, so the sampler state is what is left. Re-assert it every frame and say
 * so once if it had drifted. */


static void qo_validate_ex(GLuint* t, const char* what, int deep);

static void qo_validate(GLuint* t, const char* what)
{
    qo_validate_ex(t, what, s_passSlot++ == s_deepIdx);
}

/* Nothing to validate any more. These are atlas slot indices, not texture
 * names, and the single atlas is created once and never deleted -- the
 * lifetime problem this used to police no longer exists. */
static void qo_validate_ex(GLuint* t, const char* what, int deep)
{
    (void)t; (void)what; (void)deep;

}

static void qo_validate_cache(void)
{
    int i;

    s_passSlot = 0;

    if (qo_font_check())
    {
        qo_free_text();   /* every cached label was baked from bad glyph data */
        return;
    }

    qo_validate(&s_texTitle,  "title");
    qo_validate(&s_texHint,   "hint");
    qo_validate(&s_texWhite,  "white");
    qo_validate(&s_texCursor, "cursor");
    for (i = 0; i < QO_MAX_ROWS; i++)
    {
        qo_validate(&s_texLabel[i], "label");
        qo_validate(&s_texValue[i], "value");
        if (s_texValue[i] == 0)
            s_valueText[i][0] = 0; /* force the value string to re-bake too */
    }
    for (i = 0; i < QO_DD_MAX; i++)
        qo_validate(&s_ddTex[i], "dropdown");

    if (s_passSlot > 0)
        s_deepIdx = (s_deepIdx + 1) % s_passSlot;
}

/* Options-table names use underscores for spaces. */
static void qo_row_name(const QoRowDef* r, char* out, int n)
{
    const char* src = NULL;
    int i;

    if (r->kind == ROW_OPT)
    {
        const void* h = PcOpt_QuickFind(r->key);
        src = h ? PcOpt_QuickName(h) : r->key;
    }
    else if (r->kind == ROW_CHEAT)
        src = Pc_Cheats_Name(r->cpage, r->extra);
    else
        src = r->label;

    for (i = 0; i < n - 1 && src[i]; i++)
        out[i] = (src[i] == '_') ? ' ' : src[i];
    out[i] = 0;
}

static void qo_row_value(const QoRowDef* r, char* out, int n)
{
    char buf[48];
    out[0] = 0;
    if (r->kind == ROW_OPT)
    {
        const void* h = PcOpt_QuickFind(r->key);
        const char* v = h ? PcOpt_QuickLabel(h, buf, (int)sizeof(buf)) : "?";
        int i;
        for (i = 0; i < n - 1 && v[i]; i++)
            out[i] = (v[i] == '_') ? ' ' : v[i];
        out[i] = 0;
        if (h && !PcOpt_QuickRealtime(h) && i < n - 3)
        {
            out[i++] = ' '; out[i++] = '*'; out[i] = 0;
        }
    }
    else if (r->kind == ROW_EXTRA)
    {
        snprintf(out, (size_t)n, "%s", PcOpt_QuickExtraLabel(r->extra, buf, (int)sizeof(buf)));
    }
    else if (r->kind == ROW_CHEAT)
    {
        snprintf(out, (size_t)n, "%s", Pc_Cheats_Label(r->cpage, r->extra, buf, (int)sizeof(buf)));
    }
}

/* ------------------------------------------------------------------ */
/* Row descriptions (right-side help panel)                             */
/* ------------------------------------------------------------------ */

/* One entry per row, keyed by (kind, key/extra) — plus (cpage, extra) for
 * cheat rows. Copy lives in pc_port/docs/Quick_Options_Reference.md (ES) and
 * Quick_Options_Reference_EN.md; keep this table in step when rows change.
 * Spanish when language == 3, else English. */
typedef struct
{
    int         kind;
    const char* key;    /* ROW_OPT: config key */
    int         extra;  /* ROW_EXTRA / ROW_ACTION: QO_X_* / QO_A_* */
    int         cpage;  /* ROW_CHEAT: PC_CHEATS_PAGE_* */
    const char* en;
    const char* es;
} QoDesc;

#define QO_DK(k_, key_, en_, es_)            { (k_), (key_), -1, -1, (en_), (es_) }
#define QO_DX(x_, en_, es_)                  { ROW_EXTRA, NULL, (x_), -1, (en_), (es_) }
#define QO_DA(a_, en_, es_)                  { ROW_ACTION, NULL, (a_), -1, (en_), (es_) }
#define QO_DC(c_, x_, en_, es_)              { ROW_CHEAT, NULL, (x_), (c_), (en_), (es_) }

static const QoDesc s_descs[] = {
    /* GRAPHICS */
    QO_DK(ROW_OPT, "psx_dither", "Texture filtering. Dither is the PSX-faithful look.",
        "Filtrado de texturas. Dither es el aspecto fiel a PSX."),
    QO_DK(ROW_OPT, "msaa", "Edge smoothing. Requires restarting the game (marked with *).",
        "Suavizado de bordes. Requiere reiniciar el juego (marcado con *)."),
    QO_DK(ROW_OPT, "post_process", "Fullscreen image filter: CRT, scanlines, vignette, grade, grain, sharpen, retro or cinematic.",
        "Filtro de imagen a pantalla completa: CRT, scanlines, viñeta, grado, grano, nitidez, retro o cinemático."),
    QO_DK(ROW_OPT, "tonemap", "Final image tone mapping: Reinhard, ACES or Filmic.",
        "Mapeo de tonos de la imagen final: Reinhard, ACES o Filmic."),
    QO_DK(ROW_OPT, "fog_strength", "Fog density. 1.1 matches the PSX; lowering it lets you see further.",
        "Densidad de la niebla. 1.1 equivale a la PSX; bajarlo permite ver más lejos."),
    QO_DK(ROW_OPT, "flashlight_mode", "Classic is the original per-vertex flashlight; Modern is a stylized per-pixel spotlight, with or without real-time shadows.",
        "Classic es la linterna original por vértices; Modern es un foco estilizado por píxel, con o sin sombras en tiempo real."),
    QO_DK(ROW_OPT, "flashlight_intensity", "Flashlight cone brightness, 0 to 3.",
        "Brillo del cono de la linterna, 0 a 3."),
    QO_DK(ROW_OPT, "flashlight_size", "Flashlight cone coverage, 0 to 3.",
        "Cobertura del cono de la linterna, 0 a 3."),
    QO_DX(QO_X_SHADOW, "Flashlight shadow-map resolution, 256 to 8192. Higher means sharper shadows at a higher performance cost.",
        "Resolución del mapa de sombras de la linterna, 256 a 8192. Más alto = sombras más nítidas y más costoso para el rendimiento."),
    QO_DK(ROW_OPT, "bullet_decals", "Bullet marks where the player's shots land, up to 64, cleared on map change.",
        "Marcas de bala donde los disparos del jugador impactan, hasta 64, se borran al cambiar de mapa."),
    QO_DX(QO_X_HPBARS, "Red health bars over live enemies in view within 12 m. Not visible through walls.",
        "Barras rojas de vida sobre enemigos vivos a la vista y a menos de 12 m. No se ven a través de paredes."),
    /* HUD & AUDIO */
    QO_DK(ROW_OPT, "minimap", "On-screen minimap, square or circular.",
        "Minimapa en pantalla con estilos cuadrado y circular."),
    QO_DK(ROW_OPT, "minimap_scale", "Minimap size, 60 to 120 percent.",
        "Tamaño del minimapa, 60 a 120 %."),
    QO_DK(ROW_OPT, "minimap_corner", "Minimap screen corner.",
        "Esquina del minimapa."),
    QO_DK(ROW_OPT, "minimap_opacity", "Minimap opacity, 0 to 100 percent.",
        "Opacidad del minimapa, 0 a 100 %."),
    QO_DK(ROW_OPT, "minimap_require_map", "Only draws the minimap once Harry has found the area's paper map.",
        "Solo dibuja el minimapa si Harry ya encontró el mapa de papel de la zona."),
    QO_DK(ROW_OPT, "crosshair", "Crosshair while aiming in TPS/OTS; always visible in first person.",
        "Mirilla al apuntar en TPS/OTS; siempre visible en primera persona."),
    QO_DK(ROW_OPT, "low_health_glow", "Pulsing red edge glow at low health.",
        "Resplandor rojo pulsante en los bordes con vida baja."),
    QO_DK(ROW_OPT, "item_toast_corner", "Pickup toast corner.",
        "Esquina de los avisos de objetos."),
    QO_DK(ROW_OPT, "item_toast_scale", "Pickup toast size, 60 to 130 percent.",
        "Tamaño de los avisos, 60 a 130 %."),
    QO_DK(ROW_OPT, "item_toast_opacity", "Pickup toast opacity, 20 to 100 percent. Toasts cover pickups and used heals; the sound can be customized from the launcher.",
        "Opacidad de los avisos, 20 a 100 %. Cubren objetos recogidos o curas utilizadas. El sonido de la notificación puede personalizarse desde el launcher."),
    QO_DX(QO_X_SPEAKERS, "Speaker setup: Auto, Stereo, Quad, 5.1 or 7.1.",
        "Configuración de altavoces: Auto, Stereo, Quad, 5.1 o 7.1."),
    QO_DX(QO_X_BGM, "Music volume, 0 to 16.",
        "Volumen de la música, 0 a 16."),
    QO_DX(QO_X_SFX, "Effects volume, 0 to 16.",
        "Volumen de los efectos, 0 a 16."),
    QO_DK(ROW_OPT, "fmv_volume", "Movie cutscene volume, 0 to 1.",
        "Volumen de las cinemáticas de vídeo, 0 a 1."),
    /* HUD 2 */
    QO_DK(ROW_OPT, "ammo_corner", "Ammo counter corner.",
        "Esquina del contador de munición."),
    QO_DK(ROW_OPT, "ammo_scale", "Ammo counter size, 60 to 130 percent.",
        "Tamaño del contador, 60 a 130 %."),
    QO_DK(ROW_OPT, "ammo_opacity", "Ammo counter opacity, 20 to 100 percent.",
        "Opacidad del contador, 20 a 100 %."),
    QO_DK(ROW_OPT, "ammo_always", "Counter stays up with a firearm equipped; if not, only while aiming.",
        "El contador queda visible con arma de fuego equipada; si no, solo al apuntar."),
    QO_DX(QO_X_AMMOHUD, "Master switch for the ammo counter.",
        "Interruptor general del contador."),
    QO_DX(QO_X_AUTORELOAD, "Animated auto reload on empty magazine with reserve left.",
        "Recarga automática con animación al vaciar el cargador habiendo reserva."),
    QO_DX(QO_X_MANRELOAD, "The R key / Square-X button reloads the aimed weapon when it accepts a reload.",
        "La tecla R / botón Cuadrado/X recarga el arma apuntando si admite recarga."),
    QO_DX(QO_X_DPAD_ON, "The controller D-pad picks weapons instead of moving. They can also be assigned from the inventory.",
        "La cruceta del mando elige armas en vez de mover. También se pueden asignar desde el inventario."),
    QO_DX(QO_X_DPAD_UP, "Weapon bound Up: a weapon or None. Binding a weapon held elsewhere swaps them.",
        "Arma asignada arriba: un arma o None. Asignar un arma ocupada en otra dirección las intercambia."),
    QO_DX(QO_X_DPAD_DOWN, "Weapon bound Down: a weapon or None. Binding a weapon held elsewhere swaps them.",
        "Arma asignada abajo: un arma o None. Asignar un arma ocupada en otra dirección las intercambia."),
    QO_DX(QO_X_DPAD_LEFT, "Weapon bound Left: a weapon or None. Binding a weapon held elsewhere swaps them.",
        "Arma asignada a la izquierda: un arma o None. Asignar un arma ocupada en otra dirección las intercambia."),
    QO_DX(QO_X_DPAD_RIGHT, "Weapon bound Right: a weapon or None. Binding a weapon held elsewhere swaps them.",
        "Arma asignada a la derecha: un arma o None. Asignar un arma ocupada en otra dirección las intercambia."),
    QO_DX(QO_X_QUICKHEAL, "Fast heal with the best item available. Uses L3.",
        "Curación rápida con el mejor objeto disponible. Utiliza L3."),
    QO_DX(QO_X_TOASTBG, "Toast background opacity, 0 to 100 percent.",
        "Opacidad del fondo de los avisos, 0 a 100 %."),
    QO_DX(QO_X_HEALFLASH, "Green flash intensity when healing; 0 disables it.",
        "Intensidad del destello verde al curarse; 0 lo desactiva."),
    /* VIEW & ASPECT */
    QO_DX(QO_X_ASPECT, "Picture-shape controls. Simple uses Aspect Trim; Advanced uses Horizontal/Vertical FOV and Pixel Aspect.",
        "Controles de forma de imagen. Simple usa Aspect Trim; Advanced usa Horizontal/Vertical FOV y Pixel Aspect."),
    QO_DX(QO_X_AIMCAM, "Lets you use a different camera while aiming: Auto, Classic, TPS, OTS or FPS.",
        "Permite utilizar una cámara diferente al apuntar: Auto, Classic, TPS, OTS o FPS."),
    QO_DX(QO_X_MELEEAIMWALK, "Aiming melee in TPS/OTS forces a slow walk (like fixed cameras) instead of dropping the weapon to run.",
        "Apuntar con melee en TPS/OTS fuerza paso lento (como en cámaras fijas) en vez de soltar el arma al correr."),
    QO_DX(QO_X_CRTTRIM, "Fine 4:3 aspect trim, 0.50 to 1.50 (Simple).",
        "Ajuste fino del aspecto 4:3, 0.50 a 1.50 (Simple)."),
    QO_DX(QO_X_HFOV, "Horizontal field of view, 0.25 to 2.00 (Advanced).",
        "Campo de visión horizontal, 0.25 a 2.00 (Advanced)."),
    QO_DX(QO_X_VFOV, "Vertical field of view, 0.25 to 2.00 (Advanced).",
        "Campo de visión vertical, 0.25 a 2.00 (Advanced)."),
    QO_DX(QO_X_VSHIFT, "In-game vertical view shift, plus/minus 60 rows.",
        "Desplazamiento vertical de la vista en juego, ±60 filas."),
    QO_DX(QO_X_CUTSHIFT, "Cutscene vertical view shift, plus/minus 60 rows.",
        "Desplazamiento vertical en cinemáticas, ±60 filas."),
    QO_DX(QO_X_PAR, "Pixel aspect, 0.50 to 2.00 (Advanced).",
        "Aspecto de píxel, 0.50 a 2.00 (Advanced)."),
    QO_DA(QO_A_VIEWRESET, "Restores every value on this page.",
        "Restaura todos los valores de esta página."),
    /* CHEATS */
    QO_DC(0, 0, "Invincibility, same as key 7.",
        "Invencibilidad, igual que la tecla 7."),
    QO_DC(0, 1, "Walk through walls.",
        "Atravesar paredes."),
    QO_DC(0, 2, "Enemies ignore Harry, same as key 9.",
        "Los enemigos ignoran a Harry, igual que la tecla 9."),
    QO_DC(0, 3, "Raises the per-room concurrent-enemy cap.",
        "Eleva el límite de enemigos simultáneos por sala."),
    QO_DC(0, 4, "Grants 15 handgun bullets, instant action.",
        "Otorga 15 balas de pistola, acción inmediata."),
    QO_DC(0, 5, "Grants the rifle if missing plus 30 shells, instant action.",
        "Otorga el rifle si falta más 30 cartuchos, acción inmediata."),
    QO_DC(0, 6, "Grants the shotgun if missing plus 30 shells, instant action.",
        "Otorga la escopeta si falta más 30 cartuchos, acción inmediata."),
    QO_DC(0, 7, "Kills nearby enemies through their normal death path, instant action.",
        "Mata a los enemigos cercanos por su vía normal de muerte, acción inmediata."),
    QO_DC(0, 8, "Play as another character, keeps Harry's animations. Browsable as a dropdown.",
        "Jugar con otro personaje, conserva animaciones de Harry. Navegable como desplegable."),
    QO_DC(0, 9, "Debug free camera.",
        "Cámara libre de depuración."),
    /* DEBUG */
    QO_DC(1, 0, "Enables the console (~) and dev keys.",
        "Activa la consola (~) y teclas de desarrollo."),
    QO_DC(1, 1, "Harry's collision panel and wireframe.",
        "Panel y líneas de la colisión de Harry."),
    QO_DC(1, 2, "Animation keyframe inspector (K, comma/period).",
        "Inspector de keyframes de animación (K, coma/punto)."),
    QO_DC(1, 3, "Sticky fast-forward (the key is held; it switches itself off if Harry dies).",
        "Avance rápido conmutable (la tecla es pulsada; se apaga solo si Harry muere)."),
    QO_DC(1, 4, "Wireframe rendering.",
        "Render en alambre."),
    QO_DC(1, 5, "Untextured rendering.",
        "Render sin texturas."),
    QO_DC(1, 6, "Spawns a monster from the list on confirm. Browsable as a dropdown.",
        "Genera un monstruo de la lista al confirmar. Navegable como desplegable."),
    QO_DC(1, 7, "Writes position and map to the log, instant action.",
        "Escribe posición y mapa en el log, acción inmediata."),
    QO_DC(1, 8, "Writes the camera shot to the log, instant action.",
        "Escribe el plano de cámara en el log, acción inmediata."),
    QO_DC(1, 9, "Kills Harry instantly, instant action.",
        "Mata a Harry al instante, acción inmediata."),
    /* TRAINER */
    QO_DX(QO_X_TRWEAPON, "Firearm to grant: handgun, rifle, shotgun, blaster. Browsable list.",
        "Arma a otorgar: pistola, rifle, escopeta, bláster. Lista navegable."),
    QO_DA(QO_A_GIVEWEAPON, "Grants the chosen firearm plus 30 of its ammo.",
        "Otorga el arma de fuego elegida más 30 de su munición."),
    QO_DX(QO_X_TRMELEE, "Melee weapon to grant: knife, pipe, drill, hammer, saw, katana, axe. Browsable list.",
        "Arma cuerpo a cuerpo a otorgar: cuchillo, tubería, taladro, martillo, sierra, katana, hacha. Lista navegable."),
    QO_DA(QO_A_GIVEMELEE, "Grants the chosen melee weapon.",
        "Otorga el arma cuerpo a cuerpo elegida."),
    QO_DX(QO_X_TRHEAL, "Healing item to grant: drink, kit, ampoule. Browsable list.",
        "Curación a otorgar: bebida, botiquín, ampolla. Lista navegable."),
    QO_DA(QO_A_GIVEHEAL, "Grants 3 of the chosen heal.",
        "Otorga 3 unidades de la curación elegida."),
    /* IMMERSION */
    QO_DX(QO_X_IMMWEAPONS, "The equipped weapon hides after a few seconds and comes back on equip, aim, fire, reload, or when Harry senses danger while the radio crackles.",
        "El arma equipada se oculta a los segundos y reaparece al equipar, apuntar, disparar, recargar o si Harry detecta peligro cuando la radio emite sonidos."),
    QO_DX(QO_X_IMMFEAR, "Harry pants with fear at random while the radio sounds.",
        "Harry jadea de miedo al azar mientras la radio suena."),
    QO_DX(QO_X_IMMBLOOD, "Blood splats on the wall behind bullet-hit enemies.",
        "Salpicaduras de sangre en la pared tras enemigos heridos por bala."),
    QO_DX(QO_X_IMMFLICKER, "Near live enemies the flashlight stutters, then dies for half a second and cannot be switched back on until it recovers: 15% when close, 45% while facing. Per-pixel modes only, not Classic.",
        "Cerca de enemigos vivos la linterna falla, se apaga medio segundo y no se puede volver a encender hasta que se recupere: 15 % cerca, 45 % mirándolo. Solo modos por píxel, no Classic."),
    /* DIFFICULTY (independent from the native difficulty; Normal = vanilla) */
    QO_DX(QO_X_DIFFAMMO, "Loot ammo at 15 / 50 / 100 / 170 percent. E.g. 15 bullets become 2 / 8 / 15 / 26.",
        "Munición del loot al 15 / 50 / 100 / 170 %. P. ej. 15 balas pasan a 2 / 8 / 15 / 26."),
    QO_DX(QO_X_DIFFHEALS, "Heal power raised or lowered to 25 / 50 / 100 / 150 percent.",
        "Potencia o disminuye el efecto de curas al 25 / 50 / 100 / 150 %."),
    QO_DX(QO_X_DIFFENEMY, "Enemy damage raised or lowered: Low 50, Medium 75, Normal 100, High 250, Very High 400, Hell 700 percent.",
        "Daño enemigo aumenta o baja: Low 50, Medium 75, Normal 100, High 250, Very High 400, Hell 700 %."),
    /* HEALTH */
    QO_DX(QO_X_HHHUD, "Master switch for Harry's health indicator.",
        "Interruptor general del indicador de vida de Harry."),
    QO_DX(QO_X_HHSTYLE, "Bar: classic bar. Pulse: animated heartbeat line that speeds up at low health.",
        "Bar: barra clásica. Pulse: línea de pulso cardiaco animado que acelera con vida baja."),
    QO_DX(QO_X_HHNUMBER, "HP number at the right of the indicator.",
        "Número de vida a la derecha del indicador."),
    QO_DX(QO_X_HHCORNER, "Extreme corner for the indicator: top-left, top-right, bottom-left or bottom-right.",
        "Esquina extrema del indicador: arriba-izquierda, arriba-derecha, abajo-izquierda o abajo-derecha."),
    QO_DX(QO_X_HHSCALE, "Indicator size, 60 to 130 percent.",
        "Tamaño del indicador, 60 a 130 %."),
    QO_DX(QO_X_HHOPACITY, "Indicator opacity, 20 to 100 percent.",
        "Opacidad del indicador, 20 a 100 %."),
    QO_DX(QO_X_HHALWAYS, "Always visible; if not, only a few seconds after taking damage or healing.",
        "Siempre visible; si no, solo unos segundos tras recibir daño o curarse."),
    /* VISUAL FEEDBACK */
    QO_DX(QO_X_SHHURT, "Camera kick on taking damage.",
        "Sacudida de cámara al recibir daño."),
    QO_DX(QO_X_SHHURTI, "Strength of the hurt kick, 0 to 200 percent.",
        "Intensidad de la sacudida al recibir daño, 0 a 200 %."),
    QO_DX(QO_X_SHFIRE, "Kick when firing, as the bullet leaves, same for all 4 guns.",
        "Sacudida al disparar, al salir la bala, igual en las 4 armas."),
    QO_DX(QO_X_SHFIREI, "Strength of the fire kick, 0 to 200 percent.",
        "Intensidad de la sacudida al disparar, 0 a 200 %."),
    QO_DX(QO_X_SHDEALT, "Kick when damaging an enemy.",
        "Sacudida al dañar a un enemigo."),
    QO_DX(QO_X_SHDEALTI, "Strength of the dealt kick, 0 to 200 percent.",
        "Intensidad de la sacudida al dañar, 0 a 200 %."),
    QO_DX(QO_X_HITMARKER, "The crosshair flashes red on a kill (needs the crosshair enabled).",
        "La mirilla parpadea en rojo al matar (requiere la mirilla activa)."),
    QO_DX(QO_X_KILLFLASH, "Brief fullscreen flash on a kill.",
        "Destello fugaz a pantalla completa al matar."),
    QO_DX(QO_X_KILLMODE, "Kill flash style. B&W: black and white. Vignette: black vignette.",
        "Estilo del kill flash. B&W: blanco y negro. Vignette: viñeta negra."),
    QO_DX(QO_X_LOWOP, "Low-health glow opacity, 0 to 100 percent.",
        "Opacidad del resplandor con vida baja, 0 a 100 %."),
    QO_DX(QO_X_HURTFLASH, "Dark-red fullscreen flash when Harry takes damage.",
        "Destello rojo oscuro a pantalla completa cuando Harry recibe daño."),
    QO_DX(QO_X_HURTOP, "Hurt-flash opacity, 0 to 100 percent.",
        "Opacidad del destello de daño, 0 a 100 %."),
};

static const char* qo_desc_text(const QoRowDef* r)
{
    size_t i;
    int    wantEs = (g_PcConfig.language == 3) ? 1 : 0;
    const char* fallbackEn = NULL;
    const char* fallbackEs = NULL;

    for (i = 0; i < sizeof(s_descs) / sizeof(s_descs[0]); i++)
    {
        const QoDesc* d = &s_descs[i];
        if (d->kind != r->kind)
            continue;
        if (r->kind == ROW_OPT)
        {
            if (d->key == NULL || r->key == NULL || strcmp(d->key, r->key) != 0)
                continue;
        }
        else if (r->kind == ROW_CHEAT)
        {
            if (d->cpage != r->cpage || d->extra != r->extra)
                continue;
        }
        else
        {
            if (d->extra != r->extra)
                continue;
        }
        return wantEs ? d->es : d->en;
    }

    if (r->kind == ROW_PAGE)
    {
        fallbackEn = "Go to the next page.";
        fallbackEs = "Va a la página siguiente.";
    }
    else if (r->kind == ROW_PREV)
    {
        fallbackEn = "Back to the previous page.";
        fallbackEs = "Vuelve a la página anterior.";
    }
    else if (r->kind == ROW_CLOSE)
    {
        fallbackEn = "Close the panel.";
        fallbackEs = "Cierra el panel.";
    }
    if (fallbackEn != NULL)
        return wantEs ? fallbackEs : fallbackEn;
    return wantEs ? "Sin descripción." : "No description.";
}

/* Wrap the selected row's description into baked lines. */
static void qo_desc_sanitize(const char* src, char* dst, int n)
{
    /* The overlay font path only handles plain ASCII (anything else baked
     * artifacts), so fold Latin-1 accents to their base letter (ñ -> n) and
     * drop every other non-ASCII byte. The reference mds keep full spelling. */
    int i = 0;
    while (*src && i < n - 1)
    {
        unsigned char c = (unsigned char)*src;
        if (c < 0x80)
        {
            if (c >= 0x20 || c == '\n')
                dst[i++] = (char)c;
            src++;
        }
        else if (c == 0xC3 && src[1])
        {
            unsigned char d = (unsigned char)src[1];
            char base = 0;
            if ((d >= 0xA0 && d <= 0xA5) || (d >= 0x80 && d <= 0x85)) base = 'a';
            else if (d == 0xA7 || d == 0x87) base = 'c';
            else if ((d >= 0xA8 && d <= 0xAB) || (d >= 0x88 && d <= 0x8B)) base = 'e';
            else if ((d >= 0xAC && d <= 0xAF) || (d >= 0x8C && d <= 0x8F)) base = 'i';
            else if (d == 0xB1 || d == 0x91) base = 'n';
            else if ((d >= 0xB2 && d <= 0xB6) || d == 0xB0 || (d >= 0x92 && d <= 0x96) || d == 0x90) base = 'o';
            else if ((d >= 0xB9 && d <= 0xBC) || (d >= 0x99 && d <= 0x9C)) base = 'u';
            else if (d == 0xBF || d == 0x9F) base = 'y';
            if (base) dst[i++] = base;
            src += 2;
        }
        else if ((c & 0xE0) == 0xC0 && src[1])
        {
            src += 2;
        }
        else if ((c & 0xF0) == 0xE0 && src[1] && src[2])
        {
            src += 3;
        }
        else if ((c & 0xF8) == 0xF0 && src[1] && src[2] && src[3])
        {
            src += 4;
        }
        else
        {
            src++;
        }
    }
    dst[i] = 0;
}

static void qo_desc_bake(const QoRowDef* r, float px, int wrap)
{
    const char* raw = qo_desc_text(r);
    char clean[512];
    const char* src = clean;
    int len;
    int pos = 0;
    int li;

    if (wrap < 20) wrap = 20;
    qo_desc_sanitize(raw, clean, (int)sizeof(clean));
    len = (int)strlen(src);

    for (li = 0; li < QO_DESC_LINES; li++)
    {
        qo_retire(s_texDesc[li]);
        s_texDesc[li] = 0;
        s_descW[li] = 0;
        s_descH[li] = 0;
    }
    s_descN = 0;

    while (src[pos] && s_descN < QO_DESC_LINES)
    {
        int end, brk, k, n = 0;
        char line[96];

        while (src[pos] == ' ') pos++;
        if (!src[pos]) break;
        end = pos + wrap;
        if (end >= len)
        {
            end = len;
        }
        else
        {
            brk = end;
            while (brk > pos && src[brk] != ' ') brk--;
            if (brk > pos) end = brk; /* break on a space; else hard cut */
        }
        for (k = pos; k < end && n < (int)sizeof(line) - 1; k++) line[n++] = src[k];
        line[n] = 0;
        if (n > 0)
            s_texDesc[s_descN] = qo_bake(line, px, &s_descW[s_descN], &s_descH[s_descN]);
        s_descN++;
        pos = end;
    }
}

/* ------------------------------------------------------------------ */
/* Open / close                                                        */
/* ------------------------------------------------------------------ */

int Pc_QuickOptions_IsOpen(void)
{
    return s_phase != QO_CLOSED;
}

/* Post-open input blackout: the press that opened the panel (pause-menu
 * confirm, or a held pad button) is still down and would otherwise confirm
 * the first row instantly. Pad/keyboard levels are gated; the mouse owns its
 * own edges and is unaffected. */
#define QO_OPEN_INPUT_MS 150u
static Uint32 s_inputHoldUntil = 0;

static void qo_open(void)
{
    if (s_phase == QO_OPENING || s_phase == QO_SHOWN)
        return;
    s_phase      = QO_OPENING;
    s_phaseStart = SDL_GetTicks();
    s_inputHoldUntil = s_phaseStart + QO_OPEN_INPUT_MS;
    s_sel        = 0;
    g_PcQuickOptionsActive = 1;
}

void Pc_QuickOptions_Close(void)
{
    s_ddRow = -1;
    if (s_phase == QO_CLOSED || s_phase == QO_CLOSING)
        return;
    s_phase      = QO_CLOSING;
    s_phaseStart = SDL_GetTicks();
}

void Pc_QuickOptions_Toggle(void)
{
    if (s_phase == QO_CLOSED || s_phase == QO_CLOSING)
        qo_open();
    else
        Pc_QuickOptions_Close();
}

/* Phase machine on the GAME thread, wall clock. It used to live in Draw, and a
 * Draw that bailed (shader failed on ANGLE/ES) left the panel stuck in OPENING
 * with the freeze flag set: game frozen, input swallowed, cursor gone, no way
 * out. Draw only renders now; if GL is unusable the panel closes itself. */
static float qo_phase_tick(void)
{
    Uint32 age = SDL_GetTicks() - s_phaseStart;
    float  dim = 1.0f;

    if (s_glReady < 0)
    {
        static int s_warned;
        if (!s_warned) { s_warned = 1; SH_DBG("[QUICKOPT] GL unavailable -- quick options disabled for this run"); }
        s_phase = QO_CLOSED;
        g_PcQuickOptionsActive = 0;
        return 0.0f;
    }
    if (s_phase == QO_OPENING)
    {
        dim = (float)age / (float)QO_OPEN_MS;
        if (dim >= 1.0f) { dim = 1.0f; s_phase = QO_SHOWN; }
    }
    else if (s_phase == QO_CLOSING)
    {
        dim = 1.0f - (float)age / (float)QO_CLOSE_MS;
        if (dim <= 0.0f) { dim = 0.0f; s_phase = QO_CLOSED; g_PcQuickOptionsActive = 0; }
    }
    return dim;
}

/* ------------------------------------------------------------------ */
/* Input                                                               */
/* ------------------------------------------------------------------ */

/* Rising edge for a scancode, own history so it never fights the pad path. */
static int qo_key_edge(int sc)
{
    static unsigned char s_prev[SDL_NUM_SCANCODES];
    const unsigned char* ks = SDL_GetKeyboardState(NULL);
    int down = ks ? ks[sc] : 0;
    int edge = down && !s_prev[sc];
    s_prev[sc] = (unsigned char)down;
    return edge;
}

static void qo_set_page(int page)
{
    int n;
    s_ddRow = -1;
    s_page = (page + QO_PAGES) % QO_PAGES;
    qo_page_rows(s_page, &n);
    if (s_sel >= n) s_sel = n - 1;
    if (s_sel < 0)  s_sel = 0;
}

/* Confirm / click: cheat rows have their own confirm (the Spawn row fires
 * its browsed entry); everything else steps up. */
static void qo_confirm(const QoRowDef* r);

static void qo_activate(const QoRowDef* r, int dir)
{
    switch (r->kind)
    {
        case ROW_OPT:
        {
            const void* h = PcOpt_QuickFind(r->key);
            if (h) PcOpt_QuickAdjust(h, dir);
            break;
        }
        case ROW_EXTRA: PcOpt_QuickExtraAdjust(r->extra, dir); break;
        case ROW_CHEAT: Pc_Cheats_Adjust(r->cpage, r->extra, dir); break;
        case ROW_PAGE:  qo_set_page(s_page + (dir < 0 ? -1 : +1)); break;
        case ROW_PREV:  qo_set_page(s_page - 1); break;
        case ROW_CLOSE: Pc_QuickOptions_Close(); break;
        case ROW_ACTION: break; /* confirm-only; see the ROW_ACTION comment */
        default: break;
    }
}

/* Directions arrive as HELD state and repeat on the wall clock here. The
 * pad's own pulse repeat counts vblanks per rendered frame, so with an
 * uncapped frame rate a held arrow stepped every few milliseconds. */
#define QO_REPEAT_FIRST_MS 380u
#define QO_REPEAT_MS       85u
static int qo_repeat(int idx, int held)
{
    static int    s_wasHeld[4];
    static Uint32 s_nextAt[4];
    Uint32 now = SDL_GetTicks();
    int fire = 0;

    if (held && !s_wasHeld[idx])
    {
        fire = 1;
        s_nextAt[idx] = now + QO_REPEAT_FIRST_MS;
    }
    else if (held && (Sint32)(now - s_nextAt[idx]) >= 0)
    {
        fire = 1;
        s_nextAt[idx] = now + QO_REPEAT_MS;
    }
    s_wasHeld[idx] = held;
    return fire;
}

/* Rising edge for the pad-derived buttons (confirm/close/page). The input
 * layer asserts clickedBtnFlags every frame a gamepad button reads held, so
 * without this a held accept machine-guns actions and page turns. Mouse
 * clicks and keyboard edges arrive pre-edged and bypass this. */
static int qo_btn_edge(int idx, int held)
{
    static int s_prevBtn[4];
    int fire = held && !s_prevBtn[idx];
    s_prevBtn[idx] = held;
    return fire;
}

void Pc_QuickOptions_Update(int up, int down, int left, int right,
                            int confirm, int close, int pageNext, int pagePrev)
{
    int nRows;
    const QoRowDef* rows = qo_page_rows(s_page, &nRows);
    int mMoved, mClick, mRClick, wheel;
    float mx, my;

    confirm  = qo_btn_edge(0, confirm);
    close    = qo_btn_edge(1, close);
    pageNext = qo_btn_edge(2, pageNext);
    pagePrev = qo_btn_edge(3, pagePrev);

    if ((Sint32)(s_inputHoldUntil - SDL_GetTicks()) > 0)
    {
        up = down = left = right = confirm = close = pageNext = pagePrev = 0;
    }

    /* Advanced has two rows Simple does not, so a switch while parked on one of
     * them would leave the cursor past the end of the array. */
    if (s_sel >= nRows) s_sel = nRows - 1;
    if (s_sel < 0)      s_sel = 0;

    up    = qo_repeat(0, up);
    down  = qo_repeat(1, down);
    left  = qo_repeat(2, left);
    right = qo_repeat(3, right);

    qo_phase_tick();

    /* Keyboard extras (arrows arrive through the pad emulation already). */
    if (qo_key_edge(SDL_SCANCODE_ESCAPE))   close    = 1;
    if (qo_key_edge(SDL_SCANCODE_PAGEDOWN) || qo_key_edge(SDL_SCANCODE_E)) pageNext = 1;
    if (qo_key_edge(SDL_SCANCODE_PAGEUP)   || qo_key_edge(SDL_SCANCODE_Q)) pagePrev = 1;

    if (s_phase != QO_SHOWN) /* ignore input while animating in/out */
        return;

    /* Dropdown open: it owns every input until it closes. */
    if (s_ddRow >= 0 && s_ddRow < nRows && qo_row_is_list(&rows[s_ddRow]))
    {
        const QoRowDef* r = &rows[s_ddRow];
        int n = Pc_Cheats_ListCount(r->cpage, r->extra);
        int pick = -1, mMoved2, mClick2, wheel2;
        float mx2, my2;

        if (close) { s_ddRow = -1; return; }
        if (up)   s_ddSel = (s_ddSel + n - 1) % n;
        if (down) s_ddSel = (s_ddSel + 1) % n;
        /* Keyboard steps the selection; the window follows it. */
        if (s_ddSel < s_ddScroll) s_ddScroll = s_ddSel;
        if (s_ddSel >= s_ddScroll + QO_DD_VISIBLE) s_ddScroll = s_ddSel - QO_DD_VISIBLE + 1;

        /* The wheel scrolls the WINDOW (standard dropdown feel). It used to step
         * the selection instead, which hover re-snapped to the line under the
         * cursor on the next jitter -- so with the pointer resting over the
         * list the wheel felt dead. */
        wheel2 = Pc_MouseCursor_WheelStep();
        if (wheel2) s_ddScroll += (wheel2 > 0) ? -1 : 1;
        {
            int vis = (n < QO_DD_VISIBLE) ? n : QO_DD_VISIBLE;
            if (s_ddScroll > n - vis) s_ddScroll = n - vis;
            if (s_ddScroll < 0) s_ddScroll = 0;
            if (s_ddSel < s_ddScroll) s_ddSel = s_ddScroll;
            if (s_ddSel > s_ddScroll + vis - 1) s_ddSel = s_ddScroll + vis - 1;
        }

        mMoved2 = Pc_MouseCursor_Moved();
        mClick2 = Pc_MouseCursor_LeftClicked();
        if (s_ddShown && (mMoved2 || mClick2 || wheel2) && Pc_MouseCursor_ViewportPos(&mx2, &my2))
        {
            float py  = (1.0f - my2) * s_vpH;
            float mpx = mx2 * s_vpW;
            int   vis = (n < QO_DD_VISIBLE) ? n : QO_DD_VISIBLE;
            int   line = (int)((s_ddTop - py) / s_ddRowH);
            int   inside = mpx >= s_ddL && mpx <= s_ddR && py <= s_ddTop && py >= s_ddTop - vis * s_ddRowH;
            if (inside && line >= 0 && line < vis)
            {
                /* highlight follows the pointer as the list moves under it */
                if (mMoved2 || wheel2) s_ddSel = s_ddScroll + line;
                if (mClick2) pick = s_ddScroll + line;
            }
            else if (mClick2)
            {
                s_ddRow = -1; /* clicked away */
                return;
            }
        }
        if (confirm) pick = s_ddSel;

        if (pick >= 0)
        {
            Pc_Cheats_ListSet(r->cpage, r->extra, pick);
            s_ddRow = -1;
        }
        return;
    }
    s_ddRow = -1;

    if (close) { Pc_QuickOptions_Close(); return; }

    /* Title-bar drag. Held (not clicked) so it tracks continuously, and it is
     * resolved before the row hit-test below so dragging never also activates
     * whatever the cursor passes over. */
    {
        float dmx, dmy;

        if (Pc_MouseCursor_LeftHeld() && Pc_MouseCursor_ViewportPos(&dmx, &dmy))
        {
            float px_ = dmx * s_vpW;
            float py_ = (1.0f - dmy) * s_vpH;

            if (!s_dragging &&
                px_ >= s_geoPanelL && px_ <= s_geoPanelR &&
                py_ >= s_geoTitleB && py_ <= s_geoTitleT)
            {
                s_dragging  = 1;
                s_dragLastX = px_;
                s_dragLastY = py_;
            }
            if (s_dragging)
            {
                s_panelOfsX += px_ - s_dragLastX;
                s_panelOfsY += py_ - s_dragLastY;
                s_dragLastX  = px_;
                s_dragLastY  = py_;
                return; /* the drag owns the mouse this frame */
            }
        }
        else
        {
            s_dragging = 0;
        }
    }

    if (pageNext) qo_set_page(s_page + 1);
    if (pagePrev) qo_set_page(s_page - 1);
    rows = qo_page_rows(s_page, &nRows);

    if (up)   { s_sel = (s_sel + nRows - 1) % nRows; }
    if (down) { s_sel = (s_sel + 1) % nRows; }

    /* Mouse: hover selects, wheel adjusts, left click steps a value up (or
     * runs an action), right click steps it down. */
    mMoved  = Pc_MouseCursor_Moved();
    mClick  = Pc_MouseCursor_LeftClicked();
    mRClick = Pc_MouseCursor_RightClicked();
    wheel   = Pc_MouseCursor_WheelStep();
    if (Pc_MouseCursor_ViewportPos(&mx, &my) && s_geoRowPitch > 0.0f)
    {
        float py  = (1.0f - my) * s_vpH; /* top-left norm -> bottom-left px */
        float mpx = mx * s_vpW;
        int   row = (int)((s_geoListT - py) / s_geoRowPitch);
        int   inX = mpx >= s_geoPanelL && mpx <= s_geoPanelR;
        if (inX && row >= 0 && row < nRows)
        {
            if (mMoved) s_sel = row;
            if (mClick)
            {
                s_sel = row;
                if (qo_row_is_list(&rows[row]) && mpx > (s_geoPanelL + s_geoPanelR) * 0.5f)
                {
                    /* Right half of a list row: open the dropdown on the value. */
                    s_ddRow    = row;
                    s_ddSel    = Pc_Cheats_ListGet(rows[row].cpage, rows[row].extra);
                    s_ddScroll = s_ddSel - QO_DD_VISIBLE / 2;
                    if (s_ddScroll < 0) s_ddScroll = 0;
                    s_ddShown  = 0;
                }
                else
                    qo_confirm(&rows[row]);
            }
            if (mRClick && (QO_IS_VALUE_ROW(rows[row].kind) || rows[row].kind == ROW_PAGE || rows[row].kind == ROW_PREV))
            { s_sel = row; qo_activate(&rows[row], -1); }
            if (wheel && QO_IS_VALUE_ROW(rows[row].kind))
                qo_activate(&rows[row], wheel > 0 ? +1 : -1);
        }
    }

    /* The page rows adjust like values: Left goes back a page,
     * Right/confirm go forward (Prev rows always go back). */
    if (QO_IS_VALUE_ROW(rows[s_sel].kind) || rows[s_sel].kind == ROW_PAGE || rows[s_sel].kind == ROW_PREV)
    {
        if (left)  qo_activate(&rows[s_sel], -1);
        if (right) qo_activate(&rows[s_sel], +1);
        if (confirm) qo_confirm(&rows[s_sel]);
    }
    else if (confirm)
    {
        qo_confirm(&rows[s_sel]);
    }
}

static void qo_confirm(const QoRowDef* r)
{
    if (r->kind == ROW_CHEAT)
        Pc_Cheats_Confirm(r->cpage, r->extra);
    else if (r->kind == ROW_PREV)
        qo_activate(r, -1);
    else if (r->kind == ROW_ACTION)
    {
        if (r->extra == QO_A_VIEWRESET)
            PcOpt_QuickViewReset();
        else if (r->extra == QO_A_GIVEWEAPON)
            PcOpt_TrainerGiveWeapon();
        else if (r->extra == QO_A_GIVEMELEE)
            PcOpt_TrainerGiveMelee();
        else if (r->extra == QO_A_GIVEHEAL)
            PcOpt_TrainerGiveHeal();
    }
    else
        qo_activate(r, +1);
}

/* ------------------------------------------------------------------ */
/* Draw                                                                */
/* ------------------------------------------------------------------ */

/* Claim the atlas EARLY, at startup, rather than on the first time the panel
 * is opened.
 *
 * The atlas was being written over while alive: the text drew correctly for a
 * frame and then went black, with the texture never deleted (glIsTexture never
 * fails and the atlas is created exactly once). A live texture whose contents
 * are replaced means something RENDERS INTO it -- and the way that happens is a
 * recycled name. PsyCross creates and destroys framebuffer targets as the
 * resolution and MSAA state settle (GR_DestroyInternalTarget frees the internal
 * colour and resolve textures), and a framebuffer object elsewhere can still
 * refer to a freed name. Allocating our atlas AFTER that churn means we can be
 * handed such a name, and the next pass through that framebuffer paints the
 * scene into our glyphs: black in the cafe, red in an otherworld map.
 *
 * Claiming the name before any of that churn happens means it is never in the
 * free pool at a moment when a stale attachment could point at it. MSAA made
 * this reliable rather than occasional because it adds another create/destroy
 * pair to the same window. */
void Pc_QuickOptions_PreloadGL(void)
{
    if (!s_glReady) qo_gl_init();
    if (s_glReady != 1) return;
    qo_atlas_ensure();
    qo_build_white();
}
void Pc_QuickOptions_Draw(void)
{
    GLint vp[4];
    float vpW, vpH, panelW, panelH, panelL, panelR, panelT, panelB;
    float titleH, hintH, listT, listB, listH, rowPitch, rowH, pad, dim = 1.0f;
    int   nRows;
    const QoRowDef* rows = qo_page_rows(s_page, &nRows);
    int   px, i;

    GLint  prevProg = 0, prevVao = 0, prevBuf = 0, prevTex = 0, prevUnit = GL_TEXTURE0, prevAlign = 4;
    GLint  prevSrcRgb = GL_ONE, prevDstRgb = GL_ZERO, prevSrcA = GL_ONE, prevDstA = GL_ZERO;
    GLint  prevEqRgb = GL_FUNC_ADD, prevEqA = GL_FUNC_ADD;
    GLboolean prevBlend, prevDepth, prevCull;

    if (s_phase == QO_CLOSED)
        return;

    glGetIntegerv(GL_VIEWPORT, vp);
    if (vp[2] <= 0 || vp[3] <= 0)
        return;
    vpW = (float)vp[2];
    vpH = (float)vp[3];

    if (!s_glReady) qo_gl_init();
    if (s_glReady != 1) return;

    /* The atlas is created once and never deleted BY US -- but something else
     * frees its name. Binding a freed name creates a brand new EMPTY texture
     * object, and an empty texture samples as (0,0,0,1): opaque black over
     * every quad. That is the block corruption, and it survived the rewrite
     * because one texture can be lost exactly as easily as twenty-five.
     *
     * Checked BEFORE anything binds the atlas -- once a freed name is bound the
     * object springs back as a valid but EMPTY texture and glIsTexture can no
     * longer tell. One call per frame, and it does not stall.
     *
     * The panel background cannot show this by eye: it is drawn with a
     * near-black low-alpha colour, so white and opaque-black give the same dark
     * translucent panel. Only the text ever revealed it. */
    if (s_atlasTex != 0 && !glIsTexture(s_atlasTex))
    {
        static int s_lostLogs = 0;
        if (s_lostLogs < 8)
        {
            s_lostLogs++;
            SH_DBG("[QOTEX] atlas texture %u was DELETED by something else -- recreating",
                   (unsigned)s_atlasTex);
        }
        s_atlasTex   = 0;
        s_atlasReady = 0;
        qo_free_text();   /* every slot handle refers to the dead atlas */
        s_texWhite   = 0;
        s_texCursor  = 0;
        qo_atlas_ensure();
        qo_build_white();
    }
    if (!s_fontsTried) qo_fonts_init();

    if (s_sel >= nRows) s_sel = nRows - 1;
    if (s_sel < 0)      s_sel = 0;

    /* Either a bake failed for want of atlas room, or the Control Type switch
     * changed which rows this page has. Both are fixed the same way: drop every
     * cached handle so the page rebuilds from an empty atlas this frame, rather
     * than drawing gaps or the previous mode's labels. */
    if (s_atlasExhausted || s_viewRowsDirty)
    {
        s_viewRowsDirty = 0;
        qo_free_text();
    }

    glGetIntegerv(GL_CURRENT_PROGRAM, &prevProg);
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &prevVao);
    glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &prevBuf);
    glGetIntegerv(GL_ACTIVE_TEXTURE, &prevUnit);
    glActiveTexture(GL_TEXTURE0);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &prevTex);
    glGetIntegerv(GL_UNPACK_ALIGNMENT, &prevAlign);
    glGetIntegerv(GL_BLEND_SRC_RGB, &prevSrcRgb);
    glGetIntegerv(GL_BLEND_DST_RGB, &prevDstRgb);
    glGetIntegerv(GL_BLEND_SRC_ALPHA, &prevSrcA);
    glGetIntegerv(GL_BLEND_DST_ALPHA, &prevDstA);
    glGetIntegerv(GL_BLEND_EQUATION_RGB, &prevEqRgb);
    glGetIntegerv(GL_BLEND_EQUATION_ALPHA, &prevEqA);
    prevBlend = glIsEnabled(GL_BLEND);
    prevDepth = glIsEnabled(GL_DEPTH_TEST);
    prevCull  = glIsEnabled(GL_CULL_FACE);

    qo_gl_pump();
    qo_validate_cache();

    /* Fade only; the phase itself advances in Update (game thread). */
    {
        Uint32 age = SDL_GetTicks() - s_phaseStart;
        if (s_phase == QO_OPENING)
        {
            dim = (float)age / (float)QO_OPEN_MS;
            if (dim > 1.0f) dim = 1.0f;
        }
        else if (s_phase == QO_CLOSING)
        {
            dim = 1.0f - (float)age / (float)QO_CLOSE_MS;
            if (dim <= 0.0f) goto restore;
        }
    }

    /* Layout (viewport px, origin bottom-left). Left-anchored rather than
     * centred so the scene stays visible beside it while you tune. */
    panelH = 0.74f * vpH;
    panelW = 0.80f * panelH;
    if (panelW > 0.90f * vpW) panelW = 0.90f * vpW;
    panelL = vpW * 0.04f;
    panelB = (vpH - panelH) * 0.5f;

    /* User drag offset, clamped so a good part of the panel always stays on
     * screen (never let it be dragged fully out of reach). */
    {
        const float minL = -panelW * 0.75f;
        const float maxL = vpW - panelW * 0.25f;
        const float minB = -panelH * 0.75f;
        const float maxB = vpH - panelH * 0.25f;

        panelL += s_panelOfsX;
        panelB += s_panelOfsY;
        if (panelL < minL) { s_panelOfsX += minL - panelL; panelL = minL; }
        if (panelL > maxL) { s_panelOfsX += maxL - panelL; panelL = maxL; }
        if (panelB < minB) { s_panelOfsY += minB - panelB; panelB = minB; }
        if (panelB > maxB) { s_panelOfsY += maxB - panelB; panelB = maxB; }
    }

    panelR = panelL + panelW;
    panelT = panelB + panelH;

    pad      = panelW * 0.05f;
    titleH   = panelH * 0.09f;
    hintH    = panelH * 0.07f;
    listT    = panelT - titleH;
    listB    = panelB + hintH;
    listH    = listT - listB;
    rowPitch = listH / (float)nRows;
    rowH     = rowPitch * 0.84f;
    px       = (int)(rowH * 0.52f);
    if (px < 8) px = 8;

    if (s_bakedForPx != px || s_bakedForPage != s_page)
    {
        qo_free_text();
        s_bakedForPx   = px;
        s_bakedForPage = s_page;
    }
    if (!s_texTitle)
        s_texTitle = qo_bake(s_pageTitles[s_page], (float)(int)(titleH * 0.46f), &s_titleW, &s_titleH);
    /* Controls footer. It used to run off-screen at some panel widths, so bake
     * it once at the natural size and, if it overflows, re-bake once scaled to
     * fit -- the text always ends up inside the panel whatever its width. */
    if (!s_texHint)
    {
        char  hint[192];
        float avail = panelW - 2.0f * pad;
        int   hpx   = (int)(hintH * 0.42f);

        if (hpx < 7) hpx = 7;
        snprintf(hint, sizeof(hint),
                 "Up/Down select   Left/Right adjust   PgUp/PgDn page   drag title to move   %s or Esc close   * req restart",
                 g_PcConfig.keyQuickOptions[0] ? g_PcConfig.keyQuickOptions : "F10");
        s_texHint = qo_bake(hint, (float)hpx, &s_hintW, &s_hintH);
        if (s_texHint && s_hintW > avail && s_hintW > 0 && avail > 0.0f)
        {
            int fit = (int)((float)hpx * avail / (float)s_hintW);
            if (fit < 6)   fit = 6;
            if (fit > hpx) fit = hpx;
            s_texHint = qo_bake(hint, (float)fit, &s_hintW, &s_hintH);
        }
    }

    /* Publish geometry for Update's mouse hit-test. */
    s_vpW = vpW; s_vpH = vpH;
    s_geoListT = listT; s_geoRowPitch = rowPitch; s_geoRowH = rowH;
    s_geoPanelL = panelL; s_geoPanelR = panelR; s_geoRows = nRows;
    s_geoTitleT = panelT; s_geoTitleB = panelT - titleH;

    qo_build_white();

    glUseProgram(s_prog);
    glBindVertexArray(s_vao);
    glBindBuffer(GL_ARRAY_BUFFER, s_vbo);
    /* Re-assert the vertex layout every draw instead of trusting the VAO to
     * have preserved it.
     *
     * The shader is texture2D(u_tex, v_uv) * u_color, so everything depends on
     * a_uv (attribute 1). If that array is left disabled or repointed by other
     * GL code, v_uv collapses to a single value and every vertex samples ONE
     * texel -- and when that texel sits inside a glyph, the quad paints a solid
     * rectangle in u_color: the grey and red blotches, exactly the size of each
     * label, with the geometry still perfect because a_pos (attribute 0) is
     * unaffected.
     *
     * This is why every texture-side check came back clean: the name, size,
     * pixels and sampler state were all genuinely correct. It is also why the
     * panel background never showed it -- that quad samples a 1x1 texture,
     * where ANY uv resolves to the same texel, so broken uvs are invisible on
     * it and visible on everything else. */
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glBlendEquation(GL_FUNC_ADD);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);

#define NX(px_) (-1.0f + 2.0f * (px_) / vpW)
#define NY(py_) (-1.0f + 2.0f * (py_) / vpH)

    /* Translucent panel -- no screen dim, so a change shows in the live scene. */
    qo_quad(s_texWhite, NX(panelL), NY(panelT), NX(panelR), NY(panelB), 0.04f, 0.04f, 0.05f, 0.58f * dim);
    /* Title band + rule. */
    qo_quad(s_texWhite, NX(panelL + 2.0f), NY(panelT - 2.0f), NX(panelR - 2.0f), NY(listT),
            0.10f, 0.05f, 0.05f, 0.70f * dim);
    qo_quad(s_texWhite, NX(panelL + 2.0f), NY(listT), NX(panelR - 2.0f), NY(listT - 2.0f),
            0.47f, 0.11f, 0.08f, dim);
    if (s_texTitle)
    {
        float tx = panelL + (panelW - (float)s_titleW) * 0.5f;
        float ty = panelT - titleH * 0.30f;
        qo_quad(s_texTitle, NX(tx), NY(ty), NX(tx + s_titleW), NY(ty - s_titleH), 1.0f, 0.93f, 0.86f, dim);
    }

    /* Description panel source: re-bake on selection, page, language or
     * available-width change. Fixed viewport-derived size (x2.5 over the
     * original), independent from the sibling panel. */
    {
        int lang = (g_PcConfig.language == 3) ? 1 : 0;
        int dpx = (int)(vpH * 0.035f);
        int wrap;
        float availW = vpW - 8.0f - (panelR + 14.0f);
        if (dpx < 12) dpx = 12;
        wrap = (int)(availW / ((float)dpx * 0.55f));
        if (wrap > 60) wrap = 60;
        if (wrap < 20)
        {
            /* Too narrow for even a slim box: drop the panel. */
            if (s_descN > 0)
            {
                int li;
                for (li = 0; li < QO_DESC_LINES; li++)
                {
                    qo_retire(s_texDesc[li]);
                    s_texDesc[li] = 0;
                }
                s_descN = 0;
                s_descMaxW = 0;
            }
            s_descWrap = wrap;
        }
        else if (s_sel >= 0 && s_sel < nRows &&
            (s_descSel != s_sel || s_descPage != s_page || s_descLang != lang || s_descWrap != wrap))
        {
            int li;
            qo_desc_bake(&rows[s_sel], (float)dpx, wrap);
            s_descMaxW = 0;
            for (li = 0; li < s_descN; li++)
            {
                if (s_descW[li] > s_descMaxW)
                    s_descMaxW = s_descW[li];
            }
            s_descSel = s_sel;
            s_descPage = s_page;
            s_descLang = lang;
            s_descWrap = wrap;
        }
    }

    for (i = 0; i < nRows && i < QO_MAX_ROWS; i++)
    {
        const QoRowDef* r = &rows[i];
        float rowTop = listT - (float)i * rowPitch;
        float rowMid = rowTop - rowH * 0.5f;
        float tH, tY;
        char  txt[64];

        if (i == s_sel)
            qo_quad(s_texWhite, NX(panelL + 4.0f), NY(rowTop), NX(panelR - 4.0f), NY(rowTop - rowH),
                    0.42f, 0.16f, 0.12f, 0.55f * dim);

        if (!s_texLabel[i])
        {
            qo_row_name(r, txt, (int)sizeof(txt));
            s_texLabel[i] = qo_bake(txt, (float)px, &s_labelW[i], &s_labelH[i]);
        }

        if (QO_IS_VALUE_ROW(r->kind))
        {
            qo_row_value(r, txt, (int)sizeof(txt));
            if (s_texValue[i] == 0 || strcmp(txt, s_valueText[i]) != 0)
            {
                qo_retire(s_texValue[i]);
                snprintf(s_valueText[i], sizeof(s_valueText[i]), "%s", txt);
                s_texValue[i] = qo_bake(txt, (float)px, &s_valueW[i], &s_valueH[i]);
            }
            /* Label left. A list row's label is a BUTTON (the action lives on
             * it; the value on the right only browses), drawn as a boxed pill. */
            if (s_texLabel[i])
            {
                tH = (float)s_labelH[i]; tY = rowMid + tH * 0.5f;
                if (qo_row_is_list(r))
                {
                    float bl = panelL + pad - 6.0f, br = panelL + pad + (float)s_labelW[i] + 6.0f;
                    float bt = rowTop - rowH * 0.08f, bb = rowTop - rowH * 0.92f;
                    float sel = (i == s_sel) ? 1.0f : 0.6f;
                    qo_quad(s_texWhite, NX(bl), NY(bt), NX(br), NY(bb), 0.47f, 0.11f, 0.08f, 0.55f * sel * dim);
                    qo_quad(s_texWhite, NX(bl), NY(bt), NX(br), NY(bt - 1.0f), 1.0f, 0.93f, 0.86f, 0.35f * dim);
                    qo_quad(s_texWhite, NX(bl), NY(bb + 1.0f), NX(br), NY(bb), 1.0f, 0.93f, 0.86f, 0.35f * dim);
                    qo_quad(s_texWhite, NX(bl), NY(bt), NX(bl + 1.0f), NY(bb), 1.0f, 0.93f, 0.86f, 0.35f * dim);
                    qo_quad(s_texWhite, NX(br - 1.0f), NY(bt), NX(br), NY(bb), 1.0f, 0.93f, 0.86f, 0.35f * dim);
                }
                qo_quad(s_texLabel[i], NX(panelL + pad), NY(tY), NX(panelL + pad + s_labelW[i]), NY(tY - tH),
                        0.92f, 0.92f, 0.95f, dim);
            }
            if (s_texValue[i])
            {
                float vg = (i == s_sel) ? 1.0f : 0.85f;
                float vr = panelR - pad - (float)s_valueW[i];
                tH = (float)s_valueH[i]; tY = rowMid + tH * 0.5f;
                qo_quad(s_texValue[i], NX(vr), NY(tY), NX(vr + s_valueW[i]), NY(tY - tH),
                        vg, vg * 0.85f, vg * 0.45f, dim);
            }
        }
        else if (s_texLabel[i])
        {
            /* Action row, centred. */
            float lx = panelL + (panelW - (float)s_labelW[i]) * 0.5f;
            tH = (float)s_labelH[i]; tY = rowMid + tH * 0.5f;
            qo_quad(s_texLabel[i], NX(lx), NY(tY), NX(lx + s_labelW[i]), NY(tY - tH),
                    0.80f, 0.85f, 0.95f, dim);
        }
    }

    if (s_texHint)
    {
        float hx = panelL + (panelW - (float)s_hintW) * 0.5f;
        float hy = panelB + hintH * 0.72f;
        qo_quad(s_texHint, NX(hx), NY(hy), NX(hx + s_hintW), NY(hy - s_hintH), 0.7f, 0.7f, 0.75f, dim);
    }

    /* Description panel: hugs the main panel's right edge, sized to the baked
     * content (not the viewport). Skipped when it would run off-screen. */
    {
        float descL, descR, descPad, lineY, descH = 0.0f, boxW;
        int li;

        descL = panelR + 14.0f;
        if (descL < vpW - 8.0f && s_descN > 0 && s_descMaxW > 0)
        {
            for (li = 0; li < s_descN; li++)
                descH += (float)s_descH[li] + 6.0f;
            boxW = (float)s_descMaxW + 24.0f;
            if (descL + boxW > vpW - 8.0f)
                boxW = vpW - 8.0f - descL;
            descPad = 12.0f;
            descR = descL + boxW;
            qo_quad(s_texWhite, NX(descL), NY(listT), NX(descR), NY(listT - descH - descPad),
                    0.04f, 0.04f, 0.05f, 0.58f * dim);
            lineY = listT - descPad * 0.5f;
            for (li = 0; li < s_descN; li++)
            {
                if (s_texDesc[li])
                {
                    float tH = (float)s_descH[li];
                    qo_quad(s_texDesc[li], NX(descL + descPad), NY(lineY),
                            NX(descL + descPad + s_descW[li]), NY(lineY - tH),
                            0.85f, 0.87f, 0.90f, dim);
                }
                lineY -= (float)s_descH[li] + 6.0f;
            }
        }
    }

    /* Dropdown list over the value column of its row, on top of the rows. */
    s_ddShown = 0;
    if (s_ddRow >= 0 && s_ddRow < nRows && qo_row_is_list(&rows[s_ddRow]))
    {
        const QoRowDef* r  = &rows[s_ddRow];
        int   n   = Pc_Cheats_ListCount(r->cpage, r->extra);
        int   vis = (n < QO_DD_VISIBLE) ? n : QO_DD_VISIBLE;
        float ddL = panelL + panelW * 0.5f;
        float ddR = panelR - 4.0f;
        float ddTop = listT - (float)(s_ddRow + 1) * rowPitch + rowPitch * 0.08f;
        float ddH = rowH;
        int   k;

        if (n > QO_DD_MAX) n = QO_DD_MAX;
        if (s_ddScroll > n - vis) s_ddScroll = n - vis;
        if (s_ddScroll < 0) s_ddScroll = 0;
        /* Don't run off the panel bottom: open upward instead. */
        if (ddTop - vis * ddH < panelB) ddTop = listT - (float)s_ddRow * rowPitch + vis * ddH;

        qo_quad(s_texWhite, NX(ddL - 2.0f), NY(ddTop + 2.0f), NX(ddR + 2.0f), NY(ddTop - vis * ddH - 2.0f), 0.47f, 0.11f, 0.08f, dim);
        qo_quad(s_texWhite, NX(ddL), NY(ddTop), NX(ddR), NY(ddTop - vis * ddH), 0.05f, 0.045f, 0.06f, 0.97f * dim);
        for (k = 0; k < vis; k++)
        {
            int   e  = s_ddScroll + k;
            float et = ddTop - (float)k * ddH;
            if (e >= n) break;
            if (e == s_ddSel)
                qo_quad(s_texWhite, NX(ddL + 2.0f), NY(et), NX(ddR - 2.0f), NY(et - ddH), 0.42f, 0.16f, 0.12f, 0.75f * dim);
            if (!s_ddTex[e])
                s_ddTex[e] = qo_bake(Pc_Cheats_ListName(r->cpage, r->extra, e), (float)px, &s_ddW[e], &s_ddH[e]);
            if (s_ddTex[e])
            {
                float ty = et - (ddH - (float)s_ddH[e]) * 0.5f;
                qo_quad(s_ddTex[e], NX(ddL + 10.0f), NY(ty), NX(ddL + 10.0f + s_ddW[e]), NY(ty - s_ddH[e]),
                        e == s_ddSel ? 1.0f : 0.85f, 0.9f, 0.9f, dim);
            }
        }
        s_ddL = ddL; s_ddR = ddR; s_ddTop = ddTop; s_ddRowH = ddH;
        s_ddShown = 1;
    }

    /* Mouse cursor last, so it rides above the panel (the PSX-drawn one is
     * composited under every overlay). Nearest keeps the sprite's pixel look. */
    if (!s_texCursor)
    {
        unsigned char rgba[32 * 32 * 4];
        if (Pc_MouseCursor_SpriteRgba(rgba))
            s_texCursor = qo_upload_rgba(rgba, 32, 32, 1);
    }
    if (s_texCursor)
    {
        float cx, cy, cw, ch;
        if (Pc_MouseCursor_GlRect(vpW, vpH, &cx, &cy, &cw, &ch))
            qo_quad(s_texCursor, NX(cx), NY(cy), NX(cx + cw), NY(cy - ch), 1.0f, 1.0f, 1.0f, 1.0f);
    }

#undef NX
#undef NY

restore:
    /* Restore GL state so PsyCross / the console are untouched. */
    glBindTexture(GL_TEXTURE_2D, (GLuint)prevTex);
    glActiveTexture((GLenum)prevUnit);
    glPixelStorei(GL_UNPACK_ALIGNMENT, prevAlign);
    glBlendEquationSeparate((GLenum)prevEqRgb, (GLenum)prevEqA);
    glBlendFuncSeparate((GLenum)prevSrcRgb, (GLenum)prevDstRgb, (GLenum)prevSrcA, (GLenum)prevDstA);
    if (!prevBlend) glDisable(GL_BLEND);
    if (prevDepth)  glEnable(GL_DEPTH_TEST);
    if (prevCull)   glEnable(GL_CULL_FACE);
    glUseProgram((GLuint)prevProg);
    glBindVertexArray((GLuint)prevVao);
    glBindBuffer(GL_ARRAY_BUFFER, (GLuint)prevBuf);

    /* PsyCross keeps a redundant-bind cache and RETURNS EARLY from
     * GR_SetTexture when it believes the texture is already bound. We just
     * bound our own atlas behind that cache, so the binding it restores
     * above is not necessarily what the cache thinks. Invalidate it and the
     * next GR_SetTexture is forced to issue a real glBindTexture. */
    {
        extern unsigned int g_lastBoundTexture; /* PsyX TextureID */
        g_lastBoundTexture = (unsigned int)-1;
    }
}
