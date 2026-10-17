/*
 * The NEF: the camera's own Exif block with a raw frame attached.
 *
 * A JPEG the camera just wrote carries, in its APP1 segment, a complete
 * little-endian TIFF: the first directory with the camera and the picture
 * settings, the Exif directory behind it, Nikon's MakerNote inside that,
 * and a second directory for the thumbnail. A NEF is the same TIFF with
 * one directory more -- the SubIFD that describes the sensor's own frame.
 *
 * So the file is written as:
 *
 *   our eight-byte TIFF header     (points at our first directory)
 *   the camera's block, from +8    (every offset in it still valid, because
 *                                   the block keeps its place in the file)
 *   our first directory            (the block's own entries, plus SubIFDs)
 *   our SubIFD                     (the raw frame: size, 14 bits, 34713,
 *                                   the CFA pattern, the black level)
 *   zeros to a 512-byte boundary
 *   the frame's TicoRAW payload    (straight from the ring)
 *
 * Nothing large is copied: only the two directories are built here, and
 * the writer puts the pieces on the card one after another.
 *
 * When the JPEG has no block this can use, the same code writes the plain
 * header it wrote before: a TIFF with the raw frame and nothing else,
 * which still finds the pixels.
 */
#include "nef.h"
#include "c7d5.h"

#define TYPE_BYTE  1u
#define TYPE_ASCII 2u
#define TYPE_SHORT 3u
#define TYPE_LONG  4u
#define TYPE_RATIONAL 5u
#define TYPE_UNDEFINED 7u

#define SUB_ENTRIES  14u
#define PLAIN_IFD0_ENTRIES 6u
#define TAG_BYTES    12u   /* one directory entry */
#define STRIP_ALIGN  512u

static const char MAKE[] = "NIKON CORPORATION";
static const char MODEL[] = "NIKON Z 6III";

struct writer {
    uint64_t base;
    uint32_t at;
    uint32_t cap;
};

static void put8(struct writer *w, uint32_t at, uint8_t value)
{
    if (at < w->cap)
        *(volatile uint8_t *)(uintptr_t)(w->base + at) = value;
}

static void put16(struct writer *w, uint32_t at, uint16_t value)
{
    put8(w, at, (uint8_t)value);
    put8(w, at + 1u, (uint8_t)(value >> 8));
}

static void put32(struct writer *w, uint32_t at, uint32_t value)
{
    put16(w, at, (uint16_t)value);
    put16(w, at + 2u, (uint16_t)(value >> 16));
}

static void put_text(struct writer *w, uint32_t at, const char *text,
                     uint32_t bytes)
{
    for (uint32_t n = 0; n < bytes; n++)
        put8(w, at + n, (uint8_t)text[n]);
}

/* One directory entry. TIFF keeps a value of four bytes or fewer in the
 * entry itself and only larger ones at an offset, so a short value is
 * passed as itself and a long one as where it is. */
static void entry(struct writer *w, uint32_t at, uint32_t tag, uint32_t type,
                  uint32_t count, uint32_t value)
{
    put16(w, at, (uint16_t)tag);
    put16(w, at + 2u, (uint16_t)type);
    put32(w, at + 4u, count);
    put32(w, at + 8u, value);
    if (type == TYPE_SHORT && count == 1u)
        put32(w, at + 8u, value & 0xffffu);
}

static uint32_t text_bytes(const char *text)
{
    uint32_t n = 0;
    while (text[n])
        n++;
    return n + 1u;
}

/* ---- the camera's own block --------------------------------------------- */

static uint16_t read16(uint64_t at)
{
    return (uint16_t)(rd8(at) | ((uint32_t)rd8(at + 1u) << 8));
}

static uint32_t read32(uint64_t at)
{
    return (uint32_t)read16(at) | ((uint32_t)read16(at + 2u) << 16);
}

/* The APP1 segment of a JPEG, as a TIFF block: its address and length, and
 * where its first directory sits inside it. Zero length when the JPEG is
 * not one this understands. */
struct exif_block {
    uint64_t at;
    uint32_t bytes;
    uint32_t ifd0;     /* offset of the first directory within the block */
    uint32_t entries;
    uint32_t next;     /* the block's own next-directory offset, kept */
};

