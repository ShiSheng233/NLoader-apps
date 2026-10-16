/*
 * Host test for the Log3G10 programs: the curve parser, the settings file,
 * the start-up extension's decisions and the page (the camera's row kinds,
 * the curve choice and the text fallback), against an in-memory card and
 * an array standing in for the three Lut1 rows.
 *
 *   cc -O1 -Wall -I <sdk> -I . -DL3G_HOSTED \
 *      -DL3G_RAM_ROWS='(uintptr_t)({extern uint16_t test_ram[]; test_ram;})' \
 *      test_log3g10.c log3g10.c page.c start.c -o test
 *   ./test LOG3G10.CRV <stock rows: 1374 bytes from ROM 0x453d573c>
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "log3g10.h"

uint16_t test_ram[687];
static uint16_t test_rom[687];   /* the stock rows, for comparing */

static int failed, checks;
#define CHECK(c, ...) do { checks++; if (!(c)) { failed++; \
    printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); \
    printf("\n"); } } while (0)

/* ---- an in-memory card ---------------------------------------------------- */

#define FILES 16
struct file { char path[80]; char *data; uint32_t size; int used; };
static struct file files[FILES];
struct handle { int file, pos, used; };
static struct handle handles[8];
static int write_fails;

static struct file *find(const char *path)
{
    for (int i = 0; i < FILES; i++)
        if (files[i].used && !strcmp(files[i].path, path))
            return &files[i];
    return 0;
}

static void put_file(const char *path, const char *data, uint32_t size)
{
    struct file *f = find(path);
    for (int i = 0; !f && i < FILES; i++)
        if (!files[i].used)
            f = &files[i];
    free(f->data);
    snprintf(f->path, sizeof f->path, "%s", path);
    f->data = malloc(size + 1);
    memcpy(f->data, data, size);
    f->size = size;
    f->used = 1;
}

static void drop_file(const char *path)
{
    struct file *f = find(path);
    if (f)
        f->used = 0;
}

static int t_open(const char *path, uint32_t mode)
{
    struct file *f = find(path);
    if (mode == NLR_OPEN_WRITE) {
        put_file(path, "", 0);
        f = find(path);
    } else if (!f) {
        return -NLR_API_EIO;          /* what the firmware's open answers */
    }
    for (int h = 0; h < 8; h++)
        if (!handles[h].used) {
            handles[h] = (struct handle){(int)(f - files), 0, 1};
            return h;
        }
    return -NLR_API_EBUSY;
}

static int t_read(int fd, void *buf, uint32_t len)
{
    struct handle *h = &handles[fd];
    struct file *f = &files[h->file];
    uint32_t left = f->size - (uint32_t)h->pos;
    if (len > 100)
        len = 100;                    /* short reads, like the card's */
    if (len > left)
        len = left;
    if (!len)
        return -NLR_API_EIO;
    memcpy(buf, f->data + h->pos, len);
    h->pos += (int)len;
    return (int)len;
}

static int t_seek(int fd, int64_t off, int whence)
{
    struct handle *h = &handles[fd];
    h->pos = whence == NLR_SEEK_END ? (int)files[h->file].size + (int)off : (int)off;
    return h->pos;
}

static int t_close(int fd)
{
    handles[fd].used = 0;
    return 0;
}

static int t_write(int fd, const void *buf, uint32_t len)
{
    struct handle *h = &handles[fd];
    struct file *f = &files[h->file];
    if (write_fails)
        return -NLR_API_EIO;
    if (len > 7)
        len = 7;                      /* short writes too */
    f->data = realloc(f->data, (size_t)h->pos + len + 1);
    memcpy(f->data + h->pos, buf, len);
    h->pos += (int)len;
    if ((uint32_t)h->pos > f->size)
        f->size = (uint32_t)h->pos;
    return (int)len;
}

