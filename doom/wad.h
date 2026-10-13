#ifndef NLR_WAD_H
#define NLR_WAD_H

#include "nlr.h"

/* A diagnostic work bound, not a restriction of the WAD format. No WAD
 * contents or directory-sized allocation is retained in memory. */
#define NLR_WAD_MAX_LUMPS 65536u
#define NLR_WAD_NAME_BYTES 8u

enum nlr_wad_result {
    NLR_WAD_OK = 0,
    NLR_WAD_API = 1,
    NLR_WAD_OPEN = 2,
    NLR_WAD_IO = 3,
    NLR_WAD_FORMAT = 4,
    NLR_WAD_BOUNDS = 5,
    NLR_WAD_LIMIT = 6,
    NLR_WAD_NOT_FOUND = 7,
    NLR_WAD_ARGUMENT = 8,
    NLR_WAD_CLOSE = 9,
    NLR_WAD_BUSY = 10
};

struct nlr_wad {
    const struct nlr_api *api;
    int fd;
    uint32_t file_size;
    uint32_t lump_count;
    uint32_t directory_offset;
    uint32_t ready;
};

struct nlr_wad_lump {
    uint32_t offset;
    uint32_t size;
    uint8_t name[NLR_WAD_NAME_BYTES];
};

/* Initialize before first use; never initialize over an owned open file. */
void nlr_wad_init(struct nlr_wad *wad);

/* Open read-only and check every directory entry. Failure after opening
 * attempts close. A failed close retains fd for an explicit retry or loader
 * cleanup, returns NLR_WAD_CLOSE, and never marks the object ready. */
int nlr_wad_open(struct nlr_wad *wad, const struct nlr_api *api,
                 const char *path);
int nlr_wad_close(struct nlr_wad *wad);

/* Calls below require a ready object. They do not close it; the caller
 * always owns close, including after errors. Directory entries are checked
 * again when read, so changed entry bytes cannot bypass the range checks.
 * The input file must remain unchanged during the session. */
int nlr_wad_lump(struct nlr_wad *wad, uint32_t index,
                 struct nlr_wad_lump *out);
/* Printable ASCII name, 1..8 characters; case-insensitive. Later duplicate
 * entries take precedence. out_index is unchanged on a miss or failure. */
int nlr_wad_find(struct nlr_wad *wad, const char *name, uint32_t *out_index);
/* Read exactly len bytes within one indexed lump. A zero-length read at
 * its end is allowed; no seek/read is issued for a zero-length payload. */
int nlr_wad_read(struct nlr_wad *wad, uint32_t index, uint32_t offset,
                 void *buffer, uint32_t len);

#endif
