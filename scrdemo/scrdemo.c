/* SCRDEMO: the rear screen and the settings store as a shell sees them
 * (ABI 7, families A, C and D), as the Z 8 QEMU fixture's program. Opened
 * from Apps it runs over live view; it appends one line per step to
 * APPS/SCRDEMO.LOG:
 *
 *   caps <platform_caps>
 *   prop <id> <result> <size> <first bytes, hex>
 *   claim <result> <format> <width> <height> <stride> <alpha_stride>
 *   present <result>
 *   release <result>
 *   ev <type> <code> <value>
 *   done
 *
 * The first claim draws three vertical bands (red, green, blue, opaque);
 * after a release and a second claim it draws one grey band across the
 * middle over a transparent rest, so a screenshot tells the two frames
 * apart. Waits are queue_wait, so screen events (taken away / shown) are
 * logged as they come. A line is written and closed per step, so the log
 * on the card is complete even if the loader stops the program.
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

static void put_hex(char *out, uint32_t *at, uint32_t v, uint32_t nibbles)
{
    while (nibbles--)
        out[(*at)++] = "0123456789abcdef"[(v >> (nibbles * 4u)) & 0xfu];
}

static void put_text(char *out, uint32_t *at, const char *text)
{
    while (*text)
        out[(*at)++] = *text++;
}

static void log_line(const struct nlr_api *api, const char *line, uint32_t len)
{
    int fd = api->fs_open("APPS/SCRDEMO.LOG", NLR_OPEN_UPDATE);
    if (fd < 0)
        fd = api->fs_open("APPS/SCRDEMO.LOG", NLR_OPEN_WRITE);
    if (fd < 0)
        return;
    api->fs_seek(fd, 0, NLR_SEEK_END);
    api->fs_write(fd, line, len);
    api->fs_close(fd);
}

static void log_result(const struct nlr_api *api, const char *what, int32_t result)
{
    char line[48];
    uint32_t at = 0;
    put_text(line, &at, what);
    line[at++] = ' ';
    put_dec(line, &at, result);
    line[at++] = '\n';
    log_line(api, line, at);
}

/* One 4:2:2 pixel pair (Y0 Y1 U V, BT.601) per colour. */
struct pair {
    uint8_t y, u, v;
};

static void fill(const struct nlr_surface *s, uint32_t x0, uint32_t x1,
                 uint32_t y0, uint32_t y1, struct pair c, uint8_t alpha)
{
    for (uint32_t y = y0; y < y1 && y < s->height; y++) {
        uint8_t *row = s->pixels + (uint64_t)y * s->stride;
        uint8_t *arow = s->alpha ? s->alpha + (uint64_t)y * s->alpha_stride : 0;
        for (uint32_t x = x0 & ~1u; x < x1 && x + 1u < s->width + 1u; x += 2u) {
            row[x * 2u + 0u] = c.y;
            row[x * 2u + 1u] = c.y;
            row[x * 2u + 2u] = c.u;
            row[x * 2u + 3u] = c.v;
            if (arow) {
                arow[x] = alpha;
                arow[x + 1u] = alpha;
            }
        }
    }
}

static int32_t claim(const struct nlr_api *api, struct nlr_surface *s)
{
    char line[96];
    uint32_t at = 0;
    int32_t result = api->screen_claim(NLR_SCREEN_REAR, s);
    put_text(line, &at, "claim ");
    put_dec(line, &at, result);
    if (result == 0) {
        line[at++] = ' ';
        put_dec(line, &at, (int32_t)s->format);
        line[at++] = ' ';
        put_dec(line, &at, (int32_t)s->width);
        line[at++] = ' ';
        put_dec(line, &at, (int32_t)s->height);
        line[at++] = ' ';
        put_dec(line, &at, (int32_t)s->stride);
        line[at++] = ' ';
        put_dec(line, &at, (int32_t)s->alpha_stride);
    }
    line[at++] = '\n';
    log_line(api, line, at);
    return result;
}

/* Waits about `ms`, logging every event that arrives meanwhile. */
static void wait_logging(const struct nlr_api *api, uint32_t ms)
{
    for (uint32_t spent = 0; spent < ms; spent += 500u) {
        struct nlr_event ev;
        int32_t result = api->queue_wait(&ev, 500);
        if (result <= 0) {
            if (result < 0) {
                log_result(api, "err", result);
                api->sleep_ms(500);
            }
            continue;
        }
        char line[64];
        uint32_t at = 0;
        put_text(line, &at, "ev ");
        put_dec(line, &at, (int32_t)ev.type);
        line[at++] = ' ';
        put_dec(line, &at, (int32_t)ev.code);
        line[at++] = ' ';
        put_dec(line, &at, ev.value);
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

    /* Group 0's first items and two of group 3, read back as raw bytes. */
    static const uint16_t ids[] = {0x0000, 0x0001, 0x0002, 0x0302, 0x0127};
    for (uint32_t n = 0; n < sizeof ids / sizeof ids[0]; n++) {
        uint8_t value[16];
        uint32_t size = 0;
        int32_t result = api->prop_get(ids[n], value, sizeof value, &size);
        at = 0;
        put_text(line, &at, "prop ");
        put_hex(line, &at, ids[n], 4);
        line[at++] = ' ';
        put_dec(line, &at, result);
        line[at++] = ' ';
        put_dec(line, &at, (int32_t)size);
        line[at++] = ' ';
        for (uint32_t b = 0; result == 0 && b < size && b < 8u; b++)
            put_hex(line, &at, value[b], 2);
        line[at++] = '\n';
        log_line(api, line, at);
    }

    struct nlr_surface s;
    if (claim(api, &s) == 0) {
        const struct pair red = {76, 85, 255}, green = {150, 44, 21},
                          blue = {29, 255, 107};
        uint32_t third = s.width / 3u;
        fill(&s, 0, third, 0, s.height, red, 255);
        fill(&s, third, third * 2u, 0, s.height, green, 255);
        fill(&s, third * 2u, s.width, 0, s.height, blue, 255);
        log_result(api, "present", api->screen_present(NLR_SCREEN_REAR, 0, 0));
        wait_logging(api, 12000);
        log_result(api, "release", api->screen_release(NLR_SCREEN_REAR));
        wait_logging(api, 8000);
        if (claim(api, &s) == 0) {
            const struct pair grey = {128, 128, 128};
            fill(&s, 0, s.width, s.height / 3u, s.height * 2u / 3u, grey, 255);
            log_result(api, "present", api->screen_present(NLR_SCREEN_REAR, 0, 0));
            wait_logging(api, 8000);
            log_result(api, "release", api->screen_release(NLR_SCREEN_REAR));
        }
    }
    wait_logging(api, 4000);
    log_line(api, "done\n", 5);
    return 0;
}
