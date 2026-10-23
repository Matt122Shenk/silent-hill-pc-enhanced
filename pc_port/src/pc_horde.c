/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "game.h"

#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>

#include "bodyprog/chara/chara.h"
#include "bodyprog/collision/collision.h"
#include "bodyprog/collision/ray.h"
#include "bodyprog/formats/ipd.h"
#include "bodyprog/items.h"
#include "bodyprog/map/map.h"
#include "bodyprog/math/math.h"
#include "bodyprog/sound/sound_system.h"
#include "bodyprog/sound/sfx_id_enum.h"
#include "pc_chara_pool.h"
#include "pc_config.h"
#include "pc_horde.h"
#include "pc_item_toast.h"
#include "sh_log.h"

extern void Inventory_AddSpecialItem(u8 itemId, u8 itemCount);
extern u8 g_Player_DisableControl;

/* Arena: Old Silent Hill streets (post-cafe). Arrival reuses an authored
 * door record into the map, so always on valid ground. */
#define HORDE_ARENA_MAP MapIdx_MAP2_S00
#define HORDE_ARRIVE_X  Q12(30.0f)
#define HORDE_ARRIVE_Z  Q12(154.0f)

/* Locked-door buzz throttle: Harry can stand inside a door trigger while
 * vetoed, which would otherwise machine-gun the error SFX every frame. */
#define HORDE_BUZZ_MS   500u
/* Inter-wave countdown. */
#define HORDE_COUNTDOWN_MS 3000u

static int s_running;
static int s_requested;
static int s_poolForced;
static int s_waveSpawned;

/* Wave roster: slot membership is stamped with the wave number, so slot reuse
 * across waves can never alias. kills is monotonic per run. */
static u32 s_kills;
static u32 s_wave;
static int s_waveTotal;
static u8 s_live[NPC_COUNT_MAX];
static u8 s_seen[NPC_COUNT_MAX];
static u32 s_slotWave[NPC_COUNT_MAX];
static Uint32 s_leashSince[NPC_COUNT_MAX];
static int s_testPhase;
static Uint32 s_testDeadline;

typedef enum { HS_ARRIVE = 0, HS_COUNTDOWN, HS_FIGHT, HS_DRAFT } e_HordeTestPhase;

/* Threat: enemy HP/damage pct, x1.25 per wave, capped so q19_12 never
 * overflows on endless runs. */
#define HORDE_THREAT_MAX 1000
static unsigned int s_threatPct;

/* Player damage mult, stacked from draft rewards, pct of base. */
#define HORDE_DMG_MAX 10000u
static unsigned int s_playerDmgPct;

/* Damage Harry takes, pct of the threat-scaled hit, stacked from draft
 * resistance rewards. */
static unsigned int s_playerResistPct;

static Uint32* s_bloodStamp = NULL;
static u8* s_bloodSeen = NULL;
static int s_bloodCap = 0;

static int horde_is_live(int i)
{
    return s_live[i] && s_slotWave[i] == s_wave;
}

/* Private xorshift stream: the game's Rng_* feeds enemy behaviour and demo
 * playback, and drawing from it would desync those. */
static u32 s_rng;

static u32 hrnd(void)
{
    s_rng ^= s_rng << 13;
    s_rng ^= s_rng >> 17;
    s_rng ^= s_rng << 5;
    return s_rng;
}

static int hrnd_below(int n) { return (n <= 0) ? 0 : (int)(hrnd() % (u32)n); }

/* Wave pool. stateStep is the AI entry state: Groaner_Init only stands the
 * dog up when it is 3, AirScreamer needs 12, PuppetNurse 17 (same values the
 * maps' own spawn tables use; see RANDO_MONSTERS in pc_rando.c). */
typedef struct { u8 charaId; u8 stateStep; } s_HordeMonster;

static const s_HordeMonster HORDE_MONSTERS[] = {
    { Chara_GreyChild,   3  },
    { Chara_PuppetNurse, 17 },
    { Chara_Romper,      3  },
    { Chara_Groaner,     3  },
};
#define N_HORDE_MONSTERS ((int)(sizeof(HORDE_MONSTERS) / sizeof(HORDE_MONSTERS[0])))

/* Loot: 5% nothing, 15% heal, 80% ammo for an owned gun. Quantities roll
 * per drop. */
#define HORDE_LOOT_NOTHING_BELOW 5
#define HORDE_LOOT_HEAL_BELOW    20

typedef struct { u8 gun; u8 ammo; u8 lo; u8 hi; } s_HordeAmmo;

static const s_HordeAmmo HORDE_AMMO[] = {
    { InvItemId_Handgun,      InvItemId_HandgunBullets, 5, 10 },
    { InvItemId_Shotgun,      InvItemId_ShotgunShells,  2, 4  },
    { InvItemId_HuntingRifle, InvItemId_RifleShells,    2, 4  },
};

/* Inter-wave draft offers. Damage pool is fixed; weapons draw from the
 * unowned (HyperBlaster excluded everywhere: infinite ammo). */
