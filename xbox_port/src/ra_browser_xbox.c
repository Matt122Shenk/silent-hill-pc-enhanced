/*
 * ra_browser_xbox.c - the RetroAchievements list, drawn with the game's font.
 *
 * The shared title screen (title.c) already drives the PC achievement browser
 * through Pc_RaBrowser_Open / IsOpen / Update: the Map button opens it, the pad
 * belongs to it while it is up, and attract demos hold off. On PC those hooks
 * reach a GL panel (pc_ra_browser.c); here they reach this file, which draws
 * the same information as ordinary 2D prims in OT2 -- a darkened panel in
 * layer 7 under the glyphs that Gfx_StringDraw puts in layer 6 -- so it needs
 * no GPU code of its own. Rendered from DbgOverlay_XboxRender (main menu on).
 *
 * Text-only by design: the badge path (ra_badge_xbox.c) has one texture and a
 * blocking fetch, which suits one unlock card, not a scrolling list.
 */
#include "game.h"
#include "bodyprog/bodyprog.h"
#include "bodyprog/screen/screen_data.h"
#include "bodyprog/sound/sound_system.h"
#include "bodyprog/text/text_draw.h"

#include <string.h>

typedef struct {
    char     title[48];
    char     desc[96];
    unsigned points;
    int      unlocked;
} XbRaAch;

extern int         Xbox_Ra_Snapshot(XbRaAch* out, int max);   /* ra_xbox.c */
extern const char* Pc_Ra_UnavailableReason(void);              /* ra_xbox.c */

#define RB_MAX        96
#define RB_ROWS       9
#define RB_ROW_Y0     44
#define RB_ROW_STEP   15
#define RB_DESC_Y     184
#define RB_LINE_CHARS 34

static XbRaAch s_list[RB_MAX];
static int     s_count;
static int     s_open;
static int     s_sel;
static int     s_top;
static int     s_unlockedCount;
static unsigned s_pointsHave, s_pointsAll;

/* Map arbitrary text onto what the 12x16 atlas renders (dbg_overlay_xbox.c's
 * rule): space -> '_' (the drawer's space advance), the ASCII glyph range and
 * '!' '&' kept, anything else -> '?'. */
static void Rb_Sanitize(char* dst, const char* src, int cap)
{
    int i;

    for (i = 0; i < cap - 1 && src[i] != '\0'; i++)
    {
        unsigned char c = (unsigned char)src[i];

        if (c == ' ')
            dst[i] = '_';
        else if (c == '!' || c == '&' || (c >= 0x27 && c <= 0x7A))
            dst[i] = (char)c;
        else
            dst[i] = '?';
    }
    dst[i] = '\0';
}

static void Rb_Line(int x, int y, s16 color, const char* text, int maxGlyphs)
{
    char buf[RB_LINE_CHARS + 32];

    Rb_Sanitize(buf, text, (int)sizeof(buf));
    Gfx_StringSetColor(color);
    Gfx_StringSetPosition(x, y);
    Gfx_StringDraw(buf, maxGlyphs);
}

static void Rb_UInt(char* dst, unsigned v)
{
    char tmp[12];
    int  n = 0, j;

    do { tmp[n++] = (char)('0' + v % 10u); v /= 10u; } while (v && n < 11);
    for (j = 0; j < n; j++)
        dst[j] = tmp[n - 1 - j];
    dst[n] = '\0';
}

void Pc_RaBrowser_Open(void)
{
    int i;

    s_count = Xbox_Ra_Snapshot(s_list, RB_MAX);
    s_unlockedCount = 0;
    s_pointsHave = s_pointsAll = 0;
    for (i = 0; i < s_count; i++)
    {
        s_pointsAll += s_list[i].points;
        if (s_list[i].unlocked)
        {
            s_unlockedCount++;
            s_pointsHave += s_list[i].points;
        }
    }
    s_sel  = 0;
    s_top  = 0;
    s_open = 1;
    Sd_PlaySfx(Sfx_MenuConfirm, 0, 64);
}

int Pc_RaBrowser_IsOpen(void)
{
    return s_open;
}

