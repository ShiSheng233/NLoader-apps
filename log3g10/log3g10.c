/*
 * Log3G10: the curve file, the switch and the three Lut1 rows (log3g10.h).
 * No floating point and no C library: built with -mgeneral-regs-only, so
 * nothing here touches a register the camera's tasks do not expect us to.
 */
#include "log3g10.h"

/* The compiler may still emit these for a copy or a clear. (A hosted
 * test build has the C library's.) */
#ifndef L3G_HOSTED
void *memset(void *dst, int c, unsigned long n)
{
    unsigned char *d = dst;
    while (n--)
        *d++ = (unsigned char)c;
    return dst;
}

void *memcpy(void *dst, const void *src, unsigned long n)
{
    unsigned char *d = dst;
    const unsigned char *s = src;
    while (n--)
        *d++ = *s++;
    return dst;
}
#endif

/* ---- text ---------------------------------------------------------------- */

void l3g_clear(struct l3g_text *t)
{
    t->n = 0;
    t->s[0] = 0;
}

void l3g_put(struct l3g_text *t, const char *s)
{
    while (*s && t->n + 1u < L3G_TEXT_MAX)
        t->s[t->n++] = *s++;
    t->s[t->n] = 0;
}

void l3g_put_int(struct l3g_text *t, int32_t v)
{
    char digits[12];
    uint32_t n = 0, u = v < 0 ? (uint32_t)0 - (uint32_t)v : (uint32_t)v;
    if (v < 0)
        l3g_put(t, "-");
    do {
        digits[n++] = (char)('0' + u % 10u);
        u /= 10u;
    } while (u);
    while (n && t->n + 1u < L3G_TEXT_MAX)
        t->s[t->n++] = digits[--n];
    t->s[t->n] = 0;
}

/* ---- the three rows ------------------------------------------------------ */

#define ROW_VALUES (L3G_LANES * L3G_POINTS)

static volatile uint16_t *ram_rows(void)
{
    return (volatile uint16_t *)L3G_RAM_ROWS;
}

/* Our stores have to be visible to the core that builds the next job. */
static void publish(void)
{
    __asm__ volatile("dsb ish" ::: "memory");
}

static int by_name(const struct nlr_api *api)
{
    struct nlr_named n;
    return api && api->abi >= 8u && api->named(L3G_TABLE, &n) == 0 &&
           n.kind == NLR_NAMED_TABLE && n.size == ROW_VALUES * 2u;
}

/* The rows as they are now, from the table or from RAM. */
static void rows_now(const struct nlr_api *api, uint16_t *out)
{
    if (by_name(api) && api->table_read(L3G_TABLE, 0, out, ROW_VALUES * 2u) == 0)
        return;
    volatile uint16_t *ram = ram_rows();
    for (uint32_t k = 0; k < ROW_VALUES; k++)
        out[k] = ram[k];
}

static uint32_t rows_crc(const uint16_t *rows)
{
    uint32_t crc = 0xffffffffu;
    for (uint32_t k = 0; k < ROW_VALUES; k++) {
        uint16_t v = rows[k];
        for (int byte = 0; byte < 2; byte++) {
            crc ^= (uint8_t)(v >> (8 * byte));
            for (int bit = 0; bit < 8; bit++)
                crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1u)));
        }
    }
    return ~crc;
}

enum l3g_state l3g_state(const struct nlr_api *api, const uint16_t *curve)
{
    uint16_t rows[ROW_VALUES];
    rows_now(api, rows);
    if (rows_crc(rows) == L3G_STOCK_CRC)
        return L3G_STATE_STOCK;
    for (uint32_t k = 0; curve && k < ROW_VALUES; k++)
        if (rows[k] != curve[k % L3G_POINTS])
            return L3G_STATE_OTHER;
    return curve ? L3G_STATE_CURVE : L3G_STATE_OTHER;
}

/* The job builder copies a whole row at a time, so a job built while we
 * write may take a row half old and half new; the next job takes the new
 * one. Lane by lane keeps that window to one row. By name, the loader
 * checks the stock CRC before its first write and keeps the stock rows. */
