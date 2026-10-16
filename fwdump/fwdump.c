/* FWDUMP: the first step of the research features (docs/PLATFORM.md
 * phase 3). Opened from the NLoader page, it offers a few sets of the
 * firmware's named tables; the one chosen is read -- never written -- and
 * each table goes to the card as APPS/FWDnn.BIN, with APPS/FWDUMP.LOG
 * saying which file is which table, its size, CRC-32 and whether it still
 * held its stock bytes. The camera's state matters (pre-release on, N-RAW
 * 6K, an N-Log clip set up...), so set the camera first, then dump; dump
 * again in another state and compare.
 *
 * Every table is asked for by name (ABI 8). A name this body's profile
 * does not have is listed as such and skipped: the program never reads an
 * address of its own choosing. APPS/FWDUMP.TXT, when present, adds a
 * custom set: one name per line. */
#include "nlr.h"

static const struct nlr_api *api;

struct set {
    const char *title;
    const char *const *names;
};

static const char *const lut_names[] = {
    "isp.lut1.nlog", "isp.nlg_gamma.nlog", 0,
};
static const char *const prerelease_names[] = {
    "hsfc.prerelease.c15", "hsfc.prerelease.c30", "hsfc.prerelease.c60",
    "hsfc.prerelease.c120", "hsfc.slot_bytes", 0,
};
static const char *const opengate_names[] = {
    "sensor.readout", "movie.geometry", "movie.raw_size", "sensor.drive_mode.class13",
    "sensor.drive_mode.class8", "sensor.drive_mode.dispatch", "codec.h265.modes", 0,
};
static const char *const movie_names[] = {
    "movie.record.state", "movie.clip.path", "vr.gyro.ring", 0,
};
static const char *const gui_names[] = {
    "gui.palette", "gui.gradients", "gui.scheme.rows", "gui.scheme.values", "gui.icons", 0,
};

#define LINE_MAX 96
static char custom_buf[2048];
static const char *custom_names[64];

static uint32_t text_len(const char *s)
{
    uint32_t n = 0;
    while (s[n])
        n++;
    return n;
}

static void put(char *out, uint32_t *at, uint32_t cap, const char *s)
{
    while (*s && *at + 1 < cap)
        out[(*at)++] = *s++;
    out[*at] = 0;
}

static void put_hex(char *out, uint32_t *at, uint32_t cap, uint32_t v, int digits)
{
    static const char hex[] = "0123456789abcdef";
    for (int d = digits - 1; d >= 0; d--)
        if (*at + 1 < cap)
            out[(*at)++] = hex[(v >> (4 * d)) & 15u];
    out[*at] = 0;
}

static void put_dec(char *out, uint32_t *at, uint32_t cap, uint32_t v)
{
    char digits[12];
    int d = 0;
    do {
        digits[d++] = (char)('0' + v % 10u);
        v /= 10u;
    } while (v);
    while (d && *at + 1 < cap)
        out[(*at)++] = digits[--d];
    out[*at] = 0;
}

static uint32_t crc32(const uint8_t *p, uint32_t n)
{
    uint32_t c = ~0u;
    while (n--) {
        c ^= *p++;
        for (unsigned b = 0; b < 8; b++)
            c = (c >> 1) ^ (0xedb88320u & (0u - (c & 1)));
    }
    return ~c;
}

/* One custom set from APPS/FWDUMP.TXT: names, one per line. */
static uint32_t read_custom(void)
{
    int fd = api->fs_open("APPS/FWDUMP.TXT", NLR_OPEN_READ);
    if (fd < 0)
        return 0;
    int n = api->fs_read(fd, custom_buf, sizeof custom_buf - 1);
    api->fs_close(fd);
    if (n <= 0)
        return 0;
    custom_buf[n] = 0;
    uint32_t count = 0;
    char *p = custom_buf;
    while (*p && count + 1 < sizeof custom_names / sizeof *custom_names) {
        while (*p == '\n' || *p == '\r' || *p == ' ')
            p++;
        if (!*p)
            break;
        custom_names[count++] = p;
        while (*p && *p != '\n' && *p != '\r')
            p++;
        if (*p)
            *p++ = 0;
    }
    custom_names[count] = 0;
    return count;
}

/* Dump one set; the log line per name. Returns how many tables went to
 * the card. */
