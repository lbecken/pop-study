/* pop_objs.c - the object list.  See pop_objs.h.
 * New code; ports of original routines, original labels in comments.
 */

#include "pop_objs.h"

#include "pop_auto.h"
#include "pop_control.h"
#include "pop_mover.h"
#include "pop_world.h"

#define NEG(v) (((v) & 0x80u) != 0)
#define u8(v) ((uint8_t)(v))

enum {
    FLOORHEIGHT = 15,                    /* GAMEEQ.S 657 */
    STARIMAGE = 0x41, STARTABLE = 0,     /* GAMEBG.S 139-140: in chtable1 */
    SWORDTABLE = 2,                      /* decodeswim: chtable3 */
    MIRLEVEL = 4, MIRSCRN = 4, MIRX = 4  /* TOPCTRL.S 87, SUBS.S 77-78 */
};

void pop_zeroobjs(pop_game *g)
{
    g->objX[0] = 0;
}

/* --- adding ------------------------------------------------------------------------------------- */

void pop_addfcharx(pop_game *g, uint8_t dx)
{
    uint8_t amount = dx, sign = dx;
    if (NEG(dx)) {
        amount = u8(0u - dx);
        sign = 0xFF;
    }
    if (NEG(sign ^ g->FCharFace)) g->FCharX = (uint16_t)(g->FCharX - amount);    /* :left */
    else g->FCharX = (uint16_t)(g->FCharX + amount);
}

void pop_setobjindx(pop_game *g, uint8_t slot)
{
    g->objINDX[slot] = g->FCharIndex;
    if (g->FCharIndex < 30) g->rb[POP_RB_OBJ + g->FCharIndex] = 1;
}

void pop_addcharobj(pop_game *g, uint8_t typ)
{
    uint8_t x = u8(g->objX[0] + 1), temp = 0;
    uint16_t pos = g->FCharX;
    if (x >= POP_MAXOBJ) return;               /* list full ("shouldn't happen") */
    g->objX[0] = x;
    g->objTYP[x] = typ;
    /* CVTX: 280-dot x to byte and dot offset; 252 dots are 36 bytes */
    if (NEG(pos >> 8)) {
        do {
            temp = u8(temp - 36);
            pos = (uint16_t)(pos + 252);
        } while ((pos >> 8) != 0);
    } else {
        while ((pos >> 8) != 0) {
            temp = u8(temp + 36);
            pos = (uint16_t)(pos - 252);
        }
    }
    g->objX[x] = u8(pos / 7 + temp);           /* ByteTable, OffsetTable */
    g->OFFSET = g->objOFF[x] = u8(pos % 7);
    g->objY[x] = g->FCharY;
    g->objCU[x] = g->FCharCU;
    g->objCL[x] = g->FCharCL;
    g->objCR[x] = g->FCharCR;
    g->objCD[x] = g->FCharCD;
    g->objIMG[x] = g->FCharImage;
    g->objTAB[x] = g->FCharTable;
    g->objFACE[x] = g->FCharFace;
    pop_setobjindx(g, x);
}

/* --- marks -------------------------------------------------------------------------------------- */

/* As MARKRED: with the carry set (INDEXBLOCK: not a block of this screen)
 * the mark goes to the top line if the index is a column, else nowhere. */
static void mark(pop_game *g, unsigned buffer, uint8_t a, int *carry)
{
    uint8_t y = g->ry;
    if (*carry) {
        if (y < 10) g->rb[POP_RB_TOP + y] = a;
        return;
    }
    *pop_rb(g, buffer, y) = a;
}

void pop_markfloor(pop_game *g, uint8_t a, int *carry) { mark(g, POP_RB_FLOOR, a, carry); }
void pop_markhalf(pop_game *g, uint8_t a, int *carry) { mark(g, POP_RB_HALF, a, carry); }

/* --- the character's surroundings ---------------------------------------------------------------- */

void pop_indexchar(pop_game *g)
{
    uint8_t posn = g->ch.posn;
    if (g->ch.action == 1) {                   /* on solid ground: by the image's edges */
        g->tempblocky = g->bottomblock;
        g->tempblockx = g->leftblock;
    } else {
        g->tempblocky = g->ch.block_y;
        g->tempblockx = g->ch.block_x;
    }
    /* Climbing up, he is drawn with the block to the left ... and so he is
     * in frames 2, 3, 4 and 6: the original compares the frame number
     * here where QUICKFLOOR compares the action ("falling"). */
    if ((posn >= 135 && posn < 149) || posn == 2 || posn == 3 || posn == 4 || posn == 6) {
        g->tempblockx--;
    }
    (void)pop_indexblock(g);
    g->FCharIndex = g->ry;
}

