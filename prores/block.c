#include "block.h"

uint32_t text_len(const char *text)
{
    uint32_t n = 0;
    while (text && text[n])
        n++;
    return n;
}

void block_copy_fw(uint64_t dst, uint64_t src, uint32_t len)
{
    for (uint32_t n = 0; n < len; n++)
        *(volatile uint8_t *)(uintptr_t)(dst + n) = rd8(src + n);
}

int patch_words(const struct nlr_api *api, uint64_t site, uint32_t words,
                const struct word_change *change, uint32_t changes)
{
    uint32_t buffer[16];
    if (!api || !api->code_patch || !words || words > 16u ||
        (site & 3u) || (site & 63u) + words * 4u > 64u)
        return -NLR_API_EINVAL;
    for (uint32_t n = 0; n < words; n++)
        buffer[n] = rd32(site + n * 4u);
    for (uint32_t n = 0; n < changes; n++) {
        if (change[n].index >= words)
            return -NLR_API_EINVAL;
        buffer[change[n].index] = change[n].word;
    }
    return api->code_patch((void *)(uintptr_t)site, buffer, words * 4u);
}

int code_write(const struct nlr_api *api, uint64_t dst, const void *src,
               uint32_t len)
{
    const uint8_t *from = src;
    if (!api || !api->code_patch || (dst & 3u) || (len & 3u))
        return -NLR_API_EINVAL;
    while (len) {
        uint32_t room = 64u - (uint32_t)(dst & 63u);
        uint32_t chunk = len < room ? len : room;
        if (chunk > NLR_CODE_PATCH_MAX)
            chunk = NLR_CODE_PATCH_MAX;
        int result = api->code_patch((void *)(uintptr_t)dst, from, chunk);
        if (result != 0)
            return result;
        dst += chunk;
        from += chunk;
        len -= chunk;
    }
    return 0;
}
