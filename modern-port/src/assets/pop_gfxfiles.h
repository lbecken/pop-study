/* pop_gfxfiles.h - loaders for what the drawing code needs: the generated
 * piece tables (bgdata.bin) and the original image files (IMG.*), which
 * are read as they are from the user's checkout.
 *
 * New code for the pop-study reconstruction.
 *
 * bgdata.bin layout (little-endian):
 *     0   char[8]  magic "POPBGDT1"
 *     8   u16      image_len
 *    10   u16      symbol count
 *    12   image_len bytes: BGDATA.S assembled at 0, flame and bubble tables after it
 *    ..   symbols: u8 length, name, u16 value
 */
#ifndef POP_GFXFILES_H
#define POP_GFXFILES_H

#include "pop_gfx.h"

typedef struct pop_bgfile {
    pop_bgdata data;         /* views into `storage` */
    uint8_t   *storage;
} pop_bgfile;

/* Returns 0, or -1 with *err set (a name the drawing code needs is missing,
 * a table outside the block, a damaged file). */
int pop_bgfile_load(const char *path, pop_bgfile *out, const char **err);
void pop_bgfile_free(pop_bgfile *f);

typedef struct pop_imgfile {
    pop_imgtab tab;
    uint8_t   *storage;
} pop_imgfile;

/* An IMG.* file: checked only as far as "a count and that many pointers". */
int pop_imgfile_load(const char *path, pop_imgfile *out, const char **err);
void pop_imgfile_free(pop_imgfile *f);

#endif /* POP_GFXFILES_H */
