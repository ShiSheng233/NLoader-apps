/* STORESPY: a service (ABI 7) that records the camera's settings and status
 * store while the camera is used as usual -- the way to learn which item
 * holds what a body shows (exposure mode, aperture, EV compensation, focus,
 * battery...) when the emulator cannot turn that control.
 *
 * At start it lists every item (prop_info over 0x0000..0x84FF) and writes
 * their values to APPS/SPY0.TXT. Then, five times a second, it reads them
 * all again (prop_get) and appends each change to APPS/SPY.LOG:
 *
 *     12345 2008 8: c000000062040000 -> c0000000ba040000 (APEX 5.67 -> 6.30)
 *
 * time in ms since power-on (the clock programs see), the item, its size,
 * old and new bytes (the first 32 of a bigger item), and for a 4-byte item
 * its value as a number, for an exposure blob {192, v*192} its APEX value.
 * An item that changes more than 3 times in five seconds (a meter, a timer,
 * a blinking mark) is "busy": it is written at most once every five seconds
 * after that, and only when it differs from its last line. The
 * log goes to the card once a second; each boot starts with a line saying
 * so, and past 8 MiB it stops.
 *
 * A resident program keeps no writable static data (the loader would give
 * it a second block for the boot): everything lives on the task's stack or
 * in one block from mem_alloc. */
#include "nlr.h"

#define LOG_PATH "APPS/SPY.LOG"
#define SNAP_PATH "APPS/SPY0.TXT"
#define MAX_ITEMS 4800u
#define KEEP 32u
#define SCAN_MS 200u
#define BUSY_CHANGES 3u
#define BUSY_WINDOW_MS 5000u
#define LOG_LIMIT (8u << 20)

struct item {
    uint16_t id;
    uint16_t size;
    uint32_t crc;          /* of the whole value */
    uint32_t window_at;    /* when this item's two-second window began */
    uint32_t logged_at;
    uint16_t changes;      /* in the window */
    uint8_t busy;
    uint8_t pending;       /* busy and changed since its last line */
    uint8_t keep[KEEP];    /* the first bytes, as last written */
};

struct out {
    const struct nlr_api *api;
    const char *path;
    char buf[8192];
    uint32_t used;
    uint32_t total;
    int started;
    int across_boots;
};

static uint32_t crc32(const uint8_t *p, uint32_t n)
{
    uint32_t c = ~0u;
    for (uint32_t i = 0; i < n; i++) {
        c ^= p[i];
        for (int k = 0; k < 8; k++)
            c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u)));
    }
    return ~c;
}

static void out_init(struct out *o, const struct nlr_api *api, const char *path,
                     int across_boots)
{
    o->api = api;
    o->path = path;
    o->used = 0;
    o->total = 0;
    o->started = 0;
    o->across_boots = across_boots;
}

static void flush(struct out *o)
{
    if (!o->used)
        return;
    const struct nlr_api *api = o->api;
    int fd = -1;
    /* later writes append; the first one appends to the log (it grows
     * across boots) and starts the snapshot afresh */
    if (o->started || o->across_boots) {
        fd = api->fs_open(o->path, NLR_OPEN_UPDATE);
        if (fd >= 0 && api->fs_seek(fd, 0, NLR_SEEK_END) < 0) {
            api->fs_close(fd);
            fd = -1;
        }
    }
    if (fd < 0 && !o->started)
        fd = api->fs_open(o->path, NLR_OPEN_WRITE);
    if (fd >= 0) {
        o->started = 1;
        uint32_t done = 0;
        while (done < o->used) {
            int w = api->fs_write(fd, o->buf + done, o->used - done);
            if (w <= 0)
                break;
            done += (uint32_t)w;
        }
        api->fs_close(fd);
        o->total += done;
    }
    o->used = 0;
}

static void put(struct out *o, const char *s)
{
    while (*s) {
        if (o->used >= sizeof o->buf)
            flush(o);
        o->buf[o->used++] = *s++;
    }
}

