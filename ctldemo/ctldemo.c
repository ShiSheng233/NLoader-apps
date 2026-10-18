/* CTLDEMO: every control and dial the platform layer reports (ABI 7, family
 * B), as the Z 8 QEMU fixture's program. It takes the input on its first
 * queue_wait and appends one line per event to APPS/CTLDEMO.LOG:
 *
 *   caps <platform_caps>
 *   ev <type> <code> <value> <held>
 *   err <result>
 *
 * The host presses keys through the native controller and reads the log
 * from the card image afterwards; the held word is the mask input_held
 * returns at that event. A line is written and closed per event, so the
 * log on the card is complete even if the loader stops the program.
 *
 * State lives on the stack: a program with no writable static data gets
 * one memory region instead of two. */
#include "nlr.h"

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
    int fd = api->fs_open("APPS/CTLDEMO.LOG", NLR_OPEN_UPDATE);
    if (fd < 0)
        fd = api->fs_open("APPS/CTLDEMO.LOG", NLR_OPEN_WRITE);
    if (fd < 0)
        return;
    api->fs_seek(fd, 0, NLR_SEEK_END);
    api->fs_write(fd, line, len);
    api->fs_close(fd);
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
    for (uint32_t count = 0; count < 4000u; count++) {
        struct nlr_event ev;
        int32_t result = api->queue_wait(&ev, 2000);
        at = 0;
        if (result < 0) {
            put_text(line, &at, "err ");
            put_dec(line, &at, result);
            line[at++] = '\n';
        } else {
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
        }
        log_line(api, line, at);
        if (result < 0)
            break;
    }
    return 0;
}
