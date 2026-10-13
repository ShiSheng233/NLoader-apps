/* hello: foreground SDK example (PLAN 6.4). It exercises three observable
 * paths and nothing else:
 *   0   - normal: draw text, read HELLO.TXT, show it, return 0;
 *   1-3 - failure paths return nonzero (no table, open failed, read failed);
 *   4   - explicit exit: the file's first byte 'q' asks the loader to end
 *         the program through api->exit(4).
 * Only the API table is used; no firmware addresses, no direct calls. The
 * text channel may still return -NLR_API_ENOSYS before it is wired; the
 * return and exit paths do not depend on it succeeding.
 */

#include "nlr.h"

#define NLR_ERROR_NO_API  1
#define NLR_ERROR_NO_FILE 2
#define NLR_ERROR_READ    3
#define NLR_EXIT_REQUEST  4

/* Relative paths stay inside the loader's configured card directory. */
static const char hello_path[] = "HELLO.TXT";

int32_t nlr_app_main(const struct nlr_api *api)
{
    char line[64];
    int fd;
    int got;

    if (api == 0 || api->abi < 2u)
        return NLR_ERROR_NO_API; /* never trust a table newer than this SDK */

    api->ui_draw_text(0, 0, "hello: NLoader");
    api->ui_present();

    fd = api->fs_open(hello_path, 0);
    if (fd < 0) {
        api->log("hello: open failed: %d", fd);
        api->ui_draw_text(0, 20, "hello: HELLO.TXT missing");
        api->ui_present();
        return NLR_ERROR_NO_FILE;
    }
    got = api->fs_read(fd, line, (uint32_t)(sizeof line) - 1u);
    if (got < 0) {
        api->fs_close(fd);
        api->log("hello: read failed: %d", got);
        return NLR_ERROR_READ;
    }
    api->fs_close(fd);
    if (got > (int)(sizeof line) - 1)
        got = (int)(sizeof line) - 1;
    line[got] = '\0';

    api->ui_draw_text(0, 20, line);
    api->ui_present();
    api->log("hello: read %d bytes", got);

    if (line[0] == 'q') {
        api->exit(NLR_EXIT_REQUEST); /* foreground only; never returns */
        return NLR_EXIT_REQUEST;     /* defensive: exit should not return */
    }
    return 0;
}