static void find_exif(uint64_t jpeg, uint32_t jpeg_bytes,
                      struct exif_block *out)
{
    out->bytes = 0;
    if (!jpeg || jpeg_bytes < 32u || rd8(jpeg) != 0xffu || rd8(jpeg + 1u) != 0xd8u)
        return;
    uint64_t at = jpeg + 2u;
    uint64_t end = jpeg + (jpeg_bytes < (1u << 20) ? jpeg_bytes : (1u << 20));
    while (at + 4u < end) {
        if (rd8(at) != 0xffu)
            return;
        uint32_t marker = rd8(at + 1u);
        if (marker == 0xd8u || (marker >= 0xd0u && marker <= 0xd9u)) {
            at += 2u;
            continue;
        }
        uint32_t length = ((uint32_t)rd8(at + 2u) << 8) | rd8(at + 3u);
        if (length < 2u || at + 2u + length > end)
            return;
        if (marker == 0xe1u && length > 16u &&
            rd8(at + 4u) == 'E' && rd8(at + 5u) == 'x' && rd8(at + 6u) == 'i' &&
            rd8(at + 7u) == 'f' && !rd8(at + 8u) && !rd8(at + 9u)) {
            uint64_t tiff = at + 10u;
            if (rd8(tiff) != 'I' || rd8(tiff + 1u) != 'I' ||
                read16(tiff + 2u) != 42u)
                return;                      /* big-endian: not this camera */
            out->at = tiff;
            out->bytes = length - 8u;        /* the segment past "Exif\0\0" */
            out->ifd0 = read32(tiff + 4u);
            if (out->ifd0 + 2u >= out->bytes)
                return;
            out->entries = read16(tiff + out->ifd0);
            uint32_t after = out->ifd0 + 2u + out->entries * TAG_BYTES;
            if (!out->entries || out->entries > 200u || after + 4u > out->bytes) {
                out->bytes = 0;
                return;
            }
            out->next = read32(tiff + after);
            return;
        }
        if (marker == 0xdau)                 /* the image starts; no Exif */
            return;
        at += 2u + length;
    }
}

/* ---- the camera's MakerNote --------------------------------------------- */

/* Nikon's MakerNote, inside the Exif block: "Nikon\0\2\x10\0\0", then a
 * TIFF of its own whose offsets count from that TIFF's header. A JPEG's
 * lacks the two tags a raw developer needs and a NEF's has -- the black
 * level and the crop -- so the NEF gets a new copy of the MakerNote's
 * directory with both added. The directory is ours; every value it points
 * at stays where the camera put it. */
struct maker_note {
    uint32_t at;       /* "Nikon" within the Exif block, 0 when unusable */
    uint32_t base;     /* its TIFF header: offsets inside count from here */
    uint32_t entries;
    uint32_t first;    /* the directory, from base */
    uint32_t wb;       /* the white-balance levels (0x000C), from base */
    uint32_t temperature; /* 0x004F, kelvin, 0 when the JPEG has none */
    uint32_t has_black;
    uint32_t has_preview; /* 0x0011, the PreviewIFD */
};

static uint32_t find_tag(uint64_t tiff, uint32_t ifd, uint32_t tag,
                         uint32_t limit, uint32_t *count)
{
    if (ifd + 2u > limit)
        return 0;
    uint32_t n = read16(tiff + ifd);
    if (n > 400u || ifd + 2u + n * TAG_BYTES > limit)
        return 0;
    for (uint32_t i = 0; i < n; i++) {
        uint64_t e = tiff + ifd + 2u + i * TAG_BYTES;
        if (read16(e) == tag) {
            if (count)
                *count = read32(e + 4u);
            return read32(e + 8u);
        }
    }
    return 0;
}

