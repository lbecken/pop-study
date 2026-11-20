/* pop_assets.h - loading generated data files.
 *
 * New code for the pop-study reconstruction.  The files it loads are
 * generated from the user's own checkout of the original source by
 * tools/extract_assets/export_port_data.py and are not part of this
 * repository.  File I/O lives here so that src/core stays free of it.
 */
#ifndef POP_ASSETS_H
#define POP_ASSETS_H

#include <stdint.h>

#include "pop_game.h"
#include "pop_seq.h"
#include "pop_sfx.h"

/* seqtable.bin layout (all integers little-endian):
 *   0   8 bytes  magic "POPSEQT1"
 *   8   u16      origin (address of the first table byte in the original)
 *   10  u16      size of the table in bytes
 *   12  u16      number of entry points
 *   14  u16      reserved, 0
 *   16  size bytes of table data
 */
#define POP_SEQFILE_MAGIC "POPSEQT1"
#define POP_SEQFILE_HEADER 16

typedef struct pop_seqfile {
    pop_seqtab tab;      /* view into `storage` */
    uint16_t   entries;  /* number of entry points */
    uint8_t   *storage;  /* owned; release with pop_seqfile_free */
} pop_seqfile;

/* Returns 0 on success.  On failure returns non-zero and, if `err` is not
 * NULL, points *err at a static description. */
int pop_seqfile_load(const char *path, pop_seqfile *out, const char **err);
void pop_seqfile_free(pop_seqfile *f);

/* sfx.bin layout (all integers little-endian):
 *   0   8 bytes  magic "POPSFX01"
 *   8   u32      CPU clock in Hz used for the cycle counts
 *   12  u16      number of effects
 *   14  u16      reserved, 0
 *   then, per effect:
 *       u32      total cycles of the PLAYBACK call
 *       u32      number of speaker accesses n
 *       n x u32  cycle of each access, ascending
 */
#define POP_SFXFILE_MAGIC "POPSFX01"

typedef struct pop_sfxfile {
    uint32_t  cpu_hz;
    uint16_t  count;
    pop_sfx  *effects;   /* count entries; owned */
    uint32_t *storage;   /* all toggle lists; owned */
} pop_sfxfile;

int pop_sfxfile_load(const char *path, pop_sfxfile *out, const char **err);
void pop_sfxfile_free(pop_sfxfile *f);

/* framedef.bin layout (all integers little-endian):
 *   0   8 bytes  magic "POPFRAM1"
 *   8   u16      origin
 *   10  u16      size of the table in bytes
 *   12  u16 x 4  addresses of Fdef, altset1, altset2, swordtab
 *   20  u16 x 2  reserved, 0
 *   24  size bytes of table data
 */
#define POP_FRAMEFILE_MAGIC "POPFRAM1"
#define POP_FRAMEFILE_HEADER 24

typedef struct pop_framefile {
    pop_frametab tab;
    uint8_t     *storage;
} pop_framefile;

int pop_framefile_load(const char *path, pop_framefile *out, const char **err);
void pop_framefile_free(pop_framefile *f);

/* imgdims.bin layout (all integers little-endian):
 *   0   8 bytes  magic "POPIDIM1"
 *   8   u16      number of tables (11)
 *   10  u16      reserved, 0
 *   then, per table:
 *       u16      number of images n
 *       n x (u8 width in bytes, u8 height in lines)
 * Tables 0-6 are character tables 1-7 as the game numbers them, with the
 * guard set as table 3 (the opponent).  Tables 7-10 are the other opponent
 * sets the game loads in its place on some levels: fat guard, shadow,
 * skeleton, vizier (by file name; which level uses which is the game's
 * business, MISC.S 774).
 */
#define POP_DIMFILE_MAGIC "POPIDIM1"
#define POP_DIMFILE_TABLES 11

typedef enum pop_opponent_set {
    POP_OPP_GUARD = 0, POP_OPP_FAT, POP_OPP_SHADOW, POP_OPP_SKELETON, POP_OPP_VIZIER,
    POP_OPP_SETS
} pop_opponent_set;

typedef struct pop_dimfile {
    pop_imgdims    dims;                     /* views into `storage` */
    uint16_t       opp_count[POP_OPP_SETS];
    const uint8_t *opp_wh[POP_OPP_SETS];
    uint8_t       *storage;                  /* owned */
} pop_dimfile;

int pop_dimfile_load(const char *path, pop_dimfile *out, const char **err);
void pop_dimfile_free(pop_dimfile *f);

/* Make character table 3 describe the given opponent image set. */
void pop_dimfile_use_opponent(pop_dimfile *f, pop_opponent_set set);

/* autodata.bin layout (all integers little-endian):
 *   0   8 bytes  magic "POPAUTO1"
 *   8   u16      length of the fight tables (124)
 *   10  u16      length of the shadow positions (24)
 *   12  u16      length of the level-5 movement script
 *   14  u16      length of the demo level's movement script
 *   then the four blocks in that order
 */
#define POP_AUTOFILE_MAGIC "POPAUTO1"

typedef struct pop_autofile {
    pop_autodata data;   /* views into `storage` */
    uint8_t     *storage;
} pop_autofile;

int pop_autofile_load(const char *path, pop_autofile *out, const char **err);
void pop_autofile_free(pop_autofile *f);

/* Read an original level file (LEVEL0..LEVEL14, 2304 bytes) into g->blueprint. */
int pop_level_load(const char *path, pop_game *g, const char **err);

#endif /* POP_ASSETS_H */