/* The directory listing: files of `path`/ in the order they were made. */
static int t_dir(const char *path, uint32_t index, struct nlr_dirent *out)
{
    size_t n = strlen(path);
    for (int i = 0; i < FILES; i++) {
        if (!files[i].used || strncmp(files[i].path, path, n) || files[i].path[n] != '/')
            continue;
        if (index--)
            continue;
        memset(out, 0, sizeof *out);
        snprintf(out->name, sizeof out->name, "%s", files[i].path + n + 1);
        out->size = files[i].size;
        return 0;
    }
    return -NLR_API_ENOENT;
}

/* The programs must never touch the heap (log3g10.c says why). */
static int heap_calls;
static void *t_alloc(uint32_t size, uint32_t attrs) { (void)size; (void)attrs; heap_calls++; return 0; }
static void t_free(void *p) { (void)p; heap_calls++; }
static char drawn[160];
static int t_draw(int x, int y, const char *s) { (void)x; (void)y; snprintf(drawn, sizeof drawn, "%s", s); return 0; }
static void t_log(const char *fmt, ...) { (void)fmt; }
static uint32_t t_time(void) { return 1234; }

/* The pages: each call records what was shown and answers from a script. */
#define SHOWS 24
static const int32_t *script;
struct shown {
    int rows;                       /* 1 ui_rows, 0 ui_list */
    char title[64];
    uint32_t count, selected;
    char label[6][64];
    uint32_t kind[6], flags[6];
    char value[6][64];
};
static struct shown shown[SHOWS];
static int shows, rows_enosys;

static int32_t t_list(const char *title, const char *const *rows, uint32_t count, uint32_t sel)
{
    struct shown *s = &shown[shows < SHOWS ? shows : SHOWS - 1];
    memset(s, 0, sizeof *s);
    snprintf(s->title, sizeof s->title, "%s", title);
    s->count = count;
    s->selected = sel;
    for (uint32_t i = 0; i < count && i < 6; i++)
        snprintf(s->label[i], 64, "%s", rows[i]);
    shows++;
    return *script++;
}

static int32_t t_rows(const char *title, const struct nlr_row *rows, uint32_t count, uint32_t sel)
{
    if (rows_enosys)
        return -NLR_API_ENOSYS;
    struct shown *s = &shown[shows < SHOWS ? shows : SHOWS - 1];
    memset(s, 0, sizeof *s);
    s->rows = 1;
    snprintf(s->title, sizeof s->title, "%s", title);
    s->count = count;
    s->selected = sel;
    for (uint32_t i = 0; i < count && i < 6; i++) {
        snprintf(s->label[i], 64, "%s", rows[i].label);
        s->kind[i] = rows[i].kind;
        s->flags[i] = rows[i].flags;
        snprintf(s->value[i], 64, "%s", rows[i].value ? rows[i].value : "");
    }
    shows++;
    return *script++;
}

static struct nlr_api api = {
    .abi = 6, .mem_alloc = t_alloc, .mem_free = t_free, .fs_open = t_open,
    .fs_read = t_read, .fs_seek = t_seek, .fs_close = t_close,
    .ui_draw_text = t_draw, .log = t_log, .time_ms = t_time, .fs_dir = t_dir,
    .ui_list = t_list, .fs_write = t_write, .ui_rows = t_rows,
};

static char *slurp(const char *path, long *n)
{
    FILE *f = fopen(path, "rb");
    if (!f) { perror(path); exit(2); }
    fseek(f, 0, SEEK_END); *n = ftell(f); fseek(f, 0, SEEK_SET);
    char *b = malloc((size_t)*n + 1);
    if (fread(b, 1, (size_t)*n, f) != (size_t)*n) exit(2);
    fclose(f);
    b[*n] = 0;
    return b;
}

static int rows_are(const uint16_t *c)
{
    for (int k = 0; k < 687; k++)
        if (test_ram[k] != c[k % 229])
            return 0;
    return 1;
}

static const char *log_text(void)
{
    struct file *f = find(L3G_LOG_PATH);
    static char b[200];
    snprintf(b, sizeof b, "%.*s", f ? (int)f->size : 0, f ? f->data : "");
    return b;
}