static void find_maker_note(const struct exif_block *exif,
                            struct maker_note *mn)
{
    mn->at = 0;
    uint32_t exif_ifd = find_tag(exif->at, exif->ifd0, 0x8769u, exif->bytes, 0);
    uint32_t count = 0;
    uint32_t at = exif_ifd ? find_tag(exif->at, exif_ifd, 0x927cu, exif->bytes,
                                      &count) : 0;
    if (!at || at + 18u > exif->bytes || count < 18u)
        return;
    uint64_t p = exif->at + at;
    if (rd8(p) != 'N' || rd8(p + 1u) != 'i' || rd8(p + 2u) != 'k' ||
        rd8(p + 3u) != 'o' || rd8(p + 4u) != 'n' || rd8(p + 5u) ||
        rd8(p + 10u) != 'I' || rd8(p + 11u) != 'I' || read16(p + 12u) != 42u)
        return;
    uint32_t base = at + 10u;
    uint32_t first = read32(exif->at + base + 4u);
    if (base + first + 2u > exif->bytes)
        return;
    uint32_t entries = read16(exif->at + base + first);
    if (!entries || entries > 200u ||
        base + first + 2u + entries * TAG_BYTES + 4u > exif->bytes)
        return;
    uint32_t wb_count = 0;
    uint32_t wb = find_tag(exif->at + base, first, 0x000cu,
                           exif->bytes - base, &wb_count);
    if (wb_count != 4u || base + wb + 32u > exif->bytes)
        return;
    mn->base = base;
    mn->first = first;
    mn->entries = entries;
    mn->wb = wb;
    uint32_t kelvin_count = 0;
    /* a SHORT this small rides in the entry: find_tag returns it */
    uint32_t kelvin = find_tag(exif->at + base, first, 0x004fu,
                               exif->bytes - base, &kelvin_count) & 0xffffu;
    mn->temperature = kelvin_count == 1u ? kelvin : 0u;
    uint32_t black_count = 0;
    find_tag(exif->at + base, first, 0x003du, exif->bytes - base, &black_count);
    mn->has_black = black_count != 0u;
    uint32_t preview_count = 0;
    find_tag(exif->at + base, first, 0x0011u, exif->bytes - base, &preview_count);
    mn->has_preview = preview_count != 0u;
    mn->at = at;
}

/* ---- the directories ---------------------------------------------------- */

/* CRC-16/CCITT-FALSE, the check each field of the 0xC7D5 block ends in. */
static uint16_t crc16(struct writer *w, uint32_t at, uint32_t bytes)
{
    uint32_t crc = 0xffffu;
    for (uint32_t n = 0; n < bytes; n++) {
        crc ^= (uint32_t)rd8(w->base + at + n) << 8;
        for (uint32_t bit = 0; bit < 8u; bit++)
            crc = (crc & 0x8000u) ? ((crc << 1) ^ 0x1021u) & 0xffffu
                                  : (crc << 1) & 0xffffu;
    }
    return (uint16_t)crc;
}

/* The 0xC7D5 block at `at`: the template, this frame's white balance,
 * black level, crop and (when known) colour temperature written into it,
 * and every field's CRC done again. */
static void c7d5_block(struct writer *w, uint32_t at,
                       const struct nef_source *src, uint64_t wb,
                       uint32_t temperature)
{
    for (uint32_t n = 0; n < C7D5_BYTES; n++)
        put8(w, at + n, C7D5_TEMPLATE[n]);
    uint32_t base = at + 10u;
    uint32_t first = read32(w->base + base + 4u);
    uint32_t fields = read16(w->base + base + first);
    for (uint32_t i = 0; i < fields && i < 32u; i++) {
        uint32_t e = base + first + 2u + i * TAG_BYTES;
        uint32_t tag = read16(w->base + e);
        uint32_t bytes = read32(w->base + e + 4u);
        uint32_t value = bytes > 4u ? base + read32(w->base + e + 8u) : e + 8u;
        if (bytes < 4u || value + bytes > at + C7D5_BYTES)
            continue;
        if (tag == 0x09u && bytes >= 34u && wb)
            for (uint32_t n = 0; n < 32u; n++)
                put8(w, value + n, rd8(wb + n));
        if (tag == 0x0au && bytes >= 14u)
            for (uint32_t n = 0; n < 4u; n++)
                put16(w, value + 4u + n * 2u, src->black[n]);
        if (tag == 0x0bu && bytes >= 14u) {
            put16(w, value + 4u, 8u);
            put16(w, value + 6u, 4u);
            put16(w, value + 8u, (uint16_t)(src->width - 16u));
            put16(w, value + 10u, (uint16_t)(src->height - 8u));
        }
        if (tag == 0x10u && bytes >= 8u && temperature)
            put16(w, value + 4u, (uint16_t)temperature);
        put16(w, value + bytes - 2u, crc16(w, value, bytes - 2u));
    }
}

/* The SubIFD that describes the frame, with the strip at `strip` and the
 * 0xC7D5 block at `c7d5`. */
