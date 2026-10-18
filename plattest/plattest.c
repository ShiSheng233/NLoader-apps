/* PLATTEST: the platform layer's refusals and its named tables, as the
 * Z 8 QEMU fixture's program (tests/qemu/plat_tables.py). Opened from the
 * NLoader list (so it is not a shell and not the boot program), it appends
 * one line per check to APPS/PLATTEST.LOG:
 *
 *   run <variant>                      which build of this source ran
 *   caps <platform_caps>
 *   claim <rear> <evf>                 screen_claim: -EBUSY (opened from the
 *                                      menu), -ENOSYS (EVF)
 *   absent <prop_set> <bus_i2c> <hook> refusals: prop_set is declared on
 *                                      the Z 8 C3.11 (scalar items), so its
 *                                      write reaches the stock setter, which
 *                                      refuses 0x07000510 = 0 (-EACCES; a body
 *                                      without prop_set answers -ENOSYS);
 *                                      bus_i2c undeclared (-ENOSYS); no such
 *                                      point (-ENOENT). Z 8 QEMU log line:
 *                                      "absent -13 -38 -2"
 *   present <card> <prop_get> <size> <timer> <event>
 *                                      declared ones work
 *   reserved <bytes> <ok>              mem_reserved: the declared memory,
 *                                      written at both ends (ok 1), or 0
 *   named <name> <r> <kind> <flags> <size> <crc>   for each table
 *   entry <r> <value>                  gui.palette entry PLAT_ENTRY now
 *   write <r> <back> <flags>           a new colour there, read back
 *   stack <r> <back> <first>           a second entry on top (stacked)
 *   journal <write> <update> <read>   LOADER.LOG: -EACCES, -EACCES, 0
 *   repack <read> <row0> <write> <restore>
 *                                      isp.lut1.nlog: readable, -EACCES,
 *                                      nothing to restore (-ENOENT)
 *   done
 *
 * The program leaves its palette writes in place and returns: the loader
 * must put the stock table back when it ends. The next run's `named` line
 * shows whether it did (flags back to stock, entry back to the stock
 * colour).
 *
 * Variants (PLAT_VARIANT at build time): 0 the checks above; 1 only the
 * reservation (built with a reservation no heap has); 2 the same checks
 * built as an NPLD v2 program that MANIFEST must name. State lives on the
 * stack. */
#include "nlr.h"

#ifndef PLAT_VARIANT
#define PLAT_VARIANT 0
#endif
/* gui.palette's index of the stock highlight yellow on the Z 8 C3.11
 * (0xfcd723ff; the Z 6III's is 62): only a value to write and compare,
 * never an address. */
#define PLAT_ENTRY 60u
#define PLAT_STOCK 0xfcd723ffu
#define PLAT_NEW   0xe36617ffu
#define PLAT_ENTRY2 33u

static void put_dec(char *out, uint32_t *at, int64_t v)
{
    char digits[24];
    uint32_t n = 0;
    uint64_t value = v < 0 ? (uint64_t)(-v) : (uint64_t)v;
    do {
        digits[n++] = (char)('0' + value % 10u);
        value /= 10u;
    } while (value);
    if (v < 0)
        out[(*at)++] = '-';
    while (n)
        out[(*at)++] = digits[--n];
}

static void put_hex(char *out, uint32_t *at, uint32_t v)
{
    static const char hex[] = "0123456789abcdef";
    for (int d = 7; d >= 0; d--)
        out[(*at)++] = hex[(v >> (4 * d)) & 15u];
}

static void put_text(char *out, uint32_t *at, const char *text)
{
    while (*text)
        out[(*at)++] = *text++;
}

static void log_line(const struct nlr_api *api, const char *line, uint32_t len)
{
    int fd = api->fs_open("APPS/PLATTEST.LOG", NLR_OPEN_UPDATE);
    if (fd < 0)
        fd = api->fs_open("APPS/PLATTEST.LOG", NLR_OPEN_WRITE);
    if (fd < 0)
        return;
    api->fs_seek(fd, 0, NLR_SEEK_END);
    api->fs_write(fd, line, len);
    api->fs_close(fd);
}

/* "<label> <a> <b> ...\n" with up to six numbers */
static void say(const struct nlr_api *api, const char *label, uint32_t count,
                const int64_t *v)
{
    char line[160];
    uint32_t at = 0;
    put_text(line, &at, label);
    for (uint32_t n = 0; n < count; n++) {
        line[at++] = ' ';
        put_dec(line, &at, v[n]);
    }
    line[at++] = '\n';
    log_line(api, line, at);
}

static void reserved(const struct nlr_api *api)
{
    uint32_t bytes = 1;
    uint8_t *block = api->mem_reserved(&bytes);
    int64_t ok = 0;
    if (block && bytes) {
        block[0] = 0x5a;
        block[bytes - 1u] = 0xa5;
        ok = block[0] == 0x5a && block[bytes - 1u] == 0xa5;
    }
    int64_t v[2] = {(int64_t)bytes, ok && block ? 1 : 0};
    say(api, "reserved", 2, v);
}

