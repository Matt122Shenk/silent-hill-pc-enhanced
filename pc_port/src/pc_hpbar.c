/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Red enemy HP bars — see pc_hpbar.h for the system overview.
 *
 * Render path mirrors the bullet decals (pc_decals.c): world-space corners
 * as Q8 offsets from a Vw_WorldScreenMatrixAtPositionGet matrix, RotTransPers
 * per corner, POLY_G4 prims from the GsOUT_PACKET_P arena, addPrim into the
 * world OT at the same bucket scale world geometry uses. The bar frame is a
 * cylindrical billboard (world-upright, facing the camera around Y only),
 * so bars stay screen-horizontal under steep fixed cameras. Backdrop first,
 * fill second — within a bucket draw order is list order, so the fill wins
 * without any lift. */
#include "game.h"

#include "bodyprog/chara/chara.h"
#include "bodyprog/collision/ray.h"
#include "bodyprog/math/math.h"
#include "bodyprog/screen/screen_data.h"
#include "bodyprog/gfx/world.h"
#include "bodyprog/view/vw_calc.h"

#include "pc_config.h"
#include "pc_hpbar.h"

extern MATRIX GsWSMATRIX;

#define HPBAR_HALF_W     Q12(0.35f) /* bar half-width (~35cm) */
#define HPBAR_HALF_H     Q12(0.035f) /* bar half-height (~3.5cm) */
#define HPBAR_RANGE      Q12(12.0f) /* draw bars within 12m */
#define HPBAR_TOP_MARGIN Q12(0.35f) /* height above the collision-box top (PSX -Y = up) */
#define HPBAR_LOS_SLACK  Q12(0.3f) /* LOS target may stop this short of the bar */
#define HPBAR_OT_SHIFT   1 /* same OT scale as the bullet decals */
#define HPBAR_OT_LIFT    1

static s32 s_hpMax[NPC_COUNT_MAX];

void Pc_HpBarsReset(void)
{
    int i;

    for (i = 0; i < NPC_COUNT_MAX; i++)
        s_hpMax[i] = 0;
}

/* Project one camera-facing quad (center + half-extents along the view axes)
 * to screen corners. Returns 0 when a corner leaves the valid window. */
static int Pc_HpBarProjectQuad(const VECTOR3* center,
                               s32 ux, s32 uy, s32 uz, s32 vx, s32 vy, s32 vz,
                               s16 sx[4], s16 sy[4], s32* bucketSum)
{
    MATRIX  mat;
    SVECTOR v;
    int     k;

    Vw_WorldScreenMatrixAtPositionGet(&mat, center->vx, center->vy, center->vz);
    SetRotMatrix(&mat);
    SetTransMatrix(&mat);

    for (k = 0; k < 4; k++)
    {
        s32  st = (k & 1) ? 1 : -1;
        s32  sb = (k & 2) ? 1 : -1;
        s32  otz;
        s32  sxy;
        long p;
        long flag;

        v.vx  = (s16)Q12_TO_Q8(st * ux + sb * vx);
        v.vy  = (s16)Q12_TO_Q8(st * uy + sb * vy);
        v.vz  = (s16)Q12_TO_Q8(st * uz + sb * vz);
        v.pad = 0;

        otz = RotTransPers(&v, (int*)&sxy, &p, &flag);
        if (otz <= 8)
            return 0;

        sx[k] = (s16)(sxy & 0xFFFF);
        sy[k] = (s16)((u32)sxy >> 16);
        if (ABS(sx[k]) > 2048 || ABS(sy[k]) > 2048)
            return 0;

        *bucketSum += otz;
    }

    return 1;
}

static void Pc_HpBarEmitQuad(POLY_G4** poly, GsOT* ot, s32 bucket,
                             const s16 sx[4], const s16 sy[4], u8 r, u8 g, u8 b)
{
    setPolyG4(*poly);
    setRGB0(*poly, r, g, b);
    setRGB1(*poly, r, g, b);
    setRGB2(*poly, r, g, b);
    setRGB3(*poly, r, g, b);
    setXY4(*poly, sx[0], sy[0], sx[1], sy[1], sx[2], sy[2], sx[3], sy[3]);
    addPrim(&ot->org[bucket], *poly);
    *poly += 1;
}