int l3g_write(const struct nlr_api *api, const uint16_t *curve)
{
    if (by_name(api)) {
        for (uint32_t lane = 0; lane < L3G_LANES; lane++)
            if (api->table_write(L3G_TABLE, lane * L3G_POINTS * 2u, curve,
                                 L3G_POINTS * 2u) != 0)
                return -NLR_API_EIO;
        return l3g_state(api, curve) == L3G_STATE_CURVE ? 0 : -NLR_API_EIO;
    }
    volatile uint16_t *ram = ram_rows();
    for (uint32_t k = 0; k < ROW_VALUES; k++)
        ram[k] = curve[k % L3G_POINTS];
    publish();
    return l3g_state(api, curve) == L3G_STATE_CURVE ? 0 : -NLR_API_EIO;
}

/* Grid levels 384 and 416 are points 40 and 41; 400 is halfway. */
uint32_t l3g_grey(const uint16_t *curve)
{
    return ((uint32_t)curve[40] + curve[41] + 32u) / 64u;
}

/* ---- files --------------------------------------------------------------- */

/*
 * No heap. On this body a small block from mem_alloc comes back to the
 * front of the allocator's free list as an 8 KiB node (one page and the
 * guard page), and while it is there NLoader cannot start another program
 * for the rest of the boot ("refused at stage A"). Files are read in
 * pieces through a buffer on the stack instead (the worker's stack is
 * 32 KiB).
 */
#define PIECE 256u

/* Open `path` for reading and learn its size: the file service cannot say
 * end-of-file, so a read past the end is an error, not 0. */
static int open_sized(const struct nlr_api *api, const char *path,
                      uint32_t *size)
{
    int fd = api->fs_open(path, NLR_OPEN_READ);
    if (fd < 0)
        return fd;
    int end = api->fs_seek(fd, 0, NLR_SEEK_END);
    int at = end > 0 ? api->fs_seek(fd, 0, NLR_SEEK_SET) : end;
    if (end <= 0 || at != 0) {
        api->fs_close(fd);
        return end == 0 ? -NLR_API_ENOENT : -NLR_API_EIO;
    }
    *size = (uint32_t)end;
    return fd;
}

/* The next piece of an open file: bytes read (> 0) or a negative error. */
static int read_piece(const struct nlr_api *api, int fd, uint8_t *buf,
                      uint32_t left)
{
    uint32_t want = left < PIECE ? left : PIECE, done = 0;
    while (done < want) {
        int got = api->fs_read(fd, buf + done, want - done);
        if (got <= 0)
            return got < 0 ? got : -NLR_API_EIO;
        done += (uint32_t)got;
    }
    return (int)done;
}

int l3g_write_file(const struct nlr_api *api, const char *path,
                   const char *text, uint32_t len)
{
    int fd = api->fs_open(path, NLR_OPEN_WRITE);
    if (fd < 0)
        return fd;
    int result = 0;
    for (uint32_t done = 0; done < len;) {
        int put = api->fs_write(fd, text + done, len - done);
        if (put <= 0) {
            result = put < 0 ? put : -NLR_API_EIO;
            break;
        }
        done += (uint32_t)put;
    }
    if (api->fs_close(fd) != 0 && result == 0)
        result = -NLR_API_EIO;
    return result;
}

/* The curve file: '#' comment lines, blank lines, and 229 lines of one
 * decimal number each (0..32767), never falling. CR LF and tabs are fine.
 * Fed a piece at a time. */
struct parse {
    uint16_t *curve;
    struct l3g_text *why;
    uint32_t count, line, value;
    enum { LINE, NUMBER, AFTER, COMMENT, BLANK } mode;
    int failed;
};

static void fail(struct parse *p, const char *what)
{
    l3g_clear(p->why);
    l3g_put(p->why, "line ");
    l3g_put_int(p->why, (int32_t)p->line);
    l3g_put(p->why, ": ");
    l3g_put(p->why, what);
    p->failed = 1;
}

static void number_done(struct parse *p)
{
    if (p->count == L3G_POINTS)
        fail(p, "more than 229 values");
    else if (p->count && p->value < p->curve[p->count - 1])
        fail(p, "the curve falls");
    else
        p->curve[p->count++] = (uint16_t)p->value;
}

