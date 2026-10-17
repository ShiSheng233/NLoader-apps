#ifndef HSFC_H
#define HSFC_H

#include "z63fw.h"

/* The submit, the pool free and the writer hold the instructions this
 * build was made for. */
int hsfc_fingerprint(void);

/* Attach the three hooks and start the program's task. 0, or the loader's
 * error (some hooks may then be attached: they do nothing until HE RAW is
 * chosen, and nothing then either, without the writer's). */
int hsfc_install(const struct nlr_api *api, uint64_t block);

/* The words HERAW.CFG asked for, for the boot log. */
uint32_t hsfc_tests(void);

#endif /* HSFC_H */