static void put_hex(struct out *o, uint32_t v, int digits)
{
    char t[9];
    for (int i = digits - 1; i >= 0; i--) {
        uint32_t d = v & 15u;
        t[i] = (char)(d < 10 ? '0' + d : 'a' + d - 10);
        v >>= 4;
    }
    t[digits] = 0;
    put(o, t);
}

static void put_dec(struct out *o, int64_t v)
{
    char t[24];
    int n = 0, neg = v < 0;
    uint64_t u = neg ? (uint64_t)(-(v + 1)) + 1u : (uint64_t)v;
    do {
        t[n++] = (char)('0' + u % 10u);
        u /= 10u;
    } while (u && n < 22);
    if (neg)
        put(o, "-");
    char r[24];
    for (int i = 0; i < n; i++)
        r[i] = t[n - 1 - i];
    r[n] = 0;
    put(o, r);
}

static void put_bytes(struct out *o, const uint8_t *b, uint32_t n)
{
    for (uint32_t i = 0; i < n; i++)
        put_hex(o, b[i], 2);
}

static int32_t le32s(const uint8_t *b)
{
    return (int32_t)((uint32_t)b[0] | (uint32_t)b[1] << 8 | (uint32_t)b[2] << 16 |
                     (uint32_t)b[3] << 24);
}

/* " (12 -> 13)" for a word, " (APEX 5.67 -> 6.30)" for an exposure blob */
static void put_meaning(struct out *o, const uint8_t *was, const uint8_t *now,
                        uint32_t size, int first)
{
    if (size == 4 || size == 2 || size == 1) {
        int32_t a = 0, b = 0;
        if (size == 4) {
            a = le32s(was);
            b = le32s(now);
        } else if (size == 2) {
            a = (int16_t)(was[0] | was[1] << 8);
            b = (int16_t)(now[0] | now[1] << 8);
        } else {
            a = was[0];
            b = now[0];
        }
        put(o, " (");
        if (!first) {
            put_dec(o, a);
            put(o, " -> ");
        }
        put_dec(o, b);
        put(o, ")");
    } else if (size >= 8 && le32s(now) == 192) {
        put(o, " (APEX ");
        for (int k = first ? 1 : 0; k < 2; k++) {
            int32_t v = le32s((k ? now : was) + 4);
            int32_t hundredths = (int32_t)(((int64_t)v * 100 + (v < 0 ? -96 : 96)) / 192);
            if (hundredths < 0) {
                put(o, "-");
                hundredths = -hundredths;
            }
            put_dec(o, hundredths / 100);
            put(o, ".");
            put_dec(o, (hundredths % 100) / 10);
            put_dec(o, hundredths % 10);
            if (!k)
                put(o, " -> ");
        }
        put(o, ")");
    }
}

static void line_start(struct out *o, uint32_t ms)
{
    char t[12];
    int n = 0;
    uint32_t v = ms;
    do {
        t[n++] = (char)('0' + v % 10u);
        v /= 10u;
    } while (v);
    for (int pad = n; pad < 8; pad++)
        put(o, " ");
    char r[12];
    for (int i = 0; i < n; i++)
        r[i] = t[n - 1 - i];
    r[n] = 0;
    put(o, r);
    put(o, " ");
}

