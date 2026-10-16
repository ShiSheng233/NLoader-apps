/*
 * Log3G10 settings page (foreground program, ABI 5; ABI 6 for the camera's
 * own row kinds).
 *
 * On the camera's own menu page, drawn the way its own settings are
 * (ui_rows, ABI 6):
 *
 *   Log3G10 at start      (OFF)   the camera's toggle switch: OK flips it,
 *                                 saves it in LOG3G10.CFG and applies it
 *   Curve               LOG3G10 > the curve file: OK opens the camera's
 *                                 choice page of the .CRV files in APPS
 *   Now: stock N-Log              what the camera's N-Log rows hold now
 *   grey 341, top 841             the chosen curve, or why it is unusable
 *
 * A loader without row kinds (ABI 5, or a page that cannot draw them)
 * shows the same as text rows, and OK on "Log3G10 at start: Off" flips it.
 *
 * On: the start-up extension (start.c) loads the chosen curve into tone
 * mode N-Log each time NLoader starts, and this page loads it right away
 * (the camera uses it from the next video job it builds; leaving the menu
 * is enough). Off: the next power-on keeps the firmware's own N-Log -- the
 * firmware reloads its rows itself at power-on, and this program never
 * writes anything but a curve over stock rows.
 *
 * No writable static data (a program with none is given one memory region
 * instead of two) and no heap (log3g10.c says why): everything lives in
 * structures on the worker's stack.
 */
#include "log3g10.h"

#define ROWS        5u   /* switch, curve, now, curve details, last */
#define CHOICES_MAX 16u  /* curve files the choice page lists */

struct page {
    const struct nlr_api *api;
    int native;                /* the page draws row kinds (ABI 6) */
    struct l3g_config config;  /* the saved switch and curve */
    int curve_ok;              /* the chosen curve read and well formed */
    uint16_t curve[L3G_POINTS];
    struct l3g_text why;       /* why not, when it is not */
    struct l3g_text last;      /* the outcome of the last OK, or empty */
    struct l3g_text text[ROWS];
    struct l3g_text value;     /* the curve row's value */
    struct nlr_row rows[ROWS];
    const char *labels[ROWS];
};

/* The curve's name as the row shows it: without .CRV. */
static void curve_shown(struct l3g_text *t, const char *name)
{
    uint32_t n = 0;
    while (name[n])
        n++;
    for (uint32_t k = 0; k + 4u < n && t->n + 1u < L3G_TEXT_MAX; k++)
        t->s[t->n++] = name[k];
    t->s[t->n] = 0;
}

static void now_text(struct l3g_text *t, const struct page *p)
{
    l3g_put(t, "Now: ");
    switch (l3g_state(p->api, p->curve_ok ? p->curve : 0)) {
    case L3G_STATE_STOCK: l3g_put(t, "stock N-Log"); break;
    case L3G_STATE_CURVE: l3g_put(t, "this curve"); break;
    default: l3g_put(t, "not the stock N-Log"); break;
    }
}

static void details_text(struct l3g_text *t, const struct page *p)
{
    if (!p->curve_ok) {
        l3g_put(t, p->why.s);
        return;
    }
    l3g_put(t, "grey ");
    l3g_put_int(t, (int32_t)l3g_grey(p->curve));
    l3g_put(t, ", top ");
    l3g_put_int(t, (int32_t)((p->curve[L3G_POINTS - 1] + 16u) / 32u));
}

/* The rows, as row kinds or as text. Row 0 is the switch, row 1 the
 * curve; the rest only say something. */
static uint32_t build(struct page *p)
{
    for (uint32_t i = 0; i < ROWS; i++) {
        l3g_clear(&p->text[i]);
        p->rows[i].kind = NLR_ROW_TEXT;
        p->rows[i].flags = 0;
        p->rows[i].value = 0;
        p->rows[i].label = p->text[i].s;
        p->labels[i] = p->text[i].s;
    }
    l3g_clear(&p->value);
    if (p->native) {
        l3g_put(&p->text[0], "Log3G10 at start");
        p->rows[0].kind = NLR_ROW_SWITCH;
        p->rows[0].flags = p->config.enabled ? NLR_ROW_ON : 0;
        l3g_put(&p->text[1], "Curve");
        p->rows[1].kind = NLR_ROW_SELECT;
        curve_shown(&p->value, p->config.curve);
        p->rows[1].value = p->value.s;
    } else {
        l3g_put(&p->text[0], p->config.enabled ? "Log3G10 at start: On"
                                               : "Log3G10 at start: Off");
        l3g_put(&p->text[1], "Curve: ");
        curve_shown(&p->text[1], p->config.curve);
    }
    now_text(&p->text[2], p);
    details_text(&p->text[3], p);
    uint32_t n = 4;
    if (p->last.n) {
        l3g_put(&p->text[4], p->last.s);
        n = 5;
    }
    return n;
}

