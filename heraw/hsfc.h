#ifndef HSFC_H
#define HSFC_H

#include "z63fw.h"

/* The submit, the pool free and the writer hold the instructions this
 * build was made for. */
int hsfc_fingerprint(void);

/* Attach every hook and start the program's task. 0, or the loader's error.
 * A failed installation leaves every handler dormant and restores each site
 * that was attached. */
int hsfc_install(const struct nlr_api *api, uint64_t block);

/* First restoration error after a failed installation, or zero. */
int32_t hsfc_rollback_status(void);

/* The complete hook set is attached and may implement the menu selection. */
int hsfc_ready(void);

/* The words HERAW.CFG asked for, for the boot log. */
uint32_t hsfc_tests(void);

#endif /* HSFC_H */