static void raw_directory(struct writer *w, uint32_t at,
                          const struct nef_source *src, uint32_t strip,
                          uint32_t c7d5)
{
    put16(w, at, (uint16_t)SUB_ENTRIES);
    uint32_t e = at + 2u;
    entry(w, e, 0x00fe, TYPE_LONG, 1u, 0u);                  e += TAG_BYTES;
    entry(w, e, 0x0100, TYPE_LONG, 1u, src->width);          e += TAG_BYTES;
    entry(w, e, 0x0101, TYPE_LONG, 1u, src->height);         e += TAG_BYTES;
    entry(w, e, 0x0102, TYPE_SHORT, 1u, 14u);                e += TAG_BYTES;
    entry(w, e, 0x0103, TYPE_SHORT, 1u, 34713u);             e += TAG_BYTES;
    entry(w, e, 0x0106, TYPE_SHORT, 1u, 32803u);             e += TAG_BYTES;
    entry(w, e, 0x0111, TYPE_LONG, 1u, strip);               e += TAG_BYTES;
    entry(w, e, 0x0115, TYPE_SHORT, 1u, 1u);                 e += TAG_BYTES;
    entry(w, e, 0x0116, TYPE_LONG, 1u, src->height);         e += TAG_BYTES;
    entry(w, e, 0x0117, TYPE_LONG, 1u, src->payload_bytes);  e += TAG_BYTES;
    /* Four bytes or fewer: the value rides in the entry. 2 x 2 for the
     * pattern's size, then the pattern itself, red green green blue. */
    entry(w, e, 0x828d, TYPE_SHORT, 2u, 0x00020002u);        e += TAG_BYTES;
    entry(w, e, 0x828e, TYPE_BYTE, 4u, 0x02010100u);         e += TAG_BYTES;
    entry(w, e, 0xc7d5, TYPE_UNDEFINED, C7D5_BYTES, c7d5);   e += TAG_BYTES;
    entry(w, e, 0xc7d6, TYPE_BYTE, 1u, 1u);                  e += TAG_BYTES;
    put32(w, e, 0);
}

static uint32_t raw_directory_bytes(void)
{
    return 2u + SUB_ENTRIES * TAG_BYTES + 4u;
}

/* A new MakerNote directory at `at` (a file offset: our buffer is the
 * file's start), the camera's entries byte for byte -- their offsets still
 * count from the MakerNote's header, which keeps its place -- with the
 * tags a NEF has and a JPEG's MakerNote does not, in tag order:
 *
 *   0x0011  the PreviewIFD, when there is a preview (`preview`, 0 if not):
 *           the camera's playback shows a NEF from this and nothing else
 *   0x003D  the black level      0x0045  the crop
 *
 * The two SHORT x 4 values follow the directory. */
static uint32_t maker_note_directory_bytes(const struct maker_note *mn,
                                           uint32_t preview)
{
    uint32_t added = 2u + (preview ? 1u : 0u);
    return 2u + (mn->entries + added) * TAG_BYTES + 4u + 16u;
}

static void maker_note_directory(struct writer *w, uint32_t at,
                                 const struct exif_block *exif,
                                 const struct maker_note *mn,
                                 const struct nef_source *src,
                                 uint32_t preview)
{
    uint32_t added = 2u + (preview ? 1u : 0u);
    uint32_t entries = mn->entries + added;
    uint32_t values = at + 2u + entries * TAG_BYTES + 4u;
    put16(w, at, (uint16_t)entries);
    uint32_t out = at + 2u;
    uint32_t pending = preview ? 0x0011u : 0x003du;
    uint64_t from = exif->at + mn->base + mn->first + 2u;
    for (uint32_t i = 0; i <= mn->entries; i++) {
        uint32_t tag = i < mn->entries ? read16(from + i * TAG_BYTES) : 0x10000u;
        while (pending < tag) {
            if (pending == 0x0011u) {
                entry(w, out, 0x0011u, TYPE_LONG, 1u, preview - mn->base);
                pending = 0x003du;
            } else if (pending == 0x003du) {
                entry(w, out, 0x003du, TYPE_SHORT, 4u, values - mn->base);
                for (uint32_t n = 0; n < 4u; n++)
                    put16(w, values + n * 2u, src->black[n]);
                pending = 0x0045u;
            } else {
                entry(w, out, 0x0045u, TYPE_SHORT, 4u, values + 8u - mn->base);
                put16(w, values + 8u, 8u);
                put16(w, values + 10u, 4u);
                put16(w, values + 12u, (uint16_t)(src->width - 16u));
                put16(w, values + 14u, (uint16_t)(src->height - 8u));
                pending = 0x10000u;
            }
            out += TAG_BYTES;
        }
        if (i == mn->entries)
            break;
        for (uint32_t b = 0; b < TAG_BYTES; b++)
            put8(w, out + b, rd8(from + i * TAG_BYTES + b));
        out += TAG_BYTES;
    }
    put32(w, out, read32(from + mn->entries * TAG_BYTES));  /* next, kept */
}