static void markul(pop_game *g, int half)      /* the lower and the upper block of a column */
{
    int carry;
    g->tempblocky = g->bottomblock;
    carry = pop_indexblock(g);
    if (half) pop_markhalf(g, 2, &carry); else pop_markfloor(g, 2, &carry);
    if (g->topblock == g->bottomblock) return;
    g->tempblocky = g->topblock;
    carry = pop_indexblock(g);
    if (half) pop_markhalf(g, 2, &carry); else pop_markfloor(g, 2, &carry);
}

void pop_quickfloor(pop_game *g)
{
    uint8_t posn = g->ch.posn, a;
    int half = 0;
    if (posn >= 135 && posn < 149) {
        half = 1;                              /* :climbup */
    } else {
        a = g->ch.action;
        if (a == 1) {
            if (posn < 78 || posn >= 80) return;
        } else if (a != 2 && a != 3 && a != 4 && a != 6) {
            return;
        }
    }
    /* from the rightmost block he covers to the leftmost */
    a = g->rightblock;
    for (;;) {
        g->tempblockx = a;
        markul(g, half);
        if (g->tempblockx == g->leftblock) return;
        a = u8(g->tempblockx - 1);
        if (NEG(a)) return;
    }
}

void pop_quickfg(pop_game *g)
{
    uint8_t x, y;
    int carry;
    if (g->ch.sword >= 2) {                    /* "quick fix to cover sword" */
        if (NEG(g->ch.face)) g->leftblock--;
        else g->rightblock++;
    }
    y = g->bottomblock;
    for (;;) {
        g->tempblocky = y;
        x = g->rightblock;
        for (;;) {
            g->tempblockx = x;
            carry = pop_indexblock(g);
            pop_markfred(g, 3, &carry);
            if (g->tempblockx == g->leftblock) break;
            x = u8(g->tempblockx - 1);
            if (NEG(x)) break;
        }
        if (g->tempblocky == g->topblock) return;
        y = u8(g->tempblocky - 1);
        if (NEG(y)) return;
    }
}

void pop_cropchar(pop_game *g)
{
    uint8_t a, x;
    if (g->ch.posn >= 224 && g->ch.posn < 229) {             /* climbing the stairs: behind the door */
        a = u8(g->doortop + 2);
        if (a < g->FCharY) {
            g->FCharCU = a;
            return;
        }
        /* :bug -- a call into the debugging module, which the final build
         * does not have */
        g->unmodelled = 1;
    }
    /* under a solid floor: cut at the ceiling */
    a = pop_rdblock(g, g->ch.scrn, g->leftblock, g->topblock);
    if (a != POP_BLOCK && pop_cmpspace(a) == 0) goto sides;
    /* jumping up to touch the ceiling is enough; else the block over his
     * right edge must be solid too */
    if (!(g->ch.action == 0 && (g->ch.posn == 79 || g->ch.posn == 81))) {
        a = pop_rdblock(g, g->ch.scrn, g->rightblock, g->topblock);
        if (a != POP_BLOCK && pop_cmpspace(a) == 0) goto sides;
    }
    x = u8(g->ch.block_y + 1);
    if (x != 1) {
        a = pop_blocktop(g, x);
        if (a >= g->FCharY) goto sides;
        if (u8(a - FLOORHEIGHT) >= g->topej) goto sides;
    }
    g->FCharCU = g->topej = pop_blocktop(g, x);
sides:
    /* left of a panel (his leading edge is in the panel's block): cut on the right */
    g->blockx = pop_getblockx(g, g->CDLeftEj);
    a = pop_rdblock(g, g->ch.scrn, g->blockx, g->ch.block_y);
    if (a == POP_PANELWOF || a == POP_PANELWIF) {
        if (NEG(g->ch.face) || g->ch.action != 2) {          /* (hanging to the right: no need to look) */
            a = pop_rdblock(g, g->ch.scrn, g->blockx, g->topblock);
            if (a != POP_BLOCK && a != POP_PANELWOF && a != POP_PANELWIF) goto wall;
        }
        g->FCharCR = u8((g->tempblockx << 2) + 4);           /* :r2 */
        return;
    }
wall:
    /* left of a solid block, feet and head */
    g->blockx = pop_getblockx(g, g->CDRightEj);
    if (pop_rdblock(g, g->ch.scrn, g->blockx, g->ch.block_y) != POP_BLOCK) return;
    if (pop_rdblock(g, g->ch.scrn, g->blockx, g->topblock) != POP_BLOCK) return;
    if (g->tempscrn != g->ch.scrn) return;
    g->FCharCR = u8(g->tempblockx << 2);
}

void pop_unevenfloor(pop_game *g)
{
    if (pop_getunderft(g) == POP_DPRESSPLATE) g->ch.y++;
}

/* --- sword and star ------------------------------------------------------------------------------ */