static void feed(struct parse *p, const uint8_t *b, uint32_t n)
{
    for (uint32_t i = 0; i < n && !p->failed; i++) {
        uint8_t c = b[i];
        int blank = c == ' ' || c == '\t', digit = c >= '0' && c <= '9';
        switch (p->mode) {
        case LINE:
            if (c == '#')
                p->mode = COMMENT;
            else if (digit) {
                p->mode = NUMBER;
                p->value = (uint32_t)(c - '0');
            } else if (c == '\r')
                p->mode = BLANK;
            else if (c == '\n')
                p->line++;
            else if (!blank)
                fail(p, "not a number");
            break;
        case NUMBER:
            if (digit) {
                p->value = p->value * 10u + (uint32_t)(c - '0');
                if (p->value > L3G_VALUE_MAX)
                    fail(p, "above 32767");
            } else if (blank || c == '\r' || c == '\n') {
                number_done(p);
                p->mode = c == '\n' ? LINE : AFTER;
                p->line += c == '\n';
            } else {
                fail(p, "not one number");
            }
            break;
        case AFTER:
            if (c == '\n') {
                p->mode = LINE;
                p->line++;
            } else if (!blank && c != '\r') {
                fail(p, "not one number");
            }
            break;
        case COMMENT:
            if (c == '\n') {
                p->mode = LINE;
                p->line++;
            }
            break;
        case BLANK:
            if (c == '\n') {
                p->mode = LINE;
                p->line++;
            } else if (c != '\r') {
                fail(p, "not a number");
            }
            break;
        }
    }
}

static char upper(char ch)
{
    return ch >= 'a' && ch <= 'z' ? (char)(ch - 'a' + 'A') : ch;
}

int l3g_curve_name_ok(const char *name)
{
    uint32_t n = 0;
    for (; name && name[n]; n++) {
        char ch = name[n];
        if (n >= L3G_NAME_MAX || ch < 0x21 || ch > 0x7e || ch == '/' ||
            ch == '\\' || ch == ':')
            return 0;
    }
    if (n < 5 || name[0] == '.')
        return 0;
    return name[n - 4] == '.' && upper(name[n - 3]) == 'C' &&
           upper(name[n - 2]) == 'R' && upper(name[n - 1]) == 'V';
}

int l3g_load_curve(const struct nlr_api *api, const char *name,
                   uint16_t *curve, struct l3g_text *why)
{
    l3g_clear(why);
    if (!l3g_curve_name_ok(name)) {
        l3g_put(why, "not a curve file name");
        return -NLR_API_EINVAL;
    }
    struct l3g_text path;
    l3g_clear(&path);
    l3g_put(&path, L3G_DIR "/");
    l3g_put(&path, name);
    uint32_t size = 0;
    int fd = open_sized(api, path.s, &size);
    if (fd >= 0 && size > L3G_CURVE_BYTES_MAX) {
        api->fs_close(fd);
        fd = -NLR_API_EOVERFLOW;
    }
    if (fd < 0) {
        l3g_put(why, fd == -NLR_API_EOVERFLOW ? "file too large"
                     : fd == -NLR_API_ENOENT  ? "empty file"
                                              : "cannot read (");
        if (fd != -NLR_API_EOVERFLOW && fd != -NLR_API_ENOENT) {
            l3g_put_int(why, fd);
            l3g_put(why, ")");
        }
        return fd;
    }
    struct parse p = {curve, why, 0, 1, 0, LINE, 0};
    uint8_t piece[PIECE];
    int result = 0;
    for (uint32_t left = size; left && !p.failed;) {
        int got = read_piece(api, fd, piece, left);
        if (got < 0) {
            result = got;
            l3g_put(why, "read failed (");
            l3g_put_int(why, got);
            l3g_put(why, ")");
            break;
        }
        feed(&p, piece, (uint32_t)got);
        left -= (uint32_t)got;
    }
    if (api->fs_close(fd) != 0 && result == 0) {
        result = -NLR_API_EIO;
        l3g_put(why, "close failed");
    }
    if (result < 0)
        return result;
    if (!p.failed && p.mode == NUMBER) /* a last line without its newline */
        number_done(&p);
    if (p.failed)
        return -NLR_API_EINVAL;
    if (p.count != L3G_POINTS) {
        l3g_put_int(why, (int32_t)p.count);
        l3g_put(why, " values, 229 needed");
        return -NLR_API_EINVAL;
    }
    return 0;
}