static const unsigned int HORDE_DMG[] = { 150, 250, 275, 400 };
static const u8 HORDE_GUNS[]      = { InvItemId_Handgun, InvItemId_HuntingRifle, InvItemId_Shotgun };
static const u8 HORDE_GUN_AMMO[]  = { InvItemId_HandgunBullets, InvItemId_RifleShells, InvItemId_ShotgunShells };
static const u8 HORDE_GUN_COUNT[] = { 15, 6, 6 };
static const u8 HORDE_MELEE[]     = { InvItemId_KitchenKnife, InvItemId_SteelPipe, InvItemId_RockDrill,
                                      InvItemId_Hammer, InvItemId_Chainsaw, InvItemId_Katana, InvItemId_Axe };

/* Damage is no longer a fixed slot: it only enters a draft's candidate pool
 * on this roll. Lower once the three basic guns are owned while melee
 * weapons are still missing. */
#define HORDE_DRAFT_DMG_PCT      50
#define HORDE_DRAFT_DMG_PCT_GUNS 45

/* Resistance: incoming-damage multiplier offers (pct), stacked like damage.
 * x0.85 / x0.80 / x0.75 read as +18% / +25% / +33% max health. The floor
 * keeps max threat (x10) still hitting at x2. Rolls on the same odds as
 * damage, independently. */
static const unsigned int HORDE_RESIST[] = { 85, 80, 75 };
#define HORDE_RESIST_MIN 20u

/* Heal offers: weighted item pick, quantity rolled per offer. */
typedef struct { u8 item; u8 weight; u8 lo; u8 hi; } s_HordeHeal;

static const s_HordeHeal HORDE_HEALS[] = {
    { InvItemId_HealthDrink, 50, 2, 3 },
    { InvItemId_FirstAidKit, 35, 1, 2 },
    { InvItemId_Ampoule,     15, 1, 1 },
};
#define N_HORDE_HEALS ((int)(sizeof(HORDE_HEALS) / sizeof(HORDE_HEALS[0])))

/* Ammo offers grant this many magazines of the chosen gun's rounds. */
#define HORDE_DRAFT_AMMO_MAGS 2

extern u8 g_Items_GunsMaxLoadAmmo[36];

/* pct is the damage multiplier for Dmg offers and the item count for Heal
 * and Ammo offers; unused for weapons. */
typedef struct { int kind; u8 item; unsigned int pct; } s_HordeOffer;
static s_HordeOffer s_offers[3];

static int horde_owns_item(u8 id)
{
    int i;

    for (i = 0; i < INV_ITEM_COUNT_MAX; i++)
        if (g_SavegamePtr->items[i].id_0 == id)
            return 1;
    return 0;
}

static unsigned int horde_pick_dmg(const unsigned int* used, int nUsed)
{
    unsigned int avail[4];
    int n = 0, i, j;

    for (i = 0; i < 4; i++)
    {
        int dup = 0;
        for (j = 0; j < nUsed; j++)
            if (used[j] == HORDE_DMG[i]) { dup = 1; break; }
        if (!dup)
            avail[n++] = HORDE_DMG[i];
    }
    if (n <= 0)
        return HORDE_DMG[hrnd_below(4)];
    return avail[hrnd_below(n)];
}

/* Heal offer, never repeating `exclude` (the other heal in the same draft). */
static s_HordeOffer horde_make_heal(u8 exclude)
{
    s_HordeOffer o;
    int total = 0, roll, i;

    for (i = 0; i < N_HORDE_HEALS; i++)
        if (HORDE_HEALS[i].item != exclude)
            total += HORDE_HEALS[i].weight;
    roll = hrnd_below(total);
    for (i = 0; i < N_HORDE_HEALS; i++)
    {
        if (HORDE_HEALS[i].item == exclude)
            continue;
        if (roll < HORDE_HEALS[i].weight)
            break;
        roll -= HORDE_HEALS[i].weight;
    }
    if (i >= N_HORDE_HEALS)
        i = 0;
    o.kind = HordeOffer_Heal;
    o.item = HORDE_HEALS[i].item;
    o.pct  = (unsigned int)(HORDE_HEALS[i].lo + hrnd_below(HORDE_HEALS[i].hi - HORDE_HEALS[i].lo + 1));
    return o;
}

/* Rounds a gun has right now: loaded + reserve. The equipped gun's live
 * counts sit in playerCombat and only reach the inventory when it opens, so
 * read those for it; every other gun reads its inventory slots. */
static int horde_gun_rounds(u8 gun, u8 ammo)
{
    int i, loaded = 0, reserve = 0;
    s8 eq = g_SysWork.playerCombat.weaponInventoryIdx;

    if (eq >= 0 && eq < INV_ITEM_COUNT_MAX && g_SavegamePtr->items[eq].id_0 == gun)
        return g_SysWork.playerCombat.currentWeaponAmmo + g_SysWork.playerCombat.totalWeaponAmmo;

    for (i = 0; i < INV_ITEM_COUNT_MAX; i++)
    {
        if (g_SavegamePtr->items[i].id_0 == gun)
            loaded = g_SavegamePtr->items[i].count_1;
        else if (g_SavegamePtr->items[i].id_0 == ammo)
            reserve += g_SavegamePtr->items[i].count_1;
    }
    return loaded + reserve;
}