static void spy(void *arg)
{
    const struct nlr_api *api = arg;
    /* field by field: an initializer would zero the 8 KiB buffers through
     * a memset the program has no library for */
    struct out log, snap;
    out_init(&log, api, LOG_PATH, 1);
    out_init(&snap, api, SNAP_PATH, 0);
    uint8_t value[4096];
    struct item *items = api->mem_alloc((uint32_t)(MAX_ITEMS * sizeof(struct item)), 0);
    line_start(&log, api->time_ms());
    if (!items) {
        put(&log, "---- boot: no memory for the item list\n");
        flush(&log);
        return;
    }
    uint32_t n = 0, skipped = 0;
    for (uint32_t id = 0; id < 0x8500u && n < MAX_ITEMS; id++) {
        struct nlr_prop_info info;
        if (api->prop_info(id, &info) || !info.size)
            continue;
        if (info.size > sizeof value) {
            skipped++;
            continue;
        }
        items[n].id = (uint16_t)id;
        items[n].size = info.size;
        items[n].crc = 0;
        items[n].window_at = items[n].logged_at = 0;
        items[n].changes = 0;
        items[n].busy = items[n].pending = 0;
        n++;
    }
    put(&log, "---- boot: ");
    put_dec(&log, n);
    put(&log, " items watched (");
    put_dec(&log, skipped);
    put(&log, " larger than 4 KiB left out)\n");
    flush(&log);
    /* this boot's values, every item */
    for (uint32_t k = 0; k < n; k++) {
        uint32_t size = 0;
        if (api->prop_get(items[k].id, value, sizeof value, &size) || size != items[k].size)
            continue;
        items[k].crc = crc32(value, size);
        uint32_t keep = size < KEEP ? size : KEEP;
        for (uint32_t i = 0; i < keep; i++)
            items[k].keep[i] = value[i];
        put_hex(&snap, items[k].id, 4);
        put(&snap, " ");
        put_dec(&snap, size);
        put(&snap, ": ");
        put_bytes(&snap, value, keep);
        if (size > KEEP)
            put(&snap, "...");
        put_meaning(&snap, value, value, size, 1);
        put(&snap, "\n");
    }
    flush(&snap);
    uint32_t last_flush = api->time_ms();
    for (;;) {
        api->sleep_ms(SCAN_MS);
        uint32_t now = api->time_ms();
        for (uint32_t k = 0; k < n; k++) {
            struct item *it = &items[k];
            uint32_t size = 0;
            if (api->prop_get(it->id, value, sizeof value, &size) || size != it->size)
                continue;
            uint32_t crc = crc32(value, size);
            uint32_t keep = size < KEEP ? size : KEEP;
            if (crc == it->crc) {
                /* a busy item's last change waits for its next line */
                if (it->busy && it->pending && now - it->logged_at >= BUSY_WINDOW_MS)
                    goto write;
                continue;
            }
            it->crc = crc;
            if (now - it->window_at >= BUSY_WINDOW_MS) {
                it->window_at = now;
                if (it->busy && it->changes <= 1u)
                    it->busy = 0; /* quiet again */
                it->changes = 0;
            }
            if (++it->changes > BUSY_CHANGES && !it->busy) {
                it->busy = 1;
                line_start(&log, now);
                put_hex(&log, it->id, 4);
                put(&log, " is busy: one line every five seconds from now\n");
            }
            if (it->busy && now - it->logged_at < BUSY_WINDOW_MS) {
                it->pending = 1;
                continue;
            }
        write:
            if (it->busy) {
                uint32_t same = 1;
                for (uint32_t i = 0; i < keep && same; i++)
                    same = it->keep[i] == value[i];
                if (same) {
                    it->pending = 0;
                    continue;
                }
            }
            if (log.total + log.used < LOG_LIMIT) {
                line_start(&log, now);
                put_hex(&log, it->id, 4);
                put(&log, " ");
                put_dec(&log, size);
                put(&log, ": ");
                put_bytes(&log, it->keep, keep);
                put(&log, " -> ");
                put_bytes(&log, value, keep);
                put_meaning(&log, it->keep, value, size, 0);
                put(&log, "\n");
            }
            for (uint32_t i = 0; i < keep; i++)
                it->keep[i] = value[i];
            it->logged_at = now;
            it->pending = 0;
        }
        if (now - last_flush >= 1000u) {
            flush(&log);
            last_flush = now;
        }
    }
}

int32_t nlr_extension_init(const struct nlr_api *api)
{
    if (!api || api->abi < 7 || !api->task_start || !api->prop_get || !api->prop_info)
        return 1;
    /* a low priority: the camera's own tasks come first */
    return api->task_start(spy, (void *)api, 48u * 1024u, 30) < 0;
}