/* key=value lines. `enable` takes 0 or 1, `curve` a curve file name; an
 * unknown key or a value that does not parse keeps its default. Returns
 * the number of keys read, or a negative error. Only the first PIECE
 * bytes are looked at; the page writes far fewer. */
static int config_parse(const struct nlr_api *api, struct l3g_config *c)
{
    c->enabled = 0;
    struct l3g_text name;
    l3g_clear(&name);
    l3g_put(&name, L3G_CURVE_NAME);
    for (uint32_t k = 0; k <= name.n; k++)
        c->curve[k] = name.s[k];
    uint32_t size = 0;
    int fd = open_sized(api, L3G_CONFIG_PATH, &size);
    if (fd < 0)
        return fd;
    uint8_t b[PIECE];
    int n = read_piece(api, fd, b, size);
    if (api->fs_close(fd) != 0 && n >= 0)
        n = -NLR_API_EIO;
    if (n < 0)
        return n;
    int keys = 0;
    for (uint32_t at = 0; at < (uint32_t)n;) {
        uint32_t end = at;
        while (end < (uint32_t)n && b[end] != '\n' && b[end] != '\r')
            end++;
        const char *line = (const char *)b + at;
        uint32_t len = end - at;
        if (len == 8 && line[0] == 'e' && line[1] == 'n' && line[2] == 'a' &&
            line[3] == 'b' && line[4] == 'l' && line[5] == 'e' &&
            line[6] == '=' && (line[7] == '0' || line[7] == '1')) {
            c->enabled = line[7] - '0';
            keys++;
        } else if (len > 6 && len - 6 <= L3G_NAME_MAX && line[0] == 'c' &&
                   line[1] == 'u' && line[2] == 'r' && line[3] == 'v' &&
                   line[4] == 'e' && line[5] == '=') {
            char candidate[L3G_NAME_MAX + 1];
            for (uint32_t k = 0; k < len - 6; k++)
                candidate[k] = line[6 + k];
            candidate[len - 6] = 0;
            if (l3g_curve_name_ok(candidate)) {
                for (uint32_t k = 0; k <= len - 6; k++)
                    c->curve[k] = candidate[k];
                keys++;
            }
        }
        at = end;
        while (at < (uint32_t)n && (b[at] == '\n' || b[at] == '\r'))
            at++;
    }
    return keys;
}

/* The firmware's open cannot tell a missing file from a failed read (both
 * come back -NLR_API_EIO), so anything unreadable is the defaults: the
 * camera then simply keeps its own N-Log. */
void l3g_config_read(const struct nlr_api *api, struct l3g_config *c)
{
    (void)config_parse(api, c);
}

int l3g_config_save(const struct nlr_api *api, const struct l3g_config *c)
{
    if (!l3g_curve_name_ok(c->curve))
        return -NLR_API_EINVAL;
    struct l3g_text text;
    l3g_clear(&text);
    l3g_put(&text, "# Log3G10 page: enable=1 loads the curve into N-Log at start\n");
    l3g_put(&text, c->enabled ? "enable=1\ncurve=" : "enable=0\ncurve=");
    l3g_put(&text, c->curve);
    l3g_put(&text, "\n");
    int result = l3g_write_file(api, L3G_CONFIG_PATH, text.s, text.n);
    if (result < 0)
        return result;
    /* Only what reads back counts as saved. */
    struct l3g_config back;
    if (config_parse(api, &back) != 2 || back.enabled != (c->enabled ? 1 : 0))
        return -NLR_API_EIO;
    for (uint32_t k = 0; k <= L3G_NAME_MAX; k++) {
        if (back.curve[k] != c->curve[k])
            return -NLR_API_EIO;
        if (!c->curve[k])
            break;
    }
    return 0;
}
