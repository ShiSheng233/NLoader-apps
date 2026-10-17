#ifndef CODEC_H
#define CODEC_H

#include "z63fw.h"

/* The four grades, in the order the encoder's hardware type counts them
 * (0..3 all go through the 4:2:2 setup). */
#define GRADES 4u

/* What the rows show. The grade's own name, as Apple writes it, for the
 * sub-page and the shooting display; the short form for the value at the
 * right of the video menu's row. */
const char *codec_grade_name(uint32_t grade);
const char *codec_grade_short(uint32_t grade);

/* Every site this plugin patches still holds the word it was built
 * against: 1 when the firmware is the one this build knows, 0 otherwise.
 * Nothing is written either way. */
int codec_fingerprint(void);

/* Put the back half in place: this plugin's copies of the three tables in
 * `block`, then the instructions that name them and the two that open the
 * grades the menus never offered. 0, or the loader's error; the copies
 * are complete before any instruction changes. */
int codec_install(const struct nlr_api *api, uint64_t block);

/* The stored grade now applies: rewrite the copies. Called whenever the
 * choice changes -- never while recording, which is the only time the
 * camera reads them. No patching, so no loader call. */
int codec_apply(uint64_t block, uint32_t grade);

#endif /* CODEC_H */
