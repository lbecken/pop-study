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

#endif /* POP_ASSETS_H */