/* Ammo offer for an OWNED gun, the one with the least ammo measured in
 * magazines (15 handgun rounds weigh the same as 6 shells). Ties pick at
 * random. Returns 0 when Harry owns no gun. */
static int horde_make_ammo(s_HordeOffer* out)
{
    int i, best = -1, bestRounds = 0, bestMag = 1, ties = 0;

    for (i = 0; i < 3; i++)
    {
        int rounds, mag, cmp;

        if (!horde_owns_item(HORDE_GUNS[i]))
            continue;
        rounds = horde_gun_rounds(HORDE_GUNS[i], HORDE_GUN_AMMO[i]);
        mag    = g_Items_GunsMaxLoadAmmo[HORDE_GUNS[i] - 0x80];
        if (mag <= 0)
            mag = 1;
        /* rounds/mag < bestRounds/bestMag, without division. */
        cmp = (best < 0) ? -1 : rounds * bestMag - bestRounds * mag;
        if (cmp < 0)
        {
            best = i; bestRounds = rounds; bestMag = mag; ties = 1;
        }
        else if (cmp == 0 && hrnd_below(++ties) == 0)
        {
            best = i; bestRounds = rounds; bestMag = mag;
        }
    }
    if (best < 0)
        return 0;
    out->kind = HordeOffer_Ammo;
    out->item = HORDE_GUN_AMMO[best];
    out->pct  = (unsigned int)(bestMag * HORDE_DRAFT_AMMO_MAGS);
    return 1;
}

/* Candidate pool, then 3 drawn at random: ammo (owned gun, lowest first),
 * a heal, an unowned gun, an unowned melee weapon, and damage / resistance
 * only on their own rolls. A short pool tops up with a second, different
 * heal, then damage. */
static void horde_build_offers(void)
{
    s_HordeOffer pool[7];
    u8 guns[3], mel[7];
    int ng = 0, nm = 0, n = 0, heal, i, upPct;
    unsigned int used[1] = { 0 };

    for (i = 0; i < 3; i++)
        if (!horde_owns_item(HORDE_GUNS[i]))
            guns[ng++] = HORDE_GUNS[i];
    for (i = 0; i < 7; i++)
        if (!horde_owns_item(HORDE_MELEE[i]))
            mel[nm++] = HORDE_MELEE[i];

    if (horde_make_ammo(&pool[n]))
        n++;
    heal = n;
    pool[n++] = horde_make_heal(0);
    if (ng > 0)
    {
        pool[n].kind = HordeOffer_Gun;
        pool[n].item = guns[hrnd_below(ng)];
        pool[n].pct  = 0;
        n++;
    }
    if (nm > 0)
    {
        pool[n].kind = HordeOffer_Melee;
        pool[n].item = mel[hrnd_below(nm)];
        pool[n].pct  = 0;
        n++;
    }
    upPct = (ng == 0 && nm > 0) ? HORDE_DRAFT_DMG_PCT_GUNS : HORDE_DRAFT_DMG_PCT;
    if (hrnd_below(100) < upPct)
    {
        pool[n].kind = HordeOffer_Dmg;
        pool[n].item = 0;
        pool[n].pct  = horde_pick_dmg(used, 0);
        n++;
    }
    if (s_playerResistPct > HORDE_RESIST_MIN && hrnd_below(100) < upPct)
    {
        pool[n].kind = HordeOffer_Resist;
        pool[n].item = 0;
        pool[n].pct  = HORDE_RESIST[hrnd_below((int)(sizeof(HORDE_RESIST) / sizeof(HORDE_RESIST[0])))];
        n++;
    }
    if (n < 3)
        pool[n++] = horde_make_heal(pool[heal].item);
    if (n < 3)
    {
        pool[n].kind = HordeOffer_Dmg;
        pool[n].item = 0;
        pool[n].pct  = horde_pick_dmg(used, 0);
        n++;
    }

    /* Partial Fisher-Yates: the first 3 become the offers. */
    for (i = 0; i < 3; i++)
    {
        int j = i + hrnd_below(n - i);
        s_HordeOffer t = pool[i];
        pool[i] = pool[j];
        pool[j] = t;
        s_offers[i] = pool[i];
    }
    SH_LOG("[HORDE] draft: [%d] [%d] [%d]", s_offers[0].kind, s_offers[1].kind, s_offers[2].kind);
}

static void horde_grant_gun(u8 gun)
{
    int i;

    Inventory_AddSpecialItem(gun, 1);
    Pc_ItemToast_Show(gun, 1);
    for (i = 0; i < 3; i++)
    {
        if (HORDE_GUNS[i] == gun)
        {
            Inventory_AddSpecialItem(HORDE_GUN_AMMO[i], HORDE_GUN_COUNT[i]);
            Pc_ItemToast_Show(HORDE_GUN_AMMO[i], HORDE_GUN_COUNT[i]);
            break;
        }
    }
}

int Pc_Horde_DraftActive(void)
{
    return s_running && s_testPhase == HS_DRAFT;
}

int Pc_Horde_DraftOffer(int i, int* kind, int* item, unsigned int* pct)
{
    if (!Pc_Horde_DraftActive() || i < 0 || i >= 3)
        return 0;
    if (kind) *kind = s_offers[i].kind;
    if (item) *item = s_offers[i].item;
    if (pct)  *pct  = s_offers[i].pct;
    return 1;
}

