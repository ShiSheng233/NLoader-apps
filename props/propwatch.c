/* PROPWATCH: reading and watching the camera's settings (ABI 7, family D),
 * the Z 8 QEMU fixture for prop_get / prop_info / prop_watch.
 *
 * It writes APPS/PROPWATCH.LOG:
 *   caps <platform_caps>
 *   get <name> <id> r=<result> size=<n> info=<result> flags=<f> val=<hex>
 *       a batch of known settings, by the firmware's own names
 *       (sdk/z8/nlr_props.h), then the refusals: a dead group, a group past
 *       the table, an item past its group, a tagged id, a value bigger than
 *       the loader reads, and a buffer too small
 *   watch <result> ...   the refusals of prop_watch, then the real set
 *   ev <type> <code> <value> <t_ms> [now=<hex>]
 *       every event; for a setting, its value read again at once
 *   done <reason>
 *
 * Every control goes back to the camera (input_route), so the stock menus
 * work with the program running: the host changes a setting there and the
 * program must see the event with the item's id and its new value. It stops
 * after 240 s, or with MENU + DELETE like any program. */
#include "common.h"

#define LOG "APPS/PROPWATCH.LOG"

static const char *const reads[] = {
    "eDSID_Setting_ImageSize", "eDSID_Setting_WbMode", "eDSID_Setting_JpegEncodeQuality",
    "eDSID_Setting_IsoSense", "eDSID_Setting_FNumber", "eDSID_Setting_ShutterSpeed",
    "eDSID_Setting_CaptureFolderName",
};

static void log_watch(const struct nlr_api *api, const char *what, int32_t r)
{
    struct line l = { .at = 0 };
    put(&l, "watch ");
    put(&l, what);
    put(&l, " ");
    put_dec(&l, r);
    emit(api, LOG, &l);
}

int32_t nlr_app_main(const struct nlr_api *api)
{
    if (!api || api->abi < 7u)
        return 0;
    struct line l = { .at = 0 };
    put(&l, "caps ");
    put_dec(&l, (int32_t)api->platform_caps());
    emit(api, LOG, &l);

    for (uint32_t n = 0; n < sizeof reads / sizeof reads[0]; n++)
        log_get(api, LOG, reads[n], id_of(reads[n]), NLR_PROP_BYTES_MAX);
    uint32_t size_id = id_of("eDSID_Setting_ImageSize");
    log_get(api, LOG, "dead-group", 0x0800u, NLR_PROP_BYTES_MAX);
    log_get(api, LOG, "past-groups", 0x7c00u, NLR_PROP_BYTES_MAX);
    log_get(api, LOG, "past-items", 0x06ffu, NLR_PROP_BYTES_MAX);
    log_get(api, LOG, "tagged", 0x06000000u | size_id, NLR_PROP_BYTES_MAX);
    log_get(api, LOG, "too-big", id_of("eDSID_Setting_PictureControlStandard"), NLR_PROP_BYTES_MAX);
    log_get(api, LOG, "small-cap", size_id, 2);

    uint16_t bad[2] = { (uint16_t)size_id, 0x0800u };
    log_watch(api, "unknown", api->prop_watch(bad, 2));
    uint16_t many[NLR_WATCH_MAX + 1];
    for (uint32_t n = 0; n <= NLR_WATCH_MAX; n++)
        many[n] = (uint16_t)size_id;
    log_watch(api, "too-many", api->prop_watch(many, NLR_WATCH_MAX + 1));
    uint16_t ids[3] = { (uint16_t)size_id, (uint16_t)id_of("eDSID_Setting_WbMode"),
                        (uint16_t)id_of("eDSID_Setting_JpegEncodeQuality") };
    log_watch(api, "set", api->prop_watch(ids, 3));

    uint32_t controls[NLR_CTL_COUNT];
    for (uint32_t n = 0; n < NLR_CTL_COUNT; n++)
        controls[n] = n;
    log_watch(api, "route", api->input_route(controls, NLR_CTL_COUNT, NLR_ROUTE_CAMERA));

    uint32_t start = api->time_ms();
    const char *reason = "time";
    while (api->time_ms() - start < 240000u) {
        struct nlr_event ev;
        int32_t r = api->queue_wait(&ev, 2000);
        if (r < 0) {
            log_watch(api, "error", r);
            reason = "error";
            break;
        }
        if (!r)
            continue;
        put(&l, "ev ");
        put_dec(&l, ev.type);
        put(&l, " ");
        put_word(&l, ev.code);
        put(&l, " ");
        put_dec(&l, ev.value);
        put(&l, " ");
        put_dec(&l, (int32_t)ev.t_ms);
        if (ev.type == NLR_EV_PROP) {
            uint8_t buf[4];
            uint32_t size = 0;
            if (!api->prop_get(ev.code, buf, sizeof buf, &size)) {
                put(&l, " now=");
                put_hex(&l, buf, size);
            }
        }
        emit(api, LOG, &l);
    }
    put(&l, "done ");
    put(&l, reason);
    emit(api, LOG, &l);
    return 0;
}
