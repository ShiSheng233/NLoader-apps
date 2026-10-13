#include "doom_port.h"
#include "doomgeneric.h"
#include "doomkeys.h"
#include "m_controls.h"
#include <errno.h>
#include <stdlib.h>

const struct nlr_api *nlr_doom_api;
extern void nlr_doom_engine_shutdown(void);

int32_t nlr_app_main(const struct nlr_api *api)
{
    if (!api || api->abi < 3u || NLR_ABI_VERSION < 3u)
        return NLR_API_ENOSYS;
    if (!api->mem_alloc || !api->mem_free || !api->fs_open || !api->fs_read || !api->fs_seek ||
        !api->fs_close || !api->exit)
        return NLR_API_ENOSYS;
    nlr_doom_api = api;
    int ready = nlr_doom_platform_ready();
    if (ready != 0)
        return ready < 0 ? -ready : ready;
    /* Start the real first map with the measured small zone. Configuration
     * below is applied after the engine has loaded its own defaults. */
    char *arguments[] = {"DOOM", "-iwad",  "GAME.WAD", "-mb",    "2",        "-warp",   "1",
                         "1",    "-skill", "2",        "-nogui", "-nosound", "-nomusic"};
    doomgeneric_Create((int)(sizeof(arguments) / sizeof(arguments[0])), arguments);
    key_use = KEY_ENTER;
    key_menu_confirm = KEY_ENTER;
    key_menu_abort = KEY_ESCAPE;
    key_prevweapon = '[';
    key_nextweapon = ']';
    while (!NLR_DOOM_DIAGNOSTIC_FRAMES ||
           nlr_doom_completed_frames() < NLR_DOOM_DIAGNOSTIC_FRAMES)
        doomgeneric_Tick();
    nlr_doom_engine_shutdown();
    nlr_doom_platform_shutdown();
    nlr_doom_close_files();
    nlr_doom_free_allocations();
    return 0;
}