void Pc_RaBrowser_Update(int closeRequested, int up, int down, int confirm)
{
    (void)confirm;

    if (!s_open)
        return;
    if (closeRequested)
    {
        s_open = 0;
        Sd_PlaySfx(Sfx_MenuCancel, 0, 64);
        return;
    }
    if (s_count <= 0)
        return;
    if (up && s_sel > 0)
    {
        s_sel--;
        Sd_PlaySfx(Sfx_MenuMove, 0, 64);
    }
    if (down && s_sel < s_count - 1)
    {
        s_sel++;
        Sd_PlaySfx(Sfx_MenuMove, 0, 64);
    }
    if (s_sel < s_top)
        s_top = s_sel;
    if (s_sel >= s_top + RB_ROWS)
        s_top = s_sel - RB_ROWS + 1;
}

/* Darken the area behind the list: one subtractive TILE (abr 2, B - F) in OT2
 * layer 7, which the walk draws before the text's layer 6. */
static void Rb_Panel(void)
{
    static TILE     s_tile[2];
    static DR_TPAGE s_tp[2];
    const int       buf = g_ActiveBufferIdx;

    setTile(&s_tile[buf]);
    setSemiTrans(&s_tile[buf], 1);
    setRGB0(&s_tile[buf], 112, 112, 112);
    setXY0(&s_tile[buf], -(SCREEN_WIDTH / 2) + 6, -(SCREEN_HEIGHT / 2) + 10);
    setWH(&s_tile[buf], SCREEN_WIDTH - 12, SCREEN_HEIGHT - 20);
    AddPrim(&g_OtTags0[buf][7], &s_tile[buf]);
    setDrawTPage(&s_tp[buf], 0, 1, getTPageN(0, 2, 0, 0));
    AddPrim(&g_OtTags0[buf][7], &s_tp[buf]);
}

/* Word-wrap `text` into up to two lines of RB_LINE_CHARS. */
static void Rb_Desc(const char* text)
{
    char line[RB_LINE_CHARS + 1];
    int  len = (int)strlen(text), start = 0, row;

    for (row = 0; row < 2 && start < len; row++)
    {
        int end = start + RB_LINE_CHARS;
        if (end >= len)
        {
            end = len;
        }
        else
        {
            int cut = end;
            while (cut > start && text[cut] != ' ')
                cut--;
            if (cut > start)
                end = cut;
        }
        memcpy(line, text + start, (size_t)(end - start));
        line[end - start] = '\0';
        Rb_Line(14, RB_DESC_Y + row * 15, StringColorId_LightGrey, line, RB_LINE_CHARS);
        start = end;
        while (start < len && text[start] == ' ')
            start++;
    }
}

void Xbox_RaBrowser_Render(void)
{
    char head[64];
    int  i;

    if (!s_open)
        return;

    Rb_Panel();

    if (s_count <= 0)
    {
        const char* why = Pc_Ra_UnavailableReason();
        Rb_Line(14, 20, StringColorId_Gold, "RetroAchievements", 30);
        Rb_Line(14, 60, StringColorId_White, (why && why[0]) ? why : "No achievements to show", RB_LINE_CHARS);
        return;
    }

    strcpy(head, "Achievements ");
    Rb_UInt(head + strlen(head), (unsigned)s_unlockedCount);
    strcat(head, "/");
    Rb_UInt(head + strlen(head), (unsigned)s_count);
    strcat(head, "  ");
    Rb_UInt(head + strlen(head), s_pointsHave);
    strcat(head, "/");
    Rb_UInt(head + strlen(head), s_pointsAll);
    strcat(head, " pts");
    Rb_Line(14, 20, StringColorId_Gold, head, 40);

    for (i = 0; i < RB_ROWS && s_top + i < s_count; i++)
    {
        const XbRaAch* a = &s_list[s_top + i];
        char           row[RB_LINE_CHARS + 16];
        s16            color;

        row[0] = a->unlocked ? '+' : '-';
        row[1] = ' ';
        strncpy(row + 2, a->title, RB_LINE_CHARS - 8);
        row[RB_LINE_CHARS - 6] = '\0';
        strcat(row, " (");
        Rb_UInt(row + strlen(row), a->points);
        strcat(row, ")");

        color = (s_top + i == s_sel) ? StringColorId_Gold
              : a->unlocked          ? StringColorId_White
              :                        StringColorId_DarkGrey;
        Rb_Line(14, RB_ROW_Y0 + i * RB_ROW_STEP, color, row, RB_LINE_CHARS + 4);
    }

    Rb_Desc(s_list[s_sel].desc);
}