static void named(const struct nlr_api *api, const char *name)
{
    struct nlr_named n = {0};
    int32_t r = api->named(name, &n);
    char line[160];
    uint32_t at = 0;
    put_text(line, &at, "named ");
    put_text(line, &at, name);
    line[at++] = ' ';
    put_dec(line, &at, r);
    line[at++] = ' ';
    put_dec(line, &at, n.kind);
    line[at++] = ' ';
    put_hex(line, &at, n.flags);
    line[at++] = ' ';
    put_dec(line, &at, n.size);
    line[at++] = ' ';
    put_hex(line, &at, n.crc);
    line[at++] = '\n';
    log_line(api, line, at);
}

int32_t nlr_app_main(const struct nlr_api *api)
{
    if (!api || api->abi < 8u)
        return 0;
    /* the reservation first, before the heap is cut up further */
    int64_t v[6] = {PLAT_VARIANT};
    say(api, "run", 1, v);
    reserved(api);
    if (PLAT_VARIANT == 1) {
        api->sleep_ms(2000); /* long enough for the fixture to see it run */
        log_line(api, "done\n", 5);
        return 0;
    }
    v[0] = (int64_t)api->platform_caps();
    say(api, "caps", 1, v);

    struct nlr_surface surface = {0};
    v[0] = api->screen_claim(NLR_SCREEN_REAR, &surface);
    v[1] = api->screen_claim(NLR_SCREEN_EVF, &surface);
    say(api, "claim", 2, v);

    uint32_t value = 0;
    void *original = 0;
    uint8_t reg = 0x75, who = 0;
    v[0] = api->prop_set(0x07000510u, &value, 4);
    v[1] = api->bus_i2c("i2c:imu", &reg, 1, &who, 1);
    v[2] = api->hook_attach("store.notify", (void *)reserved, &original);
    say(api, "absent", 3, v);

    uint8_t prop[256];
    uint32_t size = 0;
    v[0] = api->card_present(0);
    v[1] = api->prop_get(0x0302u, prop, sizeof prop, &size);
    v[2] = size;
    int32_t timer = api->timer_start(50, 7);
    v[3] = timer;
    struct nlr_event ev = {0};
    int32_t got = 0;
    for (uint32_t n = 0; n < 20 && got != 2; n++) {
        int32_t w = api->queue_wait(&ev, 500);
        if (w == 1 && ev.type == NLR_EV_TIMER && ev.value == 7)
            got = 2;
        else if (w < 0)
            got = w;
    }
    v[4] = got;
    if (timer >= 0)
        api->timer_stop(timer);
    say(api, "present", 5, v);

    named(api, "gui.palette");
    named(api, "gui.gradients");
    named(api, "gui.scheme.rows");
    named(api, "gui.scheme.values");
    named(api, "gui.icons");
    named(api, "isp.lut1.nlog");
    named(api, "isp.nlg_gamma.nlog"); /* not declared: -ENOENT */

    uint32_t entry = 0, back = 0, now = PLAT_NEW;
    v[0] = api->table_read("gui.palette", 4u * PLAT_ENTRY, &entry, 4);
    v[1] = entry;
    v[2] = entry == PLAT_STOCK;
    say(api, "entry", 3, v);
    struct nlr_named n = {0};
    v[0] = api->table_write("gui.palette", 4u * PLAT_ENTRY, &now, 4);
    api->table_read("gui.palette", 4u * PLAT_ENTRY, &back, 4);
    v[1] = back;
    api->named("gui.palette", &n);
    v[2] = n.flags;
    say(api, "write", 3, v);
    uint32_t second = 0x171608ffu, back2 = 0;
    v[0] = api->table_write("gui.palette", 4u * PLAT_ENTRY2, &second, 4);
    api->table_read("gui.palette", 4u * PLAT_ENTRY2, &back2, 4);
    api->table_read("gui.palette", 4u * PLAT_ENTRY, &back, 4);
    v[1] = back2;
    v[2] = back == PLAT_NEW;
    say(api, "stack", 3, v);

    /* the loader's own log: readable, never opened for writing */
    v[0] = api->fs_open("LOADER.LOG", NLR_OPEN_WRITE);
    v[1] = api->fs_open("LOADER.LOG", NLR_OPEN_UPDATE);
    int log_fd = api->fs_open("LOADER.LOG", NLR_OPEN_READ);
    v[2] = log_fd >= 0 ? 0 : log_fd;
    if (log_fd >= 0)
        api->fs_close(log_fd);
    say(api, "journal", 3, v);

    uint16_t row0 = 0, mark = 0x1234;
    v[0] = api->table_read("isp.lut1.nlog", 0, &row0, 2);
    v[1] = row0;
    v[2] = api->table_write("isp.lut1.nlog", 0, &mark, 2);
    v[3] = api->table_restore("isp.lut1.nlog");
    say(api, "repack", 4, v);

    /* leave the palette changed for a while (the stock GUI may draw with
     * it), then return without restoring: the loader's job */
    api->sleep_ms(3000);
    log_line(api, "done\n", 5);
    return 0;
}