/* The PreviewIFD at `at`: a JPEG of `bytes` at file offset `jpeg`, every
 * offset from the MakerNote's header `base`, as in the camera's own NEF. */
#define PREVIEW_ENTRIES 8u
#define PREVIEW_IFD_BYTES (2u + PREVIEW_ENTRIES * TAG_BYTES + 4u + 16u)

static void preview_directory(struct writer *w, uint32_t at, uint32_t base,
                              uint32_t jpeg, uint32_t bytes)
{
    uint32_t values = at + 2u + PREVIEW_ENTRIES * TAG_BYTES + 4u;
    put16(w, at, (uint16_t)PREVIEW_ENTRIES);
    uint32_t e = at + 2u;
    entry(w, e, 0x00fe, TYPE_LONG, 1u, 1u);                   e += TAG_BYTES;
    entry(w, e, 0x0103, TYPE_SHORT, 1u, 6u);                  e += TAG_BYTES;
    entry(w, e, 0x011a, TYPE_RATIONAL, 1u, values - base);    e += TAG_BYTES;
    entry(w, e, 0x011b, TYPE_RATIONAL, 1u, values + 8u - base); e += TAG_BYTES;
    entry(w, e, 0x0128, TYPE_SHORT, 1u, 2u);                  e += TAG_BYTES;
    entry(w, e, 0x0201, TYPE_LONG, 1u, jpeg - base);          e += TAG_BYTES;
    entry(w, e, 0x0202, TYPE_LONG, 1u, bytes);                e += TAG_BYTES;
    entry(w, e, 0x0213, TYPE_SHORT, 1u, 2u);                  e += TAG_BYTES;
    put32(w, e, 0);
    put32(w, values, 300u);        /* 300 dpi, both ways */
    put32(w, values + 4u, 1u);
    put32(w, values + 8u, 300u);
    put32(w, values + 12u, 1u);
}

static uint32_t align4(uint32_t n)
{
    return (n + 3u) & ~3u;
}

int nef_build(uint64_t out, uint32_t cap, const struct nef_source *src,
              struct nef_plan *plan)
{
    if (!out || !src || !plan || !src->payload || !src->payload_bytes ||
        !src->width || !src->height || cap < 1024u)
        return -1;
    struct writer w = { out, 0, cap };
    for (uint32_t n = 0; n < cap; n += 4u)
        put32(&w, n, 0);
    for (uint32_t n = 0; n < sizeof *plan; n++)
        ((uint8_t *)plan)[n] = 0;
    plan->payload = src->payload;
    plan->payload_bytes = src->payload_bytes;

    struct exif_block exif;
    find_exif(src->jpeg, src->jpeg_bytes, &exif);