unsigned int Pc_Horde_PlayerDmgPct(void) { return s_running ? s_playerDmgPct : 100; }
unsigned int Pc_Horde_PlayerResistPct(void) { return s_running ? s_playerResistPct : 100; }

void Pc_Horde_DraftPick(int i)
{
    if (!Pc_Horde_DraftActive() || i < 0 || i >= 3)
        return;
    if (s_offers[i].kind == HordeOffer_Dmg)
    {
        s_playerDmgPct = s_playerDmgPct * s_offers[i].pct / 100u;
        if (s_playerDmgPct > HORDE_DMG_MAX)
            s_playerDmgPct = HORDE_DMG_MAX;
        SH_LOG("[HORDE] draft: damage x%u.%02u", s_playerDmgPct / 100u, s_playerDmgPct % 100u);
    }
    else if (s_offers[i].kind == HordeOffer_Resist)
    {
        s_playerResistPct = s_playerResistPct * s_offers[i].pct / 100u;
        if (s_playerResistPct < HORDE_RESIST_MIN)
            s_playerResistPct = HORDE_RESIST_MIN;
        SH_LOG("[HORDE] draft: damage taken x%u.%02u", s_playerResistPct / 100u, s_playerResistPct % 100u);
    }
    else if (s_offers[i].kind == HordeOffer_Gun)
    {
        horde_grant_gun(s_offers[i].item);
    }
    else if (s_offers[i].kind == HordeOffer_Heal || s_offers[i].kind == HordeOffer_Ammo)
    {
        Inventory_AddSpecialItem(s_offers[i].item, (u8)s_offers[i].pct);
        Pc_ItemToast_Show(s_offers[i].item, (u8)s_offers[i].pct);
    }
    else
    {
        Inventory_AddSpecialItem(s_offers[i].item, 1);
        Pc_ItemToast_Show(s_offers[i].item, 1);
    }
    Sd_PlaySfx(Sfx_MenuConfirm, 0, 64);
    s_testPhase    = HS_COUNTDOWN;
    s_testDeadline = SDL_GetTicks() + HORDE_COUNTDOWN_MS;
}

static void horde_grant_loot(void)
{
    int roll = hrnd_below(100);

    if (roll < HORDE_LOOT_NOTHING_BELOW)
        return;

    if (roll < HORDE_LOOT_HEAL_BELOW)
    {
        if (hrnd_below(100) < 75)
        {
            Inventory_AddSpecialItem(InvItemId_HealthDrink, 1);
            Pc_ItemToast_Show(InvItemId_HealthDrink, 1);
        }
        else
        {
            Inventory_AddSpecialItem(InvItemId_FirstAidKit, 1);
            Pc_ItemToast_Show(InvItemId_FirstAidKit, 1);
        }
        return;
    }

    {
        const s_HordeAmmo* owned[3];
        int n = 0, i;

        for (i = 0; i < (int)(sizeof(HORDE_AMMO) / sizeof(HORDE_AMMO[0])); i++)
            if (horde_owns_item(HORDE_AMMO[i].gun))
                owned[n++] = &HORDE_AMMO[i];
        /* Harry always ships with the handgun, so this only misses on a
         * stripped savegame; fall back to its bullets. */
        if (n <= 0)
        {
            owned[n++] = &HORDE_AMMO[0];
        }
        {
            const s_HordeAmmo* pick = owned[hrnd_below(n)];
            u8 count = (u8)(pick->lo + hrnd_below(pick->hi - pick->lo + 1));

            Inventory_AddSpecialItem(pick->ammo, count);
            Pc_ItemToast_Show(pick->ammo, count);
        }
        return;
    }
}

/* Blood janitor: the native floor-splat pool (unkTable1_4C, 100 entries on
 * MAP2_S00 shared with every map FX) only frees on room transitions, which
 * the arena never does. Mature splats (field_A==2) accumulate until the
 * allocator returns NO_VALUE and ALL blood stops. Two rules, horde-only:
 * far mature splats go (the draw path culls beyond 20m anyway, so losing
 * them past 25m is invisible), and total blood entries never exceed budget
 * (oldest first, PC-side timestamps — the pool has no age field).
 * Vanilla never fills the pool without transitions. */
#define HORDE_BLOOD_KEEP_DIST Q12(25.0f)
#define HORDE_BLOOD_KEEP 40

static int horde_blood_is(int a) { return a == 1 || a == 2; }

static void horde_blood_free(int idx)
{
    int j;

    g_MapOverlayHdr.unkTable1_4C[idx].field_A = 0;
    for (j = 0; j < g_MapOverlayHdr.bloodSplatCount; j++)
        if (g_MapOverlayHdr.bloodSplats[j].field_0 == idx)
            g_MapOverlayHdr.bloodSplats[j].field_0 = NO_VALUE;
}

