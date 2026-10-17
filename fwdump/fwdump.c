/* FWDUMP: the first step of the research features (docs/PLATFORM.md
 * phase 3). Opened from Setup -> Apps, it offers a few sets of the
 * firmware's named tables; the one chosen is read -- never written -- and
 * each table goes to the card as APPS/FWDnn.BIN, with APPS/FWDUMP.LOG
 * saying which file is which table, its size, CRC-32 and whether it still
 * held its stock bytes. The camera's state matters (pre-release on, N-RAW
 * 6K, an N-Log clip set up...), so set the camera first, then dump; dump
 * again in another state and compare.
 *
 * Every table is asked for by name (ABI 8). A name this body's profile
 * does not have is listed as such and skipped: the program never reads an
 * address of its own choosing. The sets name the Z 6III's tables; on the
 * Z 8 3.11 only isp.lut1.nlog and the gui.* tables are declared, and every
 * other line says "not on this body". APPS/FWDUMP.TXT, when present, adds
 * a custom set: one name per line.
 *
 * The program keeps no writable static data -- not even a table of
 * pointers, which would need relocating -- so it needs one memory region
 * instead of two and still opens when a resident extension holds memory
 * of its own. The name sets are NUL-separated strings, the menu rows and
 * the custom set live on the stack and in one block from mem_alloc. */
#include "nlr.h"

struct set {
    const char *title;
    const char *names; /* NUL-separated names */
    uint32_t bytes;    /* of `names`, the last NUL included */
};

static const char lut_names[] = "isp.lut1.nlog\0isp.nlg_gamma.nlog";
static const char prerelease_names[] =
    "hsfc.prerelease.c15\0hsfc.prerelease.c30\0hsfc.prerelease.c60\0"
    "hsfc.prerelease.c120\0hsfc.slot_bytes";
static const char opengate_names[] =
    "sensor.readout\0movie.geometry\0movie.raw_size\0sensor.drive_mode.class13\0"
    "sensor.drive_mode.class8\0sensor.drive_mode.dispatch\0codec.h265.modes";
static const char movie_names[] = "movie.record.state\0movie.clip.path\0vr.gyro.ring";
static const char gui_names[] =
    "gui.palette\0gui.gradients\0gui.scheme.rows\0gui.scheme.values\0gui.icons";

#define LINE_MAX 96
#define CUSTOM_MAX 2048u
#define SETS 6u

/* One set; out of line so that the compiler cannot fold the sets back
 * into an initialised table of pointers (writable data). */
__attribute__((noinline)) static void set_one(struct set *s, const char *title,
                                              const char *names, uint32_t bytes)
{
    s->title = title;
    s->names = names;
    s->bytes = bytes;
}

/* One menu row, out of line for the same reason. */
__attribute__((noinline)) static void row_one(struct nlr_row *r, uint32_t kind,
                                              const char *label)
{
    r->kind = kind;
    r->flags = 0;
    r->label = label;
    r->value = 0;
}

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
    for (int d = digits - 1; d >= 0; d--) {
        uint32_t x = (v >> (4 * d)) & 15u;
        if (*at + 1 < cap)
            out[(*at)++] = (char)(x < 10u ? '0' + x : 'a' + x - 10u);
    }
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

/* The custom set from APPS/FWDUMP.TXT into `buf` (CUSTOM_MAX bytes): line
 * ends and spaces become NULs. Returns the bytes used, 0 without a file. */
static uint32_t read_custom(const struct nlr_api *api, char *buf)
{
    int fd = api->fs_open("APPS/FWDUMP.TXT", NLR_OPEN_READ);
    if (fd < 0)
        return 0;
    int n = api->fs_read(fd, buf, CUSTOM_MAX - 1u);
    api->fs_close(fd);
    if (n <= 0)
        return 0;
    for (int i = 0; i < n; i++)
        if (buf[i] == '\n' || buf[i] == '\r' || buf[i] == ' ')
            buf[i] = 0;
    buf[n] = 0;
    return (uint32_t)n + 1u;
}

/* The first name of a set, or null when it has none. */
static const char *first_name(const struct set *set)
{
    for (uint32_t i = 0; i < set->bytes; i++)
        if (set->names[i])
            return set->names + i;
    return 0;
}

/* Dump one set; the log line per name. Returns how many tables went to
 * the card. */
static uint32_t dump(const struct nlr_api *api, const struct set *set, int log,
                     uint32_t *file_no)
{
    char line[LINE_MAX + 64];
    uint32_t written = 0;
    const char *end = set->names + set->bytes;
    for (const char *name = set->names; name < end; name += text_len(name) + 1u) {
        if (!*name)
            continue;
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
            char path[24];
            uint32_t p = 0;
            put(path, &p, sizeof path, "APPS/FWD");
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

static int32_t run(const struct nlr_api *api, char *custom)
{
    uint32_t customs = read_custom(api, custom);
    struct set sets[SETS];
    set_one(&sets[0], "LUT preview tables", lut_names, sizeof lut_names);
    set_one(&sets[1], "RAW pre-release tables", prerelease_names, sizeof prerelease_names);
    set_one(&sets[2], "Open gate tables", opengate_names, sizeof opengate_names);
    set_one(&sets[3], "Movie and gyro state", movie_names, sizeof movie_names);
    set_one(&sets[4], "GUI tables", gui_names, sizeof gui_names);
    set_one(&sets[5], "Custom (FWDUMP.TXT)", custom, customs);
    const char *first = customs ? first_name(&sets[5]) : 0;
    uint32_t count = first ? SETS : SETS - 1u;
    struct nlr_row rows[SETS + 1u];
    for (uint32_t i = 0; i < count; i++)
        row_one(&rows[i], NLR_ROW_ENTRY, sets[i].title);
    row_one(&rows[count], NLR_ROW_TEXT, "Set the camera first, then dump");
    uint32_t file_no = 0;
    /* "*all" as the custom set's first line: every set, no questions (for
     * a start at boot, or the emulator) */
    if (first && first[0] == '*') {
        int log = api->fs_open("APPS/FWDUMP.LOG", NLR_OPEN_WRITE);
        if (log < 0)
            return 2;
        for (uint32_t i = 0; i < SETS - 1u; i++) {
            api->fs_write(log, "== ", 3);
            api->fs_write(log, sets[i].title, text_len(sets[i].title));
            api->fs_write(log, "\n", 1);
            dump(api, &sets[i], log, &file_no);
        }
        api->fs_close(log);
        return 0;
    }
    uint32_t selected = 0;
    for (;;) {
        int32_t pick = api->ui_rows("Firmware tables", rows, count + 1u, selected);
        if (pick < 0 || pick == NLR_LIST_BACK || (uint32_t)pick >= count)
            return 0;
        selected = (uint32_t)pick;
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
        uint32_t n = dump(api, &sets[pick], log, &file_no);
        api->fs_close(log);
        char msg[40];
        at = 0;
        put_dec(msg, &at, sizeof msg, n);
        put(msg, &at, sizeof msg, " tables written; see FWDUMP.LOG");
        api->ui_caution(msg, NLR_CAUTION_OK);
    }
}

int32_t nlr_app_main(const struct nlr_api *api)
{
    if (!api || api->abi < 8)
        return 1;
    char *custom = api->mem_alloc(CUSTOM_MAX, 0);
    if (!custom)
        return 3;
    int32_t result = run(api, custom);
    api->mem_free(custom);
    return result;
}