void pop_setupsword(pop_game *g)
{
    const pop_frametab *t = g->frames;
    uint8_t a;
    size_t off;
    if (!(g->ch.id == 2 && NEG(g->ch.life))                  /* a live guard's always shows */
            && !(g->ch.posn >= 229 && g->ch.posn < 238)      /* sheathing */
            && g->ch.sword == 0) {
        return;
    }
    a = g->Fsword & 0x3F;
    if (a == 0) return;                        /* no sword in this frame */
    g->framepoint = (uint16_t)(3u * (uint8_t)(a - 1) + t->swordtab);       /* getswordframe */
    off = (uint16_t)(g->framepoint - t->origin);
    if (g->framepoint < t->origin || off + 3u > t->size) { g->unmodelled = 1; return; }
    if (t->data[off] == 0) return;
    g->FCharImage = t->data[off];              /* decodeswim */
    g->FCharTable = SWORDTABLE;
    g->Fdx = t->data[off + 1];
    g->Fdy = t->data[off + 2];
    pop_addfcharx(g, g->Fdx);
    g->FCharY = u8(g->FCharY + g->Fdy);
    pop_addcharobj(g, POP_OBJ_SWORD);
}

static void comix(pop_game *g)
{
    uint8_t posn = g->ch.posn, dx = 5;
    g->FCharIndex = 0xFF;
    if (posn == 185 || (posn != 177 && posn >= 106 && posn < 111)) {     /* dead, crouching: low */
        g->FCharY = u8(g->FCharY + 4);
    } else if (posn == 177) {                  /* impaled */
        dx = u8(-5);
    } else {
        if (posn == 178) return;               /* halved: none */
        /* ("kid strikes lower than opponent") */
        g->FCharY = u8(g->FCharY + (g->ch.id == 0 ? -15 : -11));
    }
    pop_addfcharx(g, dx);
    /* the colour is the dot's parity: the kid's star red, the others' blue */
    if ((((g->ch.id == 0 ? 0u : 1u) ^ g->FCharX ^ g->FCharFace) & 1u) == 0) g->FCharX++;
    g->FCharImage = STARIMAGE;
    g->FCharTable = STARTABLE;
    g->FCharCU = g->FCharCL = 0;
    g->FCharCR = 40;
    g->FCharCD = 192;
    pop_addcharobj(g, POP_OBJ_COMIX);
}

void pop_setupcomix(pop_game *g)
{
    /* saveFChar / restoreFChar */
    uint8_t image = g->FCharImage, y = g->FCharY, face = g->FCharFace, index = g->FCharIndex,
            cu = g->FCharCU, cd = g->FCharCD, cl = g->FCharCL, cr = g->FCharCR,
            table = g->FCharTable;
    uint16_t x = g->FCharX;
    comix(g);
    g->FCharImage = image; g->FCharX = x; g->FCharY = y; g->FCharFace = face;
    g->FCharIndex = index; g->FCharCU = cu; g->FCharCD = cd; g->FCharCL = cl;
    g->FCharCR = cr; g->FCharTable = table;
}

/* --- characters ---------------------------------------------------------------------------------- */

static void setup(pop_game *g)
{
    pop_setupchar(g);
    pop_unevenfloor(g);
    pop_getedges(g);
    pop_indexchar(g);
    pop_quickfg(g);
    pop_quickfloor(g);
    pop_cropchar(g);
}

void pop_setupkid(pop_game *g)
{
    pop_loadkid(g);
    pop_rereadblocks(g);
    if (g->ch.posn == 0) return;               /* (a delay loop instead: `pause` 25) */
    setup(g);
    pop_addcharobj(g, POP_OBJ_KID);
}

void pop_setupshad(pop_game *g)
{
    pop_loadshad(g);
    pop_rereadblocks(g);
    setup(g);
    if (g->ch.id != 1) {
        pop_addcharobj(g, POP_OBJ_GD);
        return;
    }
    /* the shadow, as it jumps out of the mirror, is cut at the mirror */
    if (g->level == MIRLEVEL && g->ch.scrn == MIRSCRN) g->FCharCL = MIRX * 4 + 1;
    pop_addcharobj(g, POP_OBJ_SHAD);
}

void pop_addchars(pop_game *g)
{
    pop_reflection(g);
    if (g->shad.face != 86 && g->shad.scrn == g->VisScrn) {  /* (86: there is no opponent) */
        pop_setupshad(g);
        if (NEG(g->ChgOppStr)) pop_setupcomix(g);            /* he has been hurt */
        pop_setupsword(g);
    }
    if (g->kid.scrn != 0 && g->kid.scrn == g->VisScrn) {
        pop_setupkid(g);
        if (NEG(g->ChgKidStr)) pop_setupcomix(g);
        pop_setupsword(g);
    }
    if (g->ChgKidStr != 0) pop_markkidmeter(g);              /* checkmeters */
    if (g->ChgOppStr != 0) pop_markoppmeter(g);
}