static void horde_blood_sweep(int keep)
{
    int i, blood = 0;
    s32 count = g_MapOverlayHdr.unkTable1Count_50;
    Uint32 now = SDL_GetTicks();

    if (count <= 0)
        return;
    if (s_bloodCap != (int)count)
    {
        free(s_bloodStamp);
        free(s_bloodSeen);
        s_bloodStamp = (Uint32*)calloc((size_t)count, sizeof(Uint32));
        s_bloodSeen  = (u8*)calloc((size_t)count, sizeof(u8));
        s_bloodCap   = (s_bloodStamp != NULL && s_bloodSeen != NULL) ? (int)count : 0;
        if (s_bloodCap == 0)
            return;
    }

    for (i = 0; i < count; i++)
    {
        int a = g_MapOverlayHdr.unkTable1_4C[i].field_A;
        s32 d;

        if (!horde_blood_is(a))
        {
            s_bloodSeen[i] = 0;
            continue;
        }
        if (!s_bloodSeen[i])
        {
            s_bloodSeen[i]  = 1;
            s_bloodStamp[i] = now;
        }
        blood++;
        if (a != 2)
            continue;
        d = ABS(g_MapOverlayHdr.unkTable1_4C[i].field_0.vx_0 - g_SysWork.playerWork.player.position.vx) +
            ABS(g_MapOverlayHdr.unkTable1_4C[i].field_4.vz_4 - g_SysWork.playerWork.player.position.vz);
        if (d > HORDE_BLOOD_KEEP_DIST)
        {
            horde_blood_free(i);
            s_bloodSeen[i] = 0;
            blood--;
        }
    }

    while (blood > keep)
    {
        int oldest = -1;
        Uint32 oldestT = 0;

        for (i = 0; i < count; i++)
        {
            if (!s_bloodSeen[i] || !horde_blood_is(g_MapOverlayHdr.unkTable1_4C[i].field_A))
                continue;
            if (oldest < 0 || (s32)(s_bloodStamp[i] - oldestT) < 0)
            {
                oldest  = i;
                oldestT = s_bloodStamp[i];
            }
        }
        if (oldest < 0)
            break;
        horde_blood_free(oldest);
        s_bloodSeen[oldest] = 0;
        blood--;
    }
}

static void horde_blood_janitor(void)
{
    horde_blood_sweep(HORDE_BLOOD_KEEP);
}

/* Once per corpse (same latch as the randomizer hook at the call site):
 * real kills only feed the counter; the wave roster is resolved by scan. */
void Pc_Horde_OnEnemyKilled(void)
{
    if (!s_running)
        return;
    s_kills++;
    horde_blood_janitor();
    horde_grant_loot();
}

unsigned int Pc_Horde_Kills(void) { return s_running ? s_kills : 0; }
unsigned int Pc_Horde_Wave(void)  { return s_running ? s_wave : 0; }
unsigned int Pc_Horde_ThreatPct(void) { return s_running ? s_threatPct : 100; }

/* Seconds left on the inter-wave countdown, or -1 outside it. */
int Pc_Horde_CountdownSecs(void)
{
    s32 rem;

    if (!s_running || s_testPhase != HS_COUNTDOWN || !s_waveSpawned)
        return -1;
    rem = (s32)(s_testDeadline - SDL_GetTicks());
    if (rem <= 0)
        return 0;
    return (rem + 999) / 1000;
}

int Pc_Horde_Alive(void)
{
    int i, n = 0;

    if (!s_running)
        return 0;
    for (i = 0; i < NPC_COUNT_MAX; i++)
    {
        s_SubCharacter* npc;

        if (!horde_is_live(i))
            continue;
        npc = &g_SysWork.npcs[i];
        if (s_seen[i] && (npc->model.charaId == Chara_None || npc->health <= Q12(0.0f)))
            continue;
        n++;
    }
    return n;
}

/* Live roster world positions for the minimap foe dots. */
int Pc_Horde_LiveEnemies(q19_12* outX, q19_12* outZ, int cap)
{
    int i, n = 0;

    if (!s_running || outX == NULL || outZ == NULL || cap <= 0)
        return 0;
    for (i = 0; i < NPC_COUNT_MAX && n < cap; i++)
    {
        s_SubCharacter* npc;

        if (!horde_is_live(i))
            continue;
        npc = &g_SysWork.npcs[i];
        if (npc->model.charaId == Chara_None || npc->health <= Q12(0.0f))
            continue;
        outX[n] = npc->position.vx;
        outZ[n] = npc->position.vz;
        n++;
    }
    return n;
}

/* Proximity ring, behind Harry's heading: excluded frontal cone, spawns land
 * behind and on the flanks so they run at him from off-screen. Inside enemy
 * notice range (Groaner wakes at 4-8m with line of sight). */
#define HORDE_RING_MIN   Q12(8.0f)
#define HORDE_RING_WIDTH Q12(6.0f)
#define HORDE_PICK_TRIES 12
#define HORDE_LOS_SLACK  Q12(0.3f)

/* Level band: a spawn must sit on Harry's walkable level, never on roofs,
 * overpasses or basements. */
#define HORDE_LEVEL_BAND Q12(1.5f)
/* Openness probe: rays this long in the four cardinal directions from the
 * candidate, at torso height. A street or alley leaves at least two open; a
 * room interior does not. */
