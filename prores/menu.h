#ifndef MENU_H
#define MENU_H

#include "z63fw.h"

/* The two rows the plugin adds to the camera's own menus, and the page
 * behind the first one. Nothing of the menu engine's code is patched:
 * every structure the engine reads is either a copy in the cave that a
 * .data pointer now names, or a handler pointer in .data. */

/* The stock structures are the ones this build read: 1, or 0 with nothing
 * written. Checked before menu_install and independently of the codec
 * side, so one of the two can be installed without the other. */
int menu_fingerprint(void);

/* The eight runtime text slots the rows draw from have buffers: without
 * them the writer would copy to address zero, and the rows are left out
 * (the camera stays stock and codec_install still applies). */
int menu_slots_ready(void);

/* Add the rows, with the plugin's block to build them in. 0, or an error
 * with no .data word changed -- what is written into the block until then
 * is memory the camera does not read. */
int menu_install(const struct nlr_api *api, uint64_t block);

/* The video row's value text ("HQ", "LT", ... or empty when the file type
 * is not ProRes 422), stored for the next refresh. */
void menu_show_grade(void);

#endif /* MENU_H */
