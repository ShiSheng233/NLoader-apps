#ifndef MENU_H
#define MENU_H

#include "z63fw.h"

/* The HSFC page is the one this build knows, and nothing has moved it. */
int menu_fingerprint(void);

/* The HE RAW row's text slot has a buffer to write into. */
int menu_slots_ready(void);

/* Put the HE RAW row in place, its tables in `block`. 0. */
int menu_install(uint64_t block);

#endif /* MENU_H */