#define HORDE_OPEN_LEN   Q12(3.0f)
#define HORDE_OPEN_MIN   2

/* Leash: a wave enemy that stays this far this long gets relocated onto the
 * ring, facing Harry. Rescues wanderers and walled-pocket spawns alike. */
#define HORDE_LEASH_DIST Q12(20.0f)
#define HORDE_LEASH_MS   4000u

extern unsigned char g_PcNpcDebugSpawned[];
extern s32 func_800808AC(q19_12 posX, q19_12 posZ);

int Pc_Horde_Active(void) { return s_running; }

void Pc_Horde_Request(void) { s_requested = 1; }
int  Pc_Horde_Requested(void) { return s_requested; }

void Pc_Horde_OnNewGame(void)
{
    if (!s_requested)
        return;
    s_requested = 0;

    g_SavegamePtr->mapIdx          = HORDE_ARENA_MAP;
    g_SavegamePtr->playerPositionX = HORDE_ARRIVE_X;
    g_SavegamePtr->playerPositionZ = HORDE_ARRIVE_Z;
    g_SavegamePtr->playerHealth    = Q12(100.0f);

    /* Skip the cafe-exit cutscene (auto-firing TriggerType_None row gated by
     * EventFlag_146): pre-setting its disabled flag leaves the same end
     * state with none of the dialogue, map animation, or the NoEnemySpawn
     * clear at its tail. */
    Savegame_EventFlagSet(EventFlag_146);
    /* Every paper map: the map screen tests the current area's bit, and the
     * run never leaves the arena, so hand them all over at once. */
    g_SavegamePtr->paperMapFlags = (s32)~0u;
    Savegame_EventFlagSet(EventFlag_M2S00_PickupMap);

    s_rng = (u32)SDL_GetTicks() | 0x9E3779B9u;

    /* The pool (assets + chara_global AI backfill) is config-gated; a horde
     * run needs every monster spawnable in the arena. Forced RAM-only, never
     * saved, restored on EndRun. */
    if (!g_PcConfig.globalCharaPool)
    {
        g_PcConfig.globalCharaPool = 1;
        s_poolForced = 1;
    }
    Pc_CharaGlobal_Open();

    s_waveSpawned = 0;
    s_kills = 0;
    s_wave = 0;
    s_threatPct = 100;
    s_playerDmgPct = 100;
    s_playerResistPct = 100;
    s_running = 1;
    SH_LOG("[HORDE] run started in MAP2_S00");
}

void Pc_Horde_EndRun(void)
{
    if (!s_running && !s_requested)
        return;
    s_running     = 0;
    s_requested   = 0;
    s_waveSpawned = 0;
    s_kills = 0;
    s_wave = 0;
    s_threatPct = 100;
    s_playerDmgPct = 100;
    s_playerResistPct = 100;
    /* Draft safety: never leave Harry frozen outside a run. */
    g_Player_DisableControl = 0;
    free(s_bloodStamp);
    free(s_bloodSeen);
    s_bloodStamp = NULL;
    s_bloodSeen  = NULL;
    s_bloodCap   = 0;
    if (s_poolForced)
    {
        s_poolForced = 0;
        g_PcConfig.globalCharaPool = 0;
    }
    SH_LOG("[HORDE] run ended");
}

int Pc_Horde_VetoTransition(void)
{
    static Uint32 s_lastBuzz = 0;
    Uint32 now;

    if (!s_running)
        return 0;
    now = SDL_GetTicks();
    if (now - s_lastBuzz >= HORDE_BUZZ_MS)
    {
        s_lastBuzz = now;
        SD_Call(Sfx_MenuError);
    }
    return 1;
}

/* Pick a spawn point on the rear-biased ring: Harry's walkable level (never
 * roofs or basements), valid floor (never the void), open line from Harry
 * (no wall between, so they can run straight at him), and open ground
 * around it (streets and alleys pass, room interiors do not).
 * Falls back to any level-valid floor point when every try is walled off. */
static int horde_ray_clear(const VECTOR3* from, const VECTOR3* to, s32 slack)
{
    s_RayTrace occ;
    s32 dist = Math_Vector2MagCalc(to->vx - from->vx, to->vz - from->vz);

    if (!Ray_TraceQuery(&occ, from, to))
        return 1;
    if (occ.character != NULL)
        return 1;
    return occ.hitDistance >= dist - slack;
}