void Pc_HpBarsDraw(GsOT* ot)
{
    POLY_G4*       poly;
    s_SubCharacter* player;
    int            i;

    if (!g_PcConfig.enemyHpBars)
        return;

    poly   = (POLY_G4*)GsOUT_PACKET_P;
    player = &g_SysWork.playerWork.player;

    for (i = 0; i < NPC_COUNT_MAX; i++)
    {
        s_SubCharacter* npc = &g_SysWork.npcs[i];
        s32 yA, yB, yLo, barY, dist, frac12;
        s32 ex, ez;
        s32 rx, ry, rz, ux, uy, uz;
        VECTOR3 center;
        s16 bgX[4], bgY[4], fillX[4], fillY[4];
        s32 bucketSum = 0;
        s32 bucket;

        if (npc->model.charaId == Chara_None)
        {
            s_hpMax[i] = 0;
            continue;
        }
        if (npc->model.charaId < Chara_AirScreamer || npc->model.charaId >= Chara_LockerDeadBody)
            continue;
        if (npc->collision.state == CharaCollisionState_Ignore)
            continue;
        if (npc->health <= Q12(0.0f))
            continue;

        if (s_hpMax[i] <= 0)
            s_hpMax[i] = npc->health;

        ex   = npc->position.vx - player->position.vx;
        ez   = npc->position.vz - player->position.vz;
        dist = Math_Vector2MagCalc(ex, ez);
        if (dist > HPBAR_RANGE)
            continue;

        yA = npc->position.vy + npc->collision.box.top;
        yB = npc->position.vy + npc->collision.box.height;
        yLo = (yA < yB) ? yA : yB;

        center.vx = npc->position.vx;
        center.vy = yLo - HPBAR_TOP_MARGIN;
        center.vz = npc->position.vz;

        /* Line of sight from Harry to the bar: world hits short of it hide
         * the bar; hitting a character (including the target) does not. */
        {
            s_RayTrace occ;
            VECTOR3    from;
            s32        distToBar;

            from.vx = player->position.vx;
            from.vy = player->position.vy - Q12(1.0f);
            from.vz = player->position.vz;
            distToBar = Math_Vector2MagCalc(center.vx - from.vx, center.vz - from.vz);
            if (Ray_TraceQuery(&occ, &from, &center) && occ.character == NULL &&
                occ.hitDistance < distToBar - HPBAR_LOS_SLACK)
                continue;
        }

        /* Cylindrical billboard: upright on the world Y axis, facing the camera
         * around Y only. A fully camera-facing quad tilts under the game's
         * steep fixed cameras, reading as rotation; this way the bar stays
         * screen-horizontal and only follows the entity's position. Right =
         * camera-right projected onto world XZ (normalized); up = world Y. */
        {
            s32 mag;

            rx = GsWSMATRIX.m[0][0];
            rz = GsWSMATRIX.m[2][0];
            mag = Math_Vector2MagCalc(rx, rz);
            if (mag > Q12(0.01f))
            {
                rx = (s32)(((s64)rx << 12) / mag);
                rz = (s32)(((s64)rz << 12) / mag);
            }
            else
            {
                /* Camera looking straight down: no horizontal heading. */
                rx = Q12(1.0f);
                rz = 0;
            }
            ry = 0;
            ux = 0;
            uy = Q12(1.0f);
            uz = 0;
        }

        if (!Pc_HpBarProjectQuad(&center,
                                 Q12_MULT(rx, HPBAR_HALF_W), Q12_MULT(ry, HPBAR_HALF_W), Q12_MULT(rz, HPBAR_HALF_W),
                                 Q12_MULT(ux, HPBAR_HALF_H), Q12_MULT(uy, HPBAR_HALF_H), Q12_MULT(uz, HPBAR_HALF_H),
                                 bgX, bgY, &bucketSum))
            continue;

        frac12 = (s32)(((s64)npc->health << 12) / s_hpMax[i]);
        if (frac12 < 0) frac12 = 0;
        if (frac12 > Q12(1.0f)) frac12 = Q12(1.0f);

        if (frac12 > 0)
        {
            VECTOR3 fillCenter;
            s32 hw = (s32)(((s64)HPBAR_HALF_W * frac12) >> 12);
            s32 dx = HPBAR_HALF_W - hw;

            /* Left-aligned fill: shrink toward the bar's left edge. */
            fillCenter.vx = center.vx - Q12_MULT(rx, dx);
            fillCenter.vy = center.vy - Q12_MULT(ry, dx);
            fillCenter.vz = center.vz - Q12_MULT(rz, dx);

            if (!Pc_HpBarProjectQuad(&fillCenter,
                                     Q12_MULT(rx, hw), Q12_MULT(ry, hw), Q12_MULT(rz, hw),
                                     Q12_MULT(ux, HPBAR_HALF_H), Q12_MULT(uy, HPBAR_HALF_H), Q12_MULT(uz, HPBAR_HALF_H),
                                     fillX, fillY, &bucketSum))
                continue;
        }

        bucket = (bucketSum >> 2) >> HPBAR_OT_SHIFT;
        bucket -= HPBAR_OT_LIFT;
        if (bucket < 1)
            bucket = 1;
        if (bucket >= ORDERING_TABLE_SIZE)
            bucket = ORDERING_TABLE_SIZE - 1;

        Pc_HpBarEmitQuad(&poly, ot, bucket, bgX, bgY, 56, 8, 8);
        if (frac12 > 0)
            Pc_HpBarEmitQuad(&poly, ot, bucket, fillX, fillY, 224, 24, 24);
    }

    GsOUT_PACKET_P = (PACKET*)poly;
}
