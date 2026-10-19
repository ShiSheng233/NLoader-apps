#ifndef HERAW_INSTALL_H
#define HERAW_INSTALL_H

#include "nlr.h"

/* Shared by the installer, the worker and every hook. The installer only
 * publishes ready after the whole set is attached. */
struct heraw_install_state {
    uint32_t ready;
    uint32_t stop;
    int32_t rollback;
};

typedef int (*heraw_install_op)(void *context, uint32_t index);

/* Attach the complete set as one transaction. Handlers remain dormant until
 * every hook is installed. A failure stops the worker and restores every
 * earlier site in reverse order. */
int heraw_install_hooks(volatile struct heraw_install_state *state,
                        void *context, uint32_t count,
                        heraw_install_op attach, heraw_install_op detach);

#endif /* HERAW_INSTALL_H */