    if (exif.bytes) {
        struct maker_note mn;
        find_maker_note(&exif, &mn);
        int extend = mn.at && !mn.has_black;
        int preview = extend && src->preview_bytes && !mn.has_preview;
        /* The camera's block, copied whole, is the file's start: every
         * offset in it stays true, and the one word that names the
         * MakerNote's directory is ours to change in the copy. Our
         * directories follow it, then the preview, then the strip. */
        uint32_t L = exif.bytes;
        uint32_t entries = exif.entries + 1u;
        uint32_t d = L;
        uint32_t ifd0 = d;               d += 2u + entries * TAG_BYTES + 4u;
        uint32_t sub = d;                d += raw_directory_bytes();
        d = align4(d);
        uint32_t c7d5 = d;               d += C7D5_BYTES;
        d = align4(d);
        uint32_t mnd = d;
        if (extend)
            d += maker_note_directory_bytes(&mn, preview);
        d = align4(d);
        uint32_t pvd = d;
        if (preview)
            d += PREVIEW_IFD_BYTES;
        uint32_t preview_bytes = preview ? src->preview_bytes : 0u;
        /* The strip on 512: the padding is ours, before the preview. */
        uint32_t strip = (d + preview_bytes + STRIP_ALIGN - 1u) &
                         ~(STRIP_ALIGN - 1u);
        uint32_t head_bytes = strip - preview_bytes;
        if (head_bytes > cap)
            return -2;

        put_text(&w, 0, "II", 2u);
        put16(&w, 2u, 42u);
        put32(&w, 4u, ifd0);
        for (uint32_t n = 8u; n < L; n++)
            put8(&w, n, rd8(exif.at + n));

        /* IFD0: the camera's entries and the one it does not have, in tag
         * order -- a reader that stops at the first tag past the one it
         * wants never finds what follows an entry out of place (with
         * SubIFDs last, Apple's never reached the Exif pointer, and with it
         * the MakerNote's black level). */
        put16(&w, ifd0, (uint16_t)entries);
        uint32_t e = ifd0 + 2u, added = 0;
        for (uint32_t n = 0; n <= exif.entries; n++) {
            uint64_t from = exif.at + exif.ifd0 + 2u + n * TAG_BYTES;
            uint32_t tag = n < exif.entries ? read16(from) : 0x10000u;
            if (!added && tag > 0x014au) {
                entry(&w, e, 0x014a, TYPE_LONG, 1u, sub);
                e += TAG_BYTES;
                added = 1;
            }
            if (n == exif.entries)
                break;
            for (uint32_t b = 0; b < TAG_BYTES; b++)
                put8(&w, e + b, rd8(from + b));
            e += TAG_BYTES;
        }
        put32(&w, e, exif.next);

        raw_directory(&w, sub, src, strip, c7d5);
        c7d5_block(&w, c7d5, src, mn.at ? exif.at + mn.base + mn.wb : 0,
                   mn.at ? mn.temperature : 0u);
        if (extend) {
            maker_note_directory(&w, mnd, &exif, &mn, src, preview ? pvd : 0u);
            put32(&w, mn.base + 4u, mnd - mn.base);
        }
        if (preview)
            preview_directory(&w, pvd, mn.base, head_bytes, preview_bytes);

        plan->head = out;
        plan->head_bytes = head_bytes;
        plan->preview_at = head_bytes;
        plan->preview_bytes = preview_bytes;
        return extend ? 0 : 2;
    }

    /* No usable Exif: the plain TIFF. */
    uint32_t ifd0 = 8u;
    uint32_t ifd0_bytes = 2u + PLAIN_IFD0_ENTRIES * TAG_BYTES + 4u;
    uint32_t sub = ifd0 + ifd0_bytes;
    uint32_t make_at = sub + raw_directory_bytes();
    uint32_t model_at = make_at + text_bytes(MAKE);
    uint32_t c7d5 = (model_at + text_bytes(MODEL) + 3u) & ~3u;
    uint32_t end = c7d5 + C7D5_BYTES;
    uint32_t strip = (end + STRIP_ALIGN - 1u) & ~(STRIP_ALIGN - 1u);
    if (strip > cap)
        return -2;

    put_text(&w, 0, "II", 2u);
    put16(&w, 2u, 42u);
    put32(&w, 4u, ifd0);
    put16(&w, ifd0, (uint16_t)PLAIN_IFD0_ENTRIES);
    uint32_t e = ifd0 + 2u;
    entry(&w, e, 0x00fe, TYPE_LONG, 1u, 1u);                       e += TAG_BYTES;
    entry(&w, e, 0x010f, TYPE_ASCII, text_bytes(MAKE), make_at);   e += TAG_BYTES;
    entry(&w, e, 0x0110, TYPE_ASCII, text_bytes(MODEL), model_at); e += TAG_BYTES;
    entry(&w, e, 0x0112, TYPE_SHORT, 1u, 1u);                      e += TAG_BYTES;
    entry(&w, e, 0x0131, TYPE_ASCII, text_bytes(MODEL), model_at); e += TAG_BYTES;
    entry(&w, e, 0x014a, TYPE_LONG, 1u, sub);                      e += TAG_BYTES;
    put32(&w, e, 0);
    raw_directory(&w, sub, src, strip, c7d5);
    put_text(&w, make_at, MAKE, text_bytes(MAKE));
    put_text(&w, model_at, MODEL, text_bytes(MODEL));
    c7d5_block(&w, c7d5, src, 0, 0u);

    plan->head = out;
    plan->head_bytes = strip;      /* header and directories in one piece */
    return 1;                      /* written, but without the camera's Exif */
}
