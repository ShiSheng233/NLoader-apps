#include "doom_port.h"
#include <stdlib.h>

/* The pinned generic engine runs shutdown hooks but its non-desktop I_Quit
 * returns. Keep those hooks, then complete the loader's normal exit path. */
extern void nlr_doom_engine_shutdown(void);

void I_Quit(void)
{
    nlr_doom_engine_shutdown();
    exit(0);
}