static const char *config_text(void)
{
    struct file *f = find(L3G_CONFIG_PATH);
    static char b[200];
    snprintf(b, sizeof b, "%.*s", f ? (int)f->size : 0, f ? f->data : "");
    return b;
}

static int load_text(const char *text, struct l3g_text *why, uint16_t *c)
{
    put_file("APPS/LOG3G10.CRV", text, (uint32_t)strlen(text));
    return l3g_load_curve(&api, "LOG3G10.CRV", c, why);
}

static void stock(void)
{
    memcpy(test_ram, test_rom, sizeof test_ram);
}

static void set_config(int enabled, const char *curve)
{
    struct l3g_config c = {enabled, ""};
    snprintf(c.curve, sizeof c.curve, "%s", curve);
    CHECK(l3g_config_save(&api, &c) == 0, "save %d %s", enabled, curve);
}

int main(int argc, char **argv)
{
    if (argc != 3)
        return 2;
    long n, m;
    char *crv = slurp(argv[1], &n);
    char *rom = slurp(argv[2], &m);
    if (m != 1374) { printf("stock rows: %ld bytes\n", m); return 2; }
    memcpy(test_rom, rom, 1374);
    stock();

    /* The curve file as mkcurve.py writes it, and the same values parsed
     * by hand; a second curve to choose (every value halved). */
    uint16_t want[229], half[229], c[229];
    struct l3g_text why;
    int k = 0;
    for (char *p = crv; *p && k < 229; ) {
        if (*p == '#') { while (*p && *p != '\n') p++; }
        else if (*p >= '0' && *p <= '9') want[k++] = (uint16_t)strtol(p, &p, 10);
        if (*p) p++;
    }
    char alt[4096];
    int at = snprintf(alt, sizeof alt, "# another curve\n");
    for (int i = 0; i < 229; i++) {
        half[i] = (uint16_t)(want[i] / 2);
        at += snprintf(alt + at, sizeof alt - (size_t)at, "%u\n", half[i]);
    }
    put_file("APPS/LOG3G10.NLB", "nlb", 3);
    put_file("APPS/LOG3G10.CRV", crv, (uint32_t)n);
    put_file("APPS/NOTES.TXT", "notes", 5);
    put_file("APPS/ALT.CRV", alt, (uint32_t)at);
    CHECK(l3g_load_curve(&api, "LOG3G10.CRV", c, &why) == 0, "curve file: %s", why.s);
    CHECK(!memcmp(c, want, sizeof c), "curve values");
    CHECK(l3g_grey(c) == 341, "grey %u", l3g_grey(c));
    CHECK(l3g_state(0, 0) == L3G_STATE_STOCK, "stock rows not recognised");
    CHECK(l3g_state(0, want) == L3G_STATE_STOCK, "stock rows taken for the curve");

    /* Curve names: a plain .CRV name in APPS, nothing that leads out. */
    CHECK(l3g_curve_name_ok("LOG3G10.CRV") && l3g_curve_name_ok("my.crv"), "good names");
    CHECK(!l3g_curve_name_ok("../X.CRV") && !l3g_curve_name_ok("A\\B.CRV") &&
          !l3g_curve_name_ok("A:B.CRV") && !l3g_curve_name_ok("A.TXT") &&
          !l3g_curve_name_ok(".CRV") && !l3g_curve_name_ok("B C.CRV") &&
          !l3g_curve_name_ok("ABCDEFGHIJKLMNOPQRSTUVWXYZABC.CRV"), "bad names");
    CHECK(l3g_load_curve(&api, "../X.CRV", c, &why) == -NLR_API_EINVAL &&
          !strcmp(why.s, "not a curve file name"), "refused name: %s", why.s);

    /* CR LF, tabs and blank lines are fine; mistakes say where. */
    char *crlf = malloc((size_t)n * 3);
    int j = 0;
    for (long i = 0; i < n; i++) {
        if (crv[i] == '\n') { crlf[j++] = '\t'; crlf[j++] = '\r'; crlf[j++] = '\n'; }
        else crlf[j++] = crv[i];
    }
    crlf[j] = 0;
    CHECK(load_text(crlf, &why, c) == 0 && !memcmp(c, want, sizeof c), "CRLF: %s", why.s);
    char *bad = malloc((size_t)n + 64);
    char *cut = strrchr(strcpy(bad, crv), '\n');
    *cut = 0; cut = strrchr(bad, '\n'); cut[1] = 0;          /* 228 values */
    CHECK(load_text(bad, &why, c) < 0 && !strcmp(why.s, "228 values, 229 needed"), "short: %s", why.s);
    strcpy(bad, crv); strcat(bad, "32767\n");
    CHECK(load_text(bad, &why, c) < 0 && !strcmp(why.s, "line 234: more than 229 values"), "long: %s", why.s);
    strcpy(bad, crv); memcpy(strstr(bad, "\n3086\n") + 1, "2999", 4);
    CHECK(load_text(bad, &why, c) < 0 && !strcmp(why.s, "line 6: the curve falls"), "falls: %s", why.s);
    strcpy(bad, crv); memcpy(strstr(bad, "\n3000\n") + 1, "40000\n", 5);
    CHECK(load_text(bad, &why, c) < 0 && !strncmp(why.s, "line 5: above", 13), "big: %s", why.s);
    strcpy(bad, crv); memcpy(strstr(bad, "\n3000\n") + 1, "30x0", 4);
    CHECK(load_text(bad, &why, c) < 0 && !strcmp(why.s, "line 5: not one number"), "junk: %s", why.s);
    strcpy(bad, crv); bad[strlen(bad) - 1] = 0;            /* no final newline */
    CHECK(load_text(bad, &why, c) == 0 && !memcmp(c, want, sizeof c), "no newline: %s", why.s);
    drop_file("APPS/LOG3G10.CRV");
    CHECK(l3g_load_curve(&api, "LOG3G10.CRV", c, &why) < 0 &&
          !strncmp(why.s, "cannot read", 11), "missing: %s", why.s);
    put_file("APPS/LOG3G10.CRV", crv, (uint32_t)n);

    /* The settings: defaults without a file or for lines that do not parse;
     * what is saved reads back. */
    struct l3g_config cfg;
    l3g_config_read(&api, &cfg);
    CHECK(!cfg.enabled && !strcmp(cfg.curve, L3G_CURVE_NAME), "defaults");
    put_file(L3G_CONFIG_PATH, "enable=2\ncurve=../X.CRV\nother=1\n", 32);
    l3g_config_read(&api, &cfg);
    CHECK(!cfg.enabled && !strcmp(cfg.curve, L3G_CURVE_NAME), "bad lines keep defaults");
    set_config(1, "ALT.CRV");
    l3g_config_read(&api, &cfg);
    CHECK(cfg.enabled && !strcmp(cfg.curve, "ALT.CRV"), "saved: %s", config_text());
    struct l3g_config wrong = {1, "../X.CRV"};
    CHECK(l3g_config_save(&api, &wrong) == -NLR_API_EINVAL, "bad name saved");
    drop_file(L3G_CONFIG_PATH);

    /* The extension: off without a settings file. */
    CHECK(nlr_extension_init(&api) == 0 && rows_are(test_rom), "off changed rows");
    CHECK(strstr(log_text(), "ms: off\n") != 0, "log: %s", log_text());
    /* On: applied to all three lanes, then a second start finds it done. */
    set_config(1, "LOG3G10.CRV");
    CHECK(nlr_extension_init(&api) == 0 && rows_are(want), "not applied");
    CHECK(strstr(log_text(), "applied LOG3G10.CRV, grey 341") != 0, "log: %s", log_text());
    CHECK(!strcmp(drawn, "Log3G10: applied LOG3G10.CRV, grey 341"), "drawn: %s", drawn);
    CHECK(nlr_extension_init(&api) == 0 && strstr(log_text(), "already applied"), "again: %s", log_text());
    /* The chosen curve is the one loaded. */
    stock();
    set_config(1, "ALT.CRV");
    CHECK(nlr_extension_init(&api) == 0 && rows_are(half) &&
          strstr(log_text(), "applied ALT.CRV"), "alt: %s", log_text());
    /* Rows that are neither stock nor this curve are left alone. */
    stock();
    test_ram[300] ^= 1;
    CHECK(nlr_extension_init(&api) == 0 && test_ram[0] == test_rom[0] &&
          strstr(log_text(), "left alone"), "other: %s", log_text());
    stock();
    /* A broken curve file keeps N-Log and says why. */
    set_config(1, "LOG3G10.CRV");
    strcpy(bad, crv); memcpy(strstr(bad, "\n3000\n") + 1, "30x0", 4);
    put_file("APPS/LOG3G10.CRV", bad, (uint32_t)strlen(bad));
    CHECK(nlr_extension_init(&api) == 0 && rows_are(test_rom) &&
          strstr(log_text(), "LOG3G10.CRV line 5: not one number, N-Log unchanged"),
          "broken: %s", log_text());
    put_file("APPS/LOG3G10.CRV", crv, (uint32_t)n);

    /* The page, with the camera's row kinds: the switch, the curve with its
     * value, two text rows. OK on the switch flips it in place (saved and,
     * on stock rows, applied at once); off waits for the next power-on. */
    stock();
    set_config(0, "LOG3G10.CRV");
    static const int32_t flip[] = {0, 0, NLR_LIST_BACK};
    script = flip; shows = 0;
    CHECK(nlr_app_main(&api) == 0 && shows == 3, "flip: %d shows", shows);
    struct shown *s = &shown[0];
    CHECK(s->rows && !strcmp(s->title, "Log3G10") && s->count == 4, "first: %d %u", s->rows, s->count);
    CHECK(s->kind[0] == NLR_ROW_SWITCH && !s->flags[0] && !strcmp(s->label[0], "Log3G10 at start"),
          "switch: %u %u %s", s->kind[0], s->flags[0], s->label[0]);
    CHECK(s->kind[1] == NLR_ROW_SELECT && !strcmp(s->label[1], "Curve") && !strcmp(s->value[1], "LOG3G10"),
          "curve row: %s = %s", s->label[1], s->value[1]);
    CHECK(s->kind[2] == NLR_ROW_TEXT && !strcmp(s->label[2], "Now: stock N-Log") &&
          s->kind[3] == NLR_ROW_TEXT && !strcmp(s->label[3], "grey 341, top 841"),
          "text rows: %s | %s", s->label[2], s->label[3]);
    s = &shown[1];
    CHECK(s->flags[0] == NLR_ROW_ON && !strcmp(s->label[2], "Now: this curve") &&
          s->count == 5 && s->kind[4] == NLR_ROW_TEXT && !strcmp(s->label[4], "Log3G10 applied"),
          "on: %u %s %s", s->flags[0], s->label[2], s->label[4]);
    s = &shown[2];
    CHECK(!s->flags[0] && !strcmp(s->label[4], "Off at next power-on"), "off: %s", s->label[4]);
    CHECK(rows_are(want) && strstr(config_text(), "enable=0\ncurve=LOG3G10.CRV\n"), "flip end: %s", config_text());

    /* The curve row opens the camera's choice page of the .CRV files in
     * APPS, the cursor on the chosen one; another choice is saved and, with
     * the switch on and the rows stock, applied. */
    stock();
    set_config(1, "LOG3G10.CRV");
    static const int32_t pick[] = {1, 1, NLR_LIST_BACK};
    script = pick; shows = 0;
    CHECK(nlr_app_main(&api) == 0 && shows == 3, "pick: %d shows", shows);
    s = &shown[1];
    CHECK(!s->rows && !strcmp(s->title, "Curve") && s->count == 2 &&
          !strcmp(s->label[0], "LOG3G10") && !strcmp(s->label[1], "ALT") && s->selected == 0,
          "choices: %s [%s|%s] sel %u", s->title, s->label[0], s->label[1], s->selected);
    s = &shown[2];
    CHECK(!strcmp(s->value[1], "ALT") && !strcmp(s->label[4], "Log3G10 applied") && rows_are(half),
          "picked: %s %s", s->value[1], s->label[4]);
    CHECK(strstr(config_text(), "enable=1\ncurve=ALT.CRV\n") != 0, "saved: %s", config_text());
    /* Back on the choice page, or the same choice, changes nothing. */
    char before[200];
    snprintf(before, sizeof before, "%s", config_text());
    static const int32_t keep[] = {1, NLR_LIST_BACK, 1, 1, NLR_LIST_BACK};
    script = keep; shows = 0;
    CHECK(nlr_app_main(&api) == 0 && shows == 5 && !shown[2].label[4][0] && !shown[4].label[4][0],
          "keep: %d shows", shows);
    CHECK(!strcmp(before, config_text()) && shown[3].selected == 1, "kept: %s", config_text());

    /* A loader without row kinds (ABI 5): the same as text rows. */
    stock();
    set_config(0, "LOG3G10.CRV");
    api.abi = 5;
    static const int32_t text[] = {0, NLR_LIST_BACK};
    script = text; shows = 0;
    CHECK(nlr_app_main(&api) == 0 && shows == 2, "text: %d", shows);
    CHECK(!shown[0].rows && !strcmp(shown[0].label[0], "Log3G10 at start: Off") &&
          !strcmp(shown[0].label[1], "Curve: LOG3G10") && !strcmp(shown[0].label[2], "Now: stock N-Log"),
          "text rows: %s | %s", shown[0].label[0], shown[0].label[1]);
    CHECK(!strcmp(shown[1].label[0], "Log3G10 at start: On") && rows_are(want), "text on: %s", shown[1].label[0]);
    api.abi = 6;
    /* A page that cannot draw row kinds answers ENOSYS: text rows too. */
    rows_enosys = 1;
    static const int32_t back[] = {NLR_LIST_BACK};
    script = back; shows = 0;
    CHECK(nlr_app_main(&api) == 0 && shows == 1 && !shown[0].rows &&
          !strcmp(shown[0].label[0], "Log3G10 at start: On"), "enosys: %s", shown[0].label[0]);
    rows_enosys = 0;

    /* Settings that cannot be saved: the open already emptied the file, so
     * the card now says off and LOG3G10.CRV, and the page shows that. */
    write_fails = 1;
    static const int32_t unsaved[] = {0, NLR_LIST_BACK};
    script = unsaved; shows = 0;
    CHECK(nlr_app_main(&api) == 0 && !strcmp(shown[1].label[4], "Not saved (-5)") &&
          !shown[1].flags[0], "unsaved: %s", shown[1].label[4]);
    write_fails = 0;

    /* A newer loader's table: the extension stands down without failing
     * (a failure would pause every later start), the page refuses. */
    stock();
    api.abi = NLR_ABI_VERSION + 1u;
    char newer[64];
    snprintf(newer, sizeof newer, "loader is newer (ABI %u), rebuild", NLR_ABI_VERSION + 1u);
    CHECK(nlr_extension_init(&api) == 0 && rows_are(test_rom) &&
          strstr(log_text(), newer), "newer: %s", log_text());
    CHECK(nlr_app_main(&api) == 1, "page on a newer table");
    api.abi = 4;
    CHECK(nlr_app_main(&api) == 1, "page on a table without fs_write's ABI");
    api.abi = 6;

    CHECK(heap_calls == 0, "heap used %d times", heap_calls);
    printf("%d checks, %d failed\n", checks, failed);
    return failed != 0;
}