static int32_t show_page(struct page *p, uint32_t selected)
{
    for (;;) {
        uint32_t n = build(p);
        if (!p->native)
            return p->api->ui_list("Log3G10", p->labels, n, selected);
        int32_t r = p->api->ui_rows("Log3G10", p->rows, n, selected);
        if (r != -NLR_API_ENOSYS)
            return r;
        p->native = 0;           /* this page cannot draw row kinds after all */
    }
}

static void reload(struct page *p)
{
    p->curve_ok = l3g_load_curve(p->api, p->config.curve, p->curve, &p->why) == 0;
}

/* After the settings were saved: what can happen now, and what has to
 * wait for the next power-on. */
static void apply_now(struct page *p)
{
    l3g_clear(&p->last);
    enum l3g_state s = l3g_state(p->api, p->curve_ok ? p->curve : 0);
    if (!p->config.enabled) {
        l3g_put(&p->last, s == L3G_STATE_STOCK ? "Saved: off"
                                               : "Off at next power-on");
        return;
    }
    if (!p->curve_ok)
        l3g_put(&p->last, "Curve file not usable");
    else if (s == L3G_STATE_CURVE)
        l3g_put(&p->last, "Saved: on");
    else if (s == L3G_STATE_OTHER)
        l3g_put(&p->last, "Loads at next power-on");
    else
        l3g_put(&p->last, l3g_write(p->api, p->curve) == 0 ? "Log3G10 applied"
                                                   : "Write failed: restart");
}

/* Save what changed, then bring the rows in line with it. */
static void save(struct page *p, const struct l3g_config *want)
{
    int saved = l3g_config_save(p->api, want);
    /* A failed save may have emptied the file (it is truncated on open),
     * so the settings are whatever the card says now, and the rows follow
     * that. */
    if (saved == 0)
        p->config = *want;
    else
        l3g_config_read(p->api, &p->config);
    reload(p);
    apply_now(p);
    if (saved != 0) {
        l3g_clear(&p->last);
        l3g_put(&p->last, "Not saved (");
        l3g_put_int(&p->last, saved);
        l3g_put(&p->last, ")");
    }
}

static int same_name(const char *a, const char *b)
{
    uint32_t k = 0;
    while (a[k] && a[k] == b[k])
        k++;
    return a[k] == b[k];
}

/* The camera's choice page of the curve files in APPS, the cursor on the
 * chosen one: 1 and the new name in `want` when another was chosen. */
static int choose_curve(struct page *p, struct l3g_config *want)
{
    const struct nlr_api *api = p->api;
    struct l3g_text names[CHOICES_MAX], label[CHOICES_MAX];
    const char *shown[CHOICES_MAX];
    uint32_t n = 0, current = 0;
    struct nlr_dirent e;
    for (uint32_t i = 0; n < CHOICES_MAX && api->fs_dir &&
                         api->fs_dir(L3G_DIR, i, &e) == 0; i++) {
        if ((e.flags & NLR_DIRENT_DIR) || !l3g_curve_name_ok(e.name))
            continue;
        l3g_clear(&names[n]);
        l3g_put(&names[n], e.name);
        l3g_clear(&label[n]);
        curve_shown(&label[n], e.name);
        shown[n] = label[n].s;
        if (same_name(e.name, p->config.curve))
            current = n;
        n++;
    }
    if (!n) {
        l3g_clear(&p->last);
        l3g_put(&p->last, "No .CRV file in APPS");
        return 0;
    }
    int32_t r = api->ui_list("Curve", shown, n, current);
    if (r < 0 || (uint32_t)r >= n || same_name(names[r].s, p->config.curve))
        return 0;
    *want = p->config;
    for (uint32_t k = 0; k <= names[r].n && k <= L3G_NAME_MAX; k++)
        want->curve[k] = names[r].s[k];
    return 1;
}

static int32_t run(struct page *p)
{
    l3g_config_read(p->api, &p->config);
    reload(p);
    uint32_t selected = 0;
    for (;;) {
        int32_t r = show_page(p, selected);
        if (r == NLR_LIST_BACK)
            return 0;
        if (r < 0)
            return 2;
        selected = (uint32_t)r < 2u ? (uint32_t)r : 0;
        l3g_config_read(p->api, &p->config);
        reload(p);
        l3g_clear(&p->last);
        struct l3g_config want = p->config;
        if (r == 0) {
            /* The switch flips in place, as the camera's own do. */
            want.enabled = !p->config.enabled;
            save(p, &want);
        } else if (r == 1 && choose_curve(p, &want)) {
            save(p, &want);
        }
    }
}

int32_t nlr_app_main(const struct nlr_api *api)
{
    /* fs_write is ABI 5; a table newer than this build is refused (the
     * SDK's rule), and the row kinds are ABI 6. */
    if (!api || api->abi < 5u || api->abi > NLR_ABI_VERSION ||
        !api->fs_write || !api->ui_list)
        return 1;
    struct page p;
    p.api = api;
    p.native = api->abi >= 6u && api->ui_rows;
    l3g_clear(&p.why);
    l3g_clear(&p.last);
    return run(&p);
}