static int horde_pick_point(q19_12* outX, q19_12* outZ, q19_12* outY)
{
    s_SubCharacter* hr = &g_SysWork.playerWork.player;
    int head = (int)hr->rotation.vy;
    q19_12 fx = Q12(0.0f), fz = Q12(0.0f), fy = Q12(0.0f);
    int haveFallback = 0;
    int i;

    for (i = 0; i < HORDE_PICK_TRIES; i++)
    {
        /* Rear 270 degrees: 180 behind, +/-135 toward the flanks. The
         * frontal cone stays empty so nothing pops into view. */
        int ang = (head + 2048 + (hrnd_below(3072) - 1536)) & 0xFFF;
        q19_12 dist = HORDE_RING_MIN + (q19_12)((s64)HORDE_RING_WIDTH * hrnd_below(1000) / 1000);
        q19_12 x = hr->position.vx + Q12_MULT(dist, Math_Sin((q19_12)ang));
        q19_12 z = hr->position.vz + Q12_MULT(dist, Math_Cos((q19_12)ang));
        s_CollisionSurface surf;
        VECTOR3 from, to;
        int open;

        if (func_800808AC(x, z) == GroundType_None)
            continue;
        Collision_SurfaceGet(&surf, x, z);

        /* Same walkable level as Harry: rejects roofs, overpasses,
         * basements and indoor floors above/below the street. */
        if (ABS(surf.groundHeight - hr->position.vy) > HORDE_LEVEL_BAND)
            continue;

        from.vx = hr->position.vx;
        from.vy = hr->position.vy - Q12(1.0f);
        from.vz = hr->position.vz;
        to.vx = x;
        to.vy = surf.groundHeight - Q12(1.0f);
        to.vz = z;
        if (!horde_ray_clear(&from, &to, HORDE_LOS_SLACK))
        {
            if (!haveFallback)
            {
                fx = x; fz = z; fy = surf.groundHeight;
                haveFallback = 1;
            }
            continue;
        }

        /* Openness: probe the four cardinals from the candidate. A same-
         * level shop floor passes the band but fails here. */
        open = 0;
        {
            static const s32 ox[4] = { 1, -1, 0, 0 };
            static const s32 oz[4] = { 0, 0, 1, -1 };
            int d;
            VECTOR3 p0, p1;

            p0.vx = x;
            p0.vy = surf.groundHeight - Q12(1.0f);
            p0.vz = z;
            for (d = 0; d < 4; d++)
            {
                p1.vx = x + ox[d] * HORDE_OPEN_LEN;
                p1.vy = p0.vy;
                p1.vz = z + oz[d] * HORDE_OPEN_LEN;
                if (horde_ray_clear(&p0, &p1, 0))
                    open++;
            }
        }
        if (open < HORDE_OPEN_MIN)
            continue;

        *outX = x; *outZ = z; *outY = surf.groundHeight;
        return 1;
    }

    if (haveFallback)
    {
        *outX = fx; *outZ = fz; *outY = fy;
        return 1;
    }
    return 0;
}

static int horde_spawn_one(int charaId, int stateStep)
{
    q19_12 x, z, y;
    int slot, i;
    s_SubCharacter* npc;

    if (g_MapOverlayHdr.charaUpdateFuncs[charaId] == NULL)
        return -1;

    for (i = 0; i < NPC_COUNT_MAX; i++)
        if (g_SysWork.npcs[i].model.charaId == Chara_None)
            break;
    if (i >= NPC_COUNT_MAX)
        return -1;
    slot = i;

    if (!horde_pick_point(&x, &z, &y))
        return -1;

    npc = &g_SysWork.npcs[slot];
    memset(npc, 0, sizeof(s_SubCharacter));
    npc->model.charaId      = (s8)charaId;
    npc->model.controlState = 0;
    npc->model.stateStep    = (u8)stateStep;
    npc->field_40           = (s8)slot;
    npc->position.vx        = x;
    npc->position.vz        = z;
    npc->position.vy        = y;
    /* Face Harry: several AIs only notice what is in front of them. */
    npc->rotation.vy        = (s16)Math_AngleBetweenPositionsGet(npc->position,
                                g_SysWork.playerWork.player.position);
    npc->model.anim.flags  |= AnimFlag_Visible;
    SET_FLAG(&g_SysWork.npcFlags, slot);
    SET_FLAG(g_SysWork.field_228C, slot);
    /* Debug-spawned: Savegame_EnemyStateUpdate skips the kill-record write,
     * so wave kills never touch the vanilla permadeath bits. */
    g_PcNpcDebugSpawned[slot] = 1;
    if (g_SysWork.npcFlagsId < (u8)(slot + 1))
        g_SysWork.npcFlagsId = (u8)(slot + 1);
    return slot;
}

/* Spawn-test loop: countdown, random group of 5/7/9, next group once all
 * are dead. */

static void horde_test_clear_corpses(void)
{
    int i;

    for (i = 0; i < NPC_COUNT_MAX; i++)
    {
        if (horde_is_live(i) && g_SysWork.npcs[i].model.charaId != Chara_None &&
            g_SysWork.npcs[i].health <= Q12(0.0f))
        {
            g_SysWork.npcs[i].model.charaId = Chara_None;
            g_SysWork.npcs[i].flags         = 0;
            CLEAR_FLAG(&g_SysWork.npcFlags, i);
            CLEAR_FLAG(g_SysWork.field_228C, i);
            g_PcNpcDebugSpawned[i] = 0;
        }
        s_live[i] = 0;
        s_seen[i] = 0;
    }
}

void Pc_Horde_OnMapLoad(s32 mapIdx)
{
    if (!s_running || mapIdx != HORDE_ARENA_MAP)
        return;

    /* The boot clears this on map entry; the arena runs scripted waves only.
     * Plain flag write, safe this early. Spawning itself waits for the first
     * gameplay frame (Update): collision/IPD chunks only finish loading in
     * GameStartup steps 7-8, and raycasts before that crash. */
    g_SysWork.sysFlags |= SysFlag_NoEnemySpawn;
    s_waveSpawned = 0;
}