static uint32_t dump(const struct set *set, int log, uint32_t *file_no)
{
    char line[LINE_MAX + 64];
    uint32_t written = 0;
    for (uint32_t i = 0; set->names[i]; i++) {
        const char *name = set->names[i];
        struct nlr_named info;
        uint32_t at = 0;
        put(line, &at, sizeof line, name);
        int32_t r = api->named(name, &info);
        if (r || info.kind != NLR_NAMED_TABLE) {
            put(line, &at, sizeof line, r ? ": not on this body\n" : ": not a table\n");
            api->fs_write(log, line, at);
            continue;
        }
        void *bytes = api->mem_alloc(info.size ? info.size : 1, 0);
        if (!bytes) {
            put(line, &at, sizeof line, ": no memory\n");
            api->fs_write(log, line, at);
            continue;
        }
        r = api->table_read(name, 0, bytes, info.size);
        if (r) {
            put(line, &at, sizeof line, ": read refused\n");
        } else {
            char path[24] = "APPS/FWD";
            uint32_t p = 8;
            put_dec(path, &p, sizeof path, (*file_no)++);
            put(path, &p, sizeof path, ".BIN");
            int fd = api->fs_open(path, NLR_OPEN_WRITE);
            int ok = fd >= 0 && api->fs_write(fd, bytes, info.size) == (int)info.size;
            if (fd >= 0)
                api->fs_close(fd);
            put(line, &at, sizeof line, " -> ");
            put(line, &at, sizeof line, path + 5);
            put(line, &at, sizeof line, ok ? " " : " WRITE FAILED ");
            put_dec(line, &at, sizeof line, info.size);
            put(line, &at, sizeof line, " bytes crc ");
            put_hex(line, &at, sizeof line, crc32(bytes, info.size), 8);
            put(line, &at, sizeof line, (info.flags & NLR_NAMED_STOCK) ? " stock" : " changed");
            put(line, &at, sizeof line, " class ");
            put_dec(line, &at, sizeof line, info.flags & 0xffu);
            put(line, &at, sizeof line, "\n");
            written += ok;
        }
        api->fs_write(log, line, at);
        api->mem_free(bytes);
    }
    return written;
}

int32_t nlr_app_main(const struct nlr_api *table)
{
    api = table;
    if (!api || api->abi < 8)
        return 1;
    uint32_t customs = read_custom();
    struct set sets[] = {
        { "LUT preview tables", lut_names },
        { "RAW pre-release tables", prerelease_names },
        { "Open gate tables", opengate_names },
        { "Movie and gyro state", movie_names },
        { "GUI tables", gui_names },
        { "Custom (FWDUMP.TXT)", custom_names },
    };
    uint32_t count = customs ? 6u : 5u;
    struct nlr_row rows[7];
    for (uint32_t i = 0; i < count; i++) {
        rows[i].kind = NLR_ROW_ENTRY;
        rows[i].flags = 0;
        rows[i].label = sets[i].title;
        rows[i].value = 0;
    }
    rows[count].kind = NLR_ROW_TEXT;
    rows[count].flags = 0;
    rows[count].label = "Set the camera first, then dump";
    rows[count].value = 0;
    uint32_t file_no = 0;
    /* "*all" as the custom set's first line: every set, no questions (for
     * a start at boot, or the emulator) */
    if (customs && custom_names[0][0] == '*') {
        int log = api->fs_open("APPS/FWDUMP.LOG", NLR_OPEN_WRITE);
        if (log < 0)
            return 2;
        for (uint32_t i = 0; i < 5; i++) {
            api->fs_write(log, "== ", 3);
            api->fs_write(log, sets[i].title, text_len(sets[i].title));
            api->fs_write(log, "\n", 1);
            dump(&sets[i], log, &file_no);
        }
        api->fs_close(log);
        return 0;
    }
    for (;;) {
        int32_t pick = api->ui_rows("Firmware tables", rows, count + 1u, 0);
        if (pick < 0 || pick == NLR_LIST_BACK || (uint32_t)pick >= count)
            return 0;
        int log = api->fs_open("APPS/FWDUMP.LOG", NLR_OPEN_UPDATE);
        if (log < 0)
            log = api->fs_open("APPS/FWDUMP.LOG", NLR_OPEN_WRITE);
        if (log < 0) {
            api->ui_caution("No card to write to", NLR_CAUTION_OK);
            continue;
        }
        api->fs_seek(log, 0, NLR_SEEK_END);
        char head[LINE_MAX];
        uint32_t at = 0;
        put(head, &at, sizeof head, "== ");
        put(head, &at, sizeof head, sets[pick].title);
        put(head, &at, sizeof head, " at ");
        put_dec(head, &at, sizeof head, api->time_ms());
        put(head, &at, sizeof head, " ms\n");
        api->fs_write(log, head, at);
        uint32_t n = dump(&sets[pick], log, &file_no);
        api->fs_close(log);
        char msg[40];
        at = 0;
        put_dec(msg, &at, sizeof msg, n);
        put(msg, &at, sizeof msg, " tables written; see FWDUMP.LOG");
        api->ui_caution(msg, NLR_CAUTION_OK);
    }
}
