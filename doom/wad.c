#include "wad.h"

#define WAD_HEADER_BYTES 12u
#define WAD_ENTRY_BYTES 16u
#define WAD_DIRECTORY_CHUNK 512u

static uint32_t little_word(const uint8_t *bytes)
{
    return (uint32_t)bytes[0] | (uint32_t)bytes[1] << 8 |
        (uint32_t)bytes[2] << 16 | (uint32_t)bytes[3] << 24;
}

static int read_at(struct nlr_wad *wad, uint32_t offset, void *buffer,
                   uint32_t size)
{
    if (offset > wad->file_size || size > wad->file_size - offset)
        return NLR_WAD_BOUNDS;
    if (!size)
        return NLR_WAD_OK;
    if (wad->api->fs_seek(wad->fd, offset, NLR_SEEK_SET) != (int)offset)
        return NLR_WAD_IO;
    uint8_t *bytes = buffer;
    uint32_t done = 0;
    while (done < size) {
        int got = wad->api->fs_read(wad->fd, bytes + done, size - done);
        /* The SDK reports a zero-byte nonempty read as an error. Also
         * reject impossible service lengths rather than trusting them. */
        if (got <= 0 || (uint32_t)got > size - done)
            return NLR_WAD_IO;
        done += (uint32_t)got;
    }
    return NLR_WAD_OK;
}

static int decode_lump(const struct nlr_wad *wad, const uint8_t *entry,
                       struct nlr_wad_lump *out)
{
    uint32_t offset = little_word(entry);
    uint32_t size = little_word(entry + 4u);
    /* The original format declares signed 32-bit positions and sizes.
     * Zero-size markers have no bytes to locate, so a nonnegative marker
     * position outside the file is harmless and need not be dereferenced. */
    if (offset > INT32_MAX || size > INT32_MAX)
        return NLR_WAD_FORMAT;
    if (size && (offset > wad->file_size || size > wad->file_size - offset))
        return NLR_WAD_BOUNDS;
    if (out) {
        out->offset = offset;
        out->size = size;
        for (uint32_t n = 0; n < NLR_WAD_NAME_BYTES; n++)
            out->name[n] = entry[8u + n];
    }
    return NLR_WAD_OK;
}

void nlr_wad_init(struct nlr_wad *wad)
{
    if (!wad)
        return;
    wad->api = 0;
    wad->fd = -1;
    wad->file_size = 0;
    wad->lump_count = 0;
    wad->directory_offset = 0;
    wad->ready = 0;
}

int nlr_wad_close(struct nlr_wad *wad)
{
    if (!wad)
        return NLR_WAD_ARGUMENT;
    wad->ready = 0;
    if (wad->fd < 0)
        return NLR_WAD_OK;
    if (!wad->api || !wad->api->fs_close || wad->api->fs_close(wad->fd))
        return NLR_WAD_CLOSE;
    wad->fd = -1;
    return NLR_WAD_OK;
}

static int reject_open(struct nlr_wad *wad, int reason)
{
    return nlr_wad_close(wad) == NLR_WAD_OK ? reason : NLR_WAD_CLOSE;
}

