/* pop_seqdump - run the sequence interpreter and print one line per tick.
 *
 * New code.  Used by tests/compare_seq_traces.py to compare the C port with
 * the independent Python decoder.
 *
 *   pop_seqdump <seqtable.bin> <ticks> [left|right] [weightless]
 *
 * For every entry point 1..N it starts a character at x=128, y=100,
 * block_y=1 and prints, per tick:
 *
 *   entry tick posn x y face block_y action x_vel y_vel seq jar alert events
 *
 * `jar` and `alert` are cleared before each tick so they show that tick
 * only.  `events` is a string of letters in call order: s<n> = add_sound(n),
 * c = add_slicers, u = gone_upstairs, p = potion_effect; "-" if none.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pop_assets.h"
#include "pop_seq.h"

typedef struct { char text[128]; size_t n; } events;

static void put(events *e, const char *s)
{
    size_t len = strlen(s);
    if (e->n + len < sizeof e->text) {
        memcpy(e->text + e->n, s, len + 1);
        e->n += len;
    }
}
static void on_sound(void *ctx, uint8_t s)
{
    char buf[8];
    snprintf(buf, sizeof buf, "s%u", (unsigned)s);
    put(ctx, buf);
}
static void on_slicers(void *ctx) { put(ctx, "c"); }
static void on_upstairs(void *ctx) { put(ctx, "u"); }
static void on_potion(void *ctx) { put(ctx, "p"); }

int main(int argc, char **argv)
{
    pop_seqfile file;
    const char *err = NULL;
    int ticks, entry, status = 0;
    uint8_t face = POP_FACE_LEFT, weightless = 0;

    if (argc < 3) {
        fprintf(stderr, "usage: %s <seqtable.bin> <ticks> [left|right] [weightless]\n", argv[0]);
        return 2;
    }
    ticks = atoi(argv[2]);
    if (argc > 3 && strcmp(argv[3], "right") == 0) face = POP_FACE_RIGHT;
    if (argc > 4 && strcmp(argv[4], "weightless") == 0) weightless = 1;
    if (pop_seqfile_load(argv[1], &file, &err) != 0) {
        fprintf(stderr, "%s: %s\n", argv[1], err);
        return 2;
    }

    for (entry = 1; entry <= file.entries; entry++) {
        pop_char c;
        pop_anim_globals g;
        events ev;
        pop_anim_hooks hooks = { &ev, on_sound, on_slicers, on_upstairs, on_potion };
        int tick;

        memset(&c, 0, sizeof c);
        c.x = 128;
        c.y = 100;
        c.face = face;
        c.block_y = 1;
        c.life = 0xFF;
        if (pop_jumpseq(&file.tab, &c, (uint8_t)entry) != POP_SEQ_OK) {
            printf("%d 0 ERROR jumpseq\n", entry);
            status = 1;
            continue;
        }
        for (tick = 1; tick <= ticks; tick++) {
            pop_seq_status st;
            g.weightless = weightless;
            g.jarabove = 0;
            g.alertguard = 0;
            ev.n = 0;
            ev.text[0] = '\0';
            st = pop_animchar(&file.tab, &c, &g, &hooks);
            if (st != POP_SEQ_OK) {
                printf("%d %d ERROR %d\n", entry, tick, (int)st);
                status = 1;
                break;
            }
            printf("%d %d %u %u %u %u %u %u %u %u %u %u %u %s\n",
                   entry, tick, (unsigned)c.posn, (unsigned)c.x, (unsigned)c.y,
                   (unsigned)c.face, (unsigned)c.block_y, (unsigned)c.action,
                   (unsigned)c.x_vel, (unsigned)c.y_vel, (unsigned)c.seq,
                   (unsigned)g.jarabove, (unsigned)g.alertguard,
                   ev.n ? ev.text : "-");
        }
    }
    pop_seqfile_free(&file);
    return status;
}
