/* ROUTEDEMO: the per-control routes (input_route, ABI 7, family B) as the
 * Z 8 QEMU fixture's program. Built twice from this source: ROUTEDEMO, a
 * program over the menu, and ROUTESHL, the same code declared as a shell.
 *
 * It takes the input on its first queue_wait, then routes UP to the camera
 * only and DOWN to both; every other control stays the program's. A press
 * of FN1 routes the half press to both, a press of FN2 routes it to the
 * program only, a press of ZOOM_OUT routes MENU and DELETE to the camera,
 * and every press of HALF makes a route call for no control at once, while
 * the half press is still held (a marker the host's instruction
 * observations key on). One line per step is appended
 * to APPS/<NAME>.LOG:
 *
 *   caps <platform_caps>
 *   route <ctl> <route> <result>
 *   mark <result>
 *   ev <type> <code> <value> <held>
 *   err <result>
 *
 * The host presses keys through the native controller and reads the log
 * from the card image afterwards; what the camera saw is judged from the
 * screenshots and the stock instruction observations. A line is written
 * and closed per step, so the log is complete even if the loader stops the
 * program. State lives on the stack (no writable static data). */
#include "nlr.h"

#ifndef ROUTEDEMO_LOG
#define ROUTEDEMO_LOG "APPS/ROUTEDEMO.LOG"
#endif

static void put_dec(char *out, uint32_t *at, int32_t v)
{
    char digits[12];
    uint32_t n = 0;
    uint32_t value = v < 0 ? (uint32_t)(-v) : (uint32_t)v;
    do {
        digits[n++] = (char)('0' + value % 10u);
        value /= 10u;
    } while (value);
    if (v < 0)
        out[(*at)++] = '-';
    while (n)
        out[(*at)++] = digits[--n];
}

static void put_text(char *out, uint32_t *at, const char *text)
{
    while (*text)
        out[(*at)++] = *text++;
}

static void log_line(const struct nlr_api *api, const char *line, uint32_t len)
{
    int fd = api->fs_open(ROUTEDEMO_LOG, NLR_OPEN_UPDATE);
    if (fd < 0)
        fd = api->fs_open(ROUTEDEMO_LOG, NLR_OPEN_WRITE);
    if (fd < 0)
        return;
    api->fs_seek(fd, 0, NLR_SEEK_END);
    api->fs_write(fd, line, len);
    api->fs_close(fd);
}

/* Route `count` controls and log one line per control with the result. */
static void route(const struct nlr_api *api, const uint32_t *ctls,
                  uint32_t count, uint32_t where)
{
    int32_t result = api->input_route(ctls, count, where);
    for (uint32_t n = 0; n < count; n++) {
        char line[48];
        uint32_t at = 0;
        put_text(line, &at, "route ");
        put_dec(line, &at, (int32_t)ctls[n]);
        line[at++] = ' ';
        put_dec(line, &at, (int32_t)where);
        line[at++] = ' ';
        put_dec(line, &at, result);
        line[at++] = '\n';
        log_line(api, line, at);
    }
}

int32_t nlr_app_main(const struct nlr_api *api)
{
    if (!api || api->abi < 7u)
        return 0;
    char line[96];
    uint32_t at = 0;
    put_text(line, &at, "caps ");
    put_dec(line, &at, (int32_t)api->platform_caps());
    line[at++] = '\n';
    log_line(api, line, at);
    /* The first wait takes the input with the default routes. */
    struct nlr_event ev;
    int32_t first = api->queue_wait(&ev, 1);
    const uint32_t up = NLR_CTL_UP, down = NLR_CTL_DOWN;
    route(api, &up, 1, NLR_ROUTE_CAMERA);
    route(api, &down, 1, NLR_ROUTE_BOTH);
    int32_t result = first;
    for (uint32_t count = 0; count < 4000u; count++) {
        if (count || first <= 0)
            result = api->queue_wait(&ev, 2000);
        at = 0;
        if (result < 0) {
            put_text(line, &at, "err ");
            put_dec(line, &at, result);
            line[at++] = '\n';
            log_line(api, line, at);
            break;
        }
        if (result == 0)
            continue; /* no event in two seconds: nothing to record */
        put_text(line, &at, "ev ");
        put_dec(line, &at, ev.type);
        line[at++] = ' ';
        put_dec(line, &at, ev.code);
        line[at++] = ' ';
        put_dec(line, &at, ev.value);
        line[at++] = ' ';
        put_dec(line, &at, (int32_t)(api->input_held() & 0x7fffffffu));
        line[at++] = '\n';
        log_line(api, line, at);
        if (ev.type != NLR_EV_CONTROL || ev.value != 1)
            continue;
        const uint32_t half = NLR_CTL_HALF;
        const uint32_t chord[2] = {NLR_CTL_MENU, NLR_CTL_DELETE};
        if (ev.code == NLR_CTL_FN1)
            route(api, &half, 1, NLR_ROUTE_BOTH);
        else if (ev.code == NLR_CTL_FN2)
            route(api, &half, 1, NLR_ROUTE_PROGRAM);
        else if (ev.code == NLR_CTL_ZOOM_OUT)
            route(api, chord, 2, NLR_ROUTE_CAMERA);
        else if (ev.code == NLR_CTL_HALF) {
            /* A marker: a route call that changes nothing, so the host can
             * tie a stock observation to this moment. */
            int32_t marked = api->input_route(&half, 0, NLR_ROUTE_BOTH);
            at = 0;
            put_text(line, &at, "mark ");
            put_dec(line, &at, marked);
            line[at++] = '\n';
            log_line(api, line, at);
        }
    }
    return 0;
}
