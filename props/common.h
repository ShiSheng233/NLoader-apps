/* Shared by PROPWATCH and PROPSET: a line log on the card, closed after
 * each line so the card holds every line even when the loader stops the
 * program. No writable static data. */
#ifndef PROPS_COMMON_H
#define PROPS_COMMON_H
#include "nlr.h"
#include "nlr_props.h"

struct line {
    char text[160];
    uint32_t at;
};

static void put(struct line *l, const char *s)
{
    while (*s && l->at < sizeof l->text - 2)
        l->text[l->at++] = *s++;
}

static void put_dec(struct line *l, int32_t v)
{
    char d[12];
    uint32_t n = 0, x = v < 0 ? 0u - (uint32_t)v : (uint32_t)v;
    do {
        d[n++] = (char)('0' + x % 10u);
        x /= 10u;
    } while (x);
    if (v < 0)
        put(l, "-");
    while (n && l->at < sizeof l->text - 2)
        l->text[l->at++] = d[--n];
}

static void put_hex(struct line *l, const uint8_t *b, uint32_t n)
{
    static const char digits[] = "0123456789abcdef";
    for (uint32_t k = 0; k < n && l->at < sizeof l->text - 3; k++) {
        l->text[l->at++] = digits[b[k] >> 4];
        l->text[l->at++] = digits[b[k] & 15];
    }
}

static void put_word(struct line *l, uint32_t v)
{
    uint8_t b[4] = { (uint8_t)(v >> 24), (uint8_t)(v >> 16), (uint8_t)(v >> 8), (uint8_t)v };
    put(l, "0x");
    put_hex(l, b, 4);
}

static void emit(const struct nlr_api *api, const char *path, struct line *l)
{
    l->text[l->at++] = '\n';
    int fd = api->fs_open(path, NLR_OPEN_UPDATE);
    if (fd < 0)
        fd = api->fs_open(path, NLR_OPEN_WRITE);
    if (fd >= 0) {
        api->fs_seek(fd, 0, NLR_SEEK_END);
        api->fs_write(fd, l->text, l->at);
        api->fs_close(fd);
    }
    l->at = 0;
}

/* "get <name> <id> r=<result> size=<n> flags=<f> val=<hex>" */
static void log_get(const struct nlr_api *api, const char *path, const char *name, uint32_t id,
                    uint32_t cap)
{
    struct line l = { .at = 0 };
    uint8_t buf[NLR_PROP_BYTES_MAX];
    uint32_t size = 0;
    struct nlr_prop_info info = { 0, 0, 0 };
    int32_t ri = api->prop_info(id, &info);
    int32_t r = api->prop_get(id, buf, cap, &size);
    put(&l, "get ");
    put(&l, name);
    put(&l, " ");
    put_word(&l, id);
    put(&l, " r=");
    put_dec(&l, r);
    put(&l, " size=");
    put_dec(&l, (int32_t)size);
    put(&l, " info=");
    put_dec(&l, ri);
    put(&l, " flags=");
    put_dec(&l, (int32_t)info.flags);
    put(&l, " val=");
    if (!r)
        put_hex(&l, buf, size < 32u ? size : 32u);
    emit(api, path, &l);
}

static inline uint32_t id_of(const char *name)
{
    const struct nlr_prop_name *p = nlr_prop_find(name);
    return p ? p->id : 0xffffu;
}
#endif
