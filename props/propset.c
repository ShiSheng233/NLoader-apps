/* PROPSET: writing a setting the way the camera's menus do (ABI 7, family
 * D), the Z 8 QEMU fixture for prop_set.
 *
 * The Z 8's menus write a choice by calling the settings service with the
 * item's tagged id (image size M: 0x06000607 with 1); prop_set calls the
 * same service. It writes APPS/PROPSET.LOG:
 *   caps <platform_caps>
 *   get ...                   image size before, with its prop_info flags
 *   set <what> <result>       the refusals (no tag, no value, too long, a
 *                             blob, no such item), then the real write
 *   get ...                   image size after, and when it changed
 *   done
 *
 * The real write sets the image size to M (1) when it is L (0) and to L
 * when it is anything else, so the host knows the value to look for. */
#include "common.h"

#define LOG "APPS/PROPSET.LOG"

static void log_set(const struct nlr_api *api, const char *what, uint32_t id, uint32_t value,
                    int32_t r)
{
    struct line l = { .at = 0 };
    put(&l, "set ");
    put(&l, what);
    put(&l, " ");
    put_word(&l, id);
    put(&l, " ");
    put_dec(&l, (int32_t)value);
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

    const struct nlr_prop_name *size = nlr_prop_find("eDSID_Setting_ImageSize");
    const struct nlr_prop_name *folder = nlr_prop_find("eDSID_Setting_CaptureFolderName");
    if (!size || !folder) {
        put(&l, "done no-names");
        emit(api, LOG, &l);
        return 0;
    }
    uint32_t tagged = nlr_prop_tagged(size);
    log_get(api, LOG, "before", size->id, 4);
    log_get(api, LOG, "blob", folder->id, NLR_PROP_BYTES_MAX);

    uint32_t now = 0, got = 0;
    api->prop_get(size->id, &now, sizeof now, &got);
    uint32_t want = now == 0 ? 1u : 0u;
    uint8_t blob[64] = { 0 };
    log_set(api, "untagged", size->id, want, api->prop_set(size->id, &want, 4));
    log_set(api, "no-size", tagged, want, api->prop_set(tagged, &want, 0));
    log_set(api, "too-long", tagged, want, api->prop_set(tagged, blob, 5));
    log_set(api, "blob", nlr_prop_tagged(folder), 0,
            api->prop_set(nlr_prop_tagged(folder), blob, folder->size));
    log_set(api, "no-item", 0x06007c00u, want, api->prop_set(0x06007c00u, &want, 4));

    uint32_t t0 = api->time_ms();
    int32_t r = api->prop_set(tagged, &want, 4);
    uint32_t t1 = api->time_ms();
    log_set(api, "real", tagged, want, r);
    uint32_t seen = 0xffffffffu;
    for (uint32_t k = 0; k < 100u; k++) {
        uint32_t v = 0;
        api->prop_get(size->id, &v, sizeof v, &got);
        if (v == want) {
            seen = api->time_ms() - t0;
            break;
        }
        api->sleep_ms(10);
    }
    put(&l, "took ");
    put_dec(&l, (int32_t)(t1 - t0));
    put(&l, " seen ");
    put_dec(&l, (int32_t)seen);
    emit(api, LOG, &l);
    api->sleep_ms(500);
    log_get(api, LOG, "after", size->id, 4);
    put(&l, "done");
    emit(api, LOG, &l);
    return 0;
}
