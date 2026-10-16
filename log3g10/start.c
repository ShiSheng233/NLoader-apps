/*
 * Log3G10 at start (resource extension, ABI 5).
 *
 * Runs once each time NLoader starts its extensions (on the Z 6III: the
 * first time the NLoader page is opened after power-on). When the
 * settings page's switch is on, it loads the chosen curve (LOG3G10.CRV
 * unless the page chose another) into the three Lut1 rows tone mode N-Log
 * uses, and records what it did in LOG3G10.LOG.
 *
 * It only ever writes over the firmware's own rows: anything else in them
 * is left alone. Everything that is not this program's fault -- the switch
 * off, no usable curve, rows it does not recognise -- returns 0 with the
 * camera unchanged, because a failed extension holds NLoader's
 * interruption marker and pauses the next start. Only a write that did
 * not read back (rows possibly half changed) reports failure.
 *
 * No writable static data (an extension's memory stays allocated for the
 * whole boot, and one without data is given one region instead of two) and
 * no heap (log3g10.c says why): the curve lives on the stack.
 */
#include "log3g10.h"

static int32_t finish(const struct nlr_api *api, int32_t result,
                      const struct l3g_text *what)
{
    struct l3g_text line;
    l3g_clear(&line);
    l3g_put(&line, "Log3G10: ");
    l3g_put(&line, what->s);
    api->ui_draw_text(0, 0, line.s);

    struct l3g_text log;
    l3g_clear(&log);
    l3g_put(&log, "start ");
    l3g_put_int(&log, (int32_t)api->time_ms());
    l3g_put(&log, " ms: ");
    l3g_put(&log, what->s);
    l3g_put(&log, "\n");
    /* The record is a courtesy; failing to write it changes nothing. */
    if (l3g_write_file(api, L3G_LOG_PATH, log.s, log.n) != 0)
        api->log("l3g: log not written");
    return result;
}

static int32_t start(const struct nlr_api *api, uint16_t *curve)
{
    struct l3g_text what;
    l3g_clear(&what);
    struct l3g_config config;
    l3g_config_read(api, &config);
    if (!config.enabled) {
        l3g_put(&what, "off");
        return finish(api, 0, &what);
    }
    struct l3g_text why;
    if (l3g_load_curve(api, config.curve, curve, &why) != 0) {
        l3g_put(&what, config.curve);
        l3g_put(&what, " ");
        l3g_put(&what, why.s);
        l3g_put(&what, ", N-Log unchanged");
        return finish(api, 0, &what);
    }
    switch (l3g_state(api, curve)) {
    case L3G_STATE_CURVE:
        l3g_put(&what, "already applied");
        return finish(api, 0, &what);
    case L3G_STATE_OTHER:
        l3g_put(&what, "rows are not the stock N-Log, left alone");
        return finish(api, 0, &what);
    case L3G_STATE_STOCK:
        break;
    }
    if (l3g_write(api, curve) != 0) {
        l3g_put(&what, "write did not read back");
        return finish(api, 7, &what);
    }
    l3g_put(&what, "applied ");
    l3g_put(&what, config.curve);
    l3g_put(&what, ", grey ");
    l3g_put_int(&what, (int32_t)l3g_grey(curve));
    return finish(api, 0, &what);
}

int32_t nlr_extension_init(const struct nlr_api *api)
{
    if (!api || api->abi < 5u || !api->fs_write)
        return -NLR_API_ENOSYS;
    /* A loader newer than this build: the SDK's rule is to refuse its
     * table, but refusing by failing would hold the interruption marker
     * and pause every start until the extension is removed. The table only
     * grows at the end, so the calls used to say so are safe. */
    if (api->abi > NLR_ABI_VERSION) {
        struct l3g_text what;
        l3g_clear(&what);
        l3g_put(&what, "loader is newer (ABI ");
        l3g_put_int(&what, (int32_t)api->abi);
        l3g_put(&what, "), rebuild; N-Log unchanged");
        return finish(api, 0, &what);
    }
    uint16_t curve[L3G_POINTS];
    return start(api, curve);
}
