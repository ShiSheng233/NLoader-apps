#ifndef BLOCK_H
#define BLOCK_H

#include "z63fw.h"

/* The plugin's own block is ordinary memory: the camera reads it, this
 * program writes it, and nothing of the firmware's code channel is
 * involved. These are the three things that are needed often enough to
 * share. */
void block_copy_fw(uint64_t dst, uint64_t src, uint32_t len);
uint32_t text_len(const char *text);

/* One cache line's worth of the firmware's own instructions, changed in
 * place: `words` of them are read from `site`, the caller's `change` are
 * applied by index, and the whole span goes back through the loader's
 * code channel in one atomic call. The words the caller does not name are
 * written back exactly as they were, which is how a register's halves can
 * be changed where another register's are interleaved with them. */
struct word_change {
    uint32_t index;
    uint32_t word;
};

int patch_words(const struct nlr_api *api, uint64_t site, uint32_t words,
                const struct word_change *change, uint32_t changes);

/* A run of words into the firmware's text, split at the cache-line
 * boundaries the code channel requires. Used for the one trampoline this
 * plugin puts in the code window's free gap. */
int code_write(const struct nlr_api *api, uint64_t dst, const void *src,
               uint32_t len);

#endif /* BLOCK_H */
