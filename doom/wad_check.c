#include "wad.h"

#define WAD_SAMPLE_BYTES 64u

static int sample_lump(struct nlr_wad *wad, uint32_t index,
                       const struct nlr_wad_lump *lump, uint32_t *checksum)
{
    uint8_t sample[WAD_SAMPLE_BYTES];
    uint32_t size = lump->size < sizeof sample ? lump->size : sizeof sample;
    int result = nlr_wad_read(wad, index, 0, sample, size);
    if (result)
        return result;
    uint32_t hash = UINT32_C(2166136261);
    for (uint32_t n = 0; n < size; n++)
        hash = (hash ^ sample[n]) * UINT32_C(16777619);
    *checksum = hash;
    return NLR_WAD_OK;
}

static int sample_ends(struct nlr_wad *wad, uint32_t checksums[2])
{
    for (uint32_t end = 0; end < 2u; end++) {
        for (uint32_t n = 0; n < wad->lump_count; n++) {
            uint32_t index = end ? wad->lump_count - n - 1u : n;
            struct nlr_wad_lump lump;
            int result = nlr_wad_lump(wad, index, &lump);
            if (result)
                return result;
            if (!lump.size)
                continue;
            result = sample_lump(wad, index, &lump, &checksums[end]);
            if (result)
                return result;
            break;
        }
    }
    return NLR_WAD_OK;
}

int32_t nlr_app_main(const struct nlr_api *api)
{
    if (!api || !api->abi || api->abi > NLR_ABI_VERSION ||
        !api->log || !api->ui_draw_text)
        return NLR_WAD_API;
    struct nlr_wad wad;
    nlr_wad_init(&wad);
    uint32_t checksums[2] = {0, 0};
    int result = nlr_wad_open(&wad, api, "DOOM.WAD");
    if (!result)
        result = sample_ends(&wad, checksums);
    /* A failed close still belongs to loader cleanup. Do not repeatedly
     * retry it or claim the file has been released. */
    if (result != NLR_WAD_CLOSE && nlr_wad_close(&wad))
        result = NLR_WAD_CLOSE;
    if (result)
        api->log("wad rejected: %d", result);
    else
        api->log("W %u %x %x", wad.lump_count, checksums[0], checksums[1]);
    api->ui_draw_text(0, 0, result ? "WAD rejected" : "WAD ready");
    return result;
}