int nlr_wad_open(struct nlr_wad *wad, const struct nlr_api *api,
                 const char *path)
{
    if (!wad || !path || !path[0])
        return NLR_WAD_ARGUMENT;
    if (wad->fd >= 0)
        return NLR_WAD_BUSY;
    if (!api || api->abi < 2u || !api->fs_open ||
        !api->fs_seek || !api->fs_read || !api->fs_close)
        return NLR_WAD_API;
    nlr_wad_init(wad);
    wad->api = api;
    wad->fd = api->fs_open(path, 0);
    if (wad->fd < 0)
        return NLR_WAD_OPEN;
    int size = api->fs_seek(wad->fd, 0, NLR_SEEK_END);
    if (size < 0)
        return reject_open(wad, NLR_WAD_IO);
    wad->file_size = (uint32_t)size;
    if (wad->file_size < WAD_HEADER_BYTES)
        return reject_open(wad, NLR_WAD_BOUNDS);
    uint8_t header[WAD_HEADER_BYTES];
    int result = read_at(wad, 0, header, sizeof header);
    if (result)
        return reject_open(wad, result);
    if ((header[0] != 'I' && header[0] != 'P') || header[1] != 'W' ||
        header[2] != 'A' || header[3] != 'D')
        return reject_open(wad, NLR_WAD_FORMAT);
    wad->lump_count = little_word(header + 4u);
    wad->directory_offset = little_word(header + 8u);
    if (wad->lump_count > INT32_MAX || wad->directory_offset > INT32_MAX)
        return reject_open(wad, NLR_WAD_FORMAT);
    /* Divide remaining bytes before multiplying count; a malicious count
     * cannot wrap a computed directory end into the file. */
    if (wad->directory_offset > wad->file_size ||
        wad->lump_count > (wad->file_size - wad->directory_offset) / WAD_ENTRY_BYTES)
        return reject_open(wad, NLR_WAD_BOUNDS);
    if (wad->lump_count > NLR_WAD_MAX_LUMPS)
        return reject_open(wad, NLR_WAD_LIMIT);
    uint8_t entries[WAD_DIRECTORY_CHUNK];
    for (uint32_t first = 0; first < wad->lump_count;) {
        uint32_t count = wad->lump_count - first;
        if (count > sizeof entries / WAD_ENTRY_BYTES)
            count = sizeof entries / WAD_ENTRY_BYTES;
        result = read_at(wad, wad->directory_offset + first * WAD_ENTRY_BYTES,
                         entries, count * WAD_ENTRY_BYTES);
        if (result)
            return reject_open(wad, result);
        for (uint32_t n = 0; n < count; n++) {
            result = decode_lump(wad, entries + n * WAD_ENTRY_BYTES, 0);
            if (result)
                return reject_open(wad, result);
        }
        first += count;
    }
    wad->ready = 1;
    return NLR_WAD_OK;
}

int nlr_wad_lump(struct nlr_wad *wad, uint32_t index,
                 struct nlr_wad_lump *out)
{
    if (!wad || !wad->ready || wad->fd < 0 || !out)
        return NLR_WAD_ARGUMENT;
    if (index >= wad->lump_count)
        return NLR_WAD_BOUNDS;
    uint8_t entry[WAD_ENTRY_BYTES];
    int result = read_at(wad, wad->directory_offset + index * WAD_ENTRY_BYTES,
                         entry, sizeof entry);
    return result ? result : decode_lump(wad, entry, out);
}

static uint8_t uppercase(uint8_t value)
{
    return value >= 'a' && value <= 'z' ? (uint8_t)(value - 'a' + 'A') : value;
}

int nlr_wad_find(struct nlr_wad *wad, const char *name, uint32_t *out_index)
{
    if (!wad || !wad->ready || !name || !name[0] || !out_index)
        return NLR_WAD_ARGUMENT;
    uint8_t wanted[NLR_WAD_NAME_BYTES] = {0};
    uint32_t length = 0;
    while (length < NLR_WAD_NAME_BYTES && name[length]) {
        if ((uint8_t)name[length] < 0x20u || (uint8_t)name[length] > 0x7eu)
            return NLR_WAD_ARGUMENT;
        wanted[length] = uppercase((uint8_t)name[length]);
        length++;
    }
    if (name[length])
        return NLR_WAD_ARGUMENT;
    for (uint32_t index = wad->lump_count; index > 0; index--) {
        struct nlr_wad_lump lump;
        int result = nlr_wad_lump(wad, index - 1u, &lump);
        if (result)
            return result;
        uint32_t matched = 0;
        while (matched < NLR_WAD_NAME_BYTES &&
               uppercase(lump.name[matched]) == wanted[matched])
            matched++;
        if (matched == NLR_WAD_NAME_BYTES) {
            *out_index = index - 1u;
            return NLR_WAD_OK;
        }
    }
    return NLR_WAD_NOT_FOUND;
}

int nlr_wad_read(struct nlr_wad *wad, uint32_t index, uint32_t offset,
                 void *buffer, uint32_t len)
{
    if (len && !buffer)
        return NLR_WAD_ARGUMENT;
    struct nlr_wad_lump lump;
    int result = nlr_wad_lump(wad, index, &lump);
    if (result)
        return result;
    if (offset > lump.size || len > lump.size - offset)
        return NLR_WAD_BOUNDS;
    return len ? read_at(wad, lump.offset + offset, buffer, len) : NLR_WAD_OK;
}