/* MainLoop, next to Pc_Rando_Update. Fires the starter group once the world
 * is fully live: gameplay state, arena map, collision ready. */
void Pc_Horde_Update(void)
{
    int i;

    if (!s_running)
        return;
    if (g_GameWork.gameState != GameState_InGame || g_SysWork.sysState != SysState_Gameplay)
        return;
    if (g_SavegamePtr->mapIdx != HORDE_ARENA_MAP)
        return;

    g_SysWork.sysFlags |= SysFlag_NoEnemySpawn;

    if (!s_waveSpawned)
    {
        s_waveSpawned  = 1;
        s_testPhase    = HS_COUNTDOWN;
        s_testDeadline = SDL_GetTicks() + HORDE_COUNTDOWN_MS;
        SH_LOG("[HORDE] test: first group in 3s");
        return;
    }

    if (s_testPhase == HS_COUNTDOWN)
    {
        int spawned = 0;

        if ((s32)(SDL_GetTicks() - s_testDeadline) < 0)
            return;

        horde_test_clear_corpses();
        s_wave++;
        if (s_wave >= 2)
        {
            s_threatPct = s_threatPct * 125u / 100u;
            if (s_threatPct > HORDE_THREAT_MAX)
                s_threatPct = HORDE_THREAT_MAX;
        }
        {
            int group = 5 + 2 * hrnd_below(3);
            for (i = 0; i < group; i++)
            {
                const s_HordeMonster* m = &HORDE_MONSTERS[hrnd_below(N_HORDE_MONSTERS)];
                int slot = horde_spawn_one(m->charaId, m->stateStep);
                if (slot >= 0)
                {
                    s_live[slot] = 1;
                    s_slotWave[slot] = s_wave;
                    s_leashSince[slot] = SDL_GetTicks();
                    spawned++;
                }
            }
        }
        s_waveTotal = spawned;
        if (spawned <= 0)
        {
            s_testDeadline = SDL_GetTicks() + HORDE_COUNTDOWN_MS;
            SH_LOG("[HORDE] test: no spawn points, retry in 3s");
            return;
        }
        s_testPhase = HS_FIGHT;
        SH_LOG("[HORDE] test: %d spawned, kill them all", spawned);
        return;
    }

    if (s_testPhase == HS_DRAFT)
    {
        /* The wave waits on the player: the draft overlay reads the offers
         * and Pc_Horde_DraftPick resumes into HS_COUNTDOWN. */
        return;
    }

    if (s_testPhase == HS_FIGHT)
    {
        int alive = 0;
        Uint32 now = SDL_GetTicks();

        for (i = 0; i < NPC_COUNT_MAX; i++)
        {
            s_SubCharacter* npc;
            s32 dist;

            if (!horde_is_live(i))
                continue;
            npc = &g_SysWork.npcs[i];
            /* Fresh spawns set health in their AI-init first frame; latch it
             * once seen alive, scaled by the wave threat. */
            if (npc->model.charaId != Chara_None && npc->health > Q12(0.0f) && !s_seen[i])
            {
                s_seen[i] = 1;
                if (s_threatPct != 100)
                    npc->health = (q19_12)(((s64)npc->health * (s64)s_threatPct) / 100);
            }
            /* Real deaths and engine-vacated slots both clear the roster;
             * only the corpse latch feeds kills. */
            if (s_seen[i] && (npc->model.charaId == Chara_None || npc->health <= Q12(0.0f)))
                continue;
            if (npc->model.charaId == Chara_None)
            {
                /* Emptied before ever living: free the roster space, the
                 * wave still waits on the rest. */
                s_live[i] = 0;
                continue;
            }
            alive++;

            /* Leash (horde only, no AI touched): wanderers get relocated
             * onto the ring, facing Harry, so the wave keeps its pressure. */
            dist = Math_Vector2MagCalc(npc->position.vx - g_SysWork.playerWork.player.position.vx,
                                       npc->position.vz - g_SysWork.playerWork.player.position.vz);
            if (dist < HORDE_LEASH_DIST)
            {
                s_leashSince[i] = now;
            }
            else if (now - s_leashSince[i] >= HORDE_LEASH_MS)
            {
                q19_12 x, z, y;

                if (horde_pick_point(&x, &z, &y))
                {
                    npc->position.vx = x;
                    npc->position.vz = z;
                    npc->position.vy = y;
                    npc->rotation.vy = (s16)Math_AngleBetweenPositionsGet(npc->position,
                                            g_SysWork.playerWork.player.position);
                    SH_LOG("[HORDE] test: leashed slot %d back onto the ring", i);
                }
                s_leashSince[i] = now;
            }
        }
        if (alive <= 0)
        {
            horde_build_offers();
            s_testPhase = HS_DRAFT;
            SH_LOG("[HORDE] test: wave %u cleared, draft picks next", s_wave);
            /* Guaranteed flow: every third wave compacts the blood pool
             * to half budget, visible splats included. */
            if (s_wave % 3 == 0)
                horde_blood_sweep(HORDE_BLOOD_KEEP / 2);
        }
    }
}
