/* THEME.NLB by name (ABI 8): a resident extension that re-colours the
 * camera's own GUI through the profile's "gui.palette" table, with no
 * firmware address of its own. Built once, it runs on the Z 6III 2.00 and
 * the Z 8 3.11.
 *
 * The palette is a pointer table: the GUI finds its RGBA colours through a
 * RAM slot pointing at read-only data. table_write makes the loader copy
 * that data into RAM, change the entries asked for, and point the slot at
 * the copy; an extension's writes stay for the boot. Measured in QEMU on
 * the Z 6III with a build that changed the same entries by address: the
 * menu list, highlight, title bar, text, values, scroll bar and the
 * live-view overlay follow the pointer from their next redraw; the left
 * column of category tabs keeps its colours, which are in its bitmaps.
 *
 * The two bodies' palettes are not the same table. The Z 6III's has 561
 * entries, the Z 8's 560: compared entry by entry the Z 8 lacks five of the
 * Z 6III's, adds four and changes one, so the same colour sits at another
 * index past the first difference (index 50). The Z 8 set below is the
 * Z 6III set moved by that alignment (stock colours equal on both bodies;
 * from the two stock firmwares' palette bytes, whose CRC-32 matches each
 * profile). The set is chosen by the table's size, as named() reports it,
 * never by an address.
 *
 * Every entry is checked before anything is written: the table must hold
 * its stock bytes (named() says so) and each entry its stock colour. Any
 * difference leaves the camera as it is and says why in THEMELOG.TXT.
 * Returns 0 in every case but a write the loader refused half way. */
#include "nlr.h"

struct swap {
    uint16_t index;
    uint32_t stock; /* 0xRRGGBBAA as the firmware holds it */
    uint32_t theme;
};

/* The new UI's colour tokens (z63-newui SPEC.md 2.1) on the stock roles
 * measured for each entry of the Z 6III's palette (561 entries). */
static const struct swap swaps_z6iii[] = {
    { 62, 0xFCD723FFu, 0xE36617FFu },  /* highlight bar: accent */
    { 385, 0xCCCCCCFFu, 0xE36617FFu }, /* scroll thumb: accent */
    { 35, 0x222222FFu, 0x171608FFu },  /* row background: bg */
    { 39, 0x2B2B2BFFu, 0x242418FFu },  /* title bar: divider-faint */
    { 58, 0xFFFFFFFFu, 0xFDFDFDFFu },  /* text: text */
    { 23, 0xBEBEBEFFu, 0x71716BFFu },  /* values, row icons, >: text-dim */
    { 19, 0xA0A0A0FFu, 0x71716BFFu },  /* grey elements: text-dim */
    { 17, 0x8C8C8CFFu, 0x4D4C46FFu },  /* highlight on the tab column: border */
    { 45, 0x464646FFu, 0x4D4C46FFu },  /* live-view border line: border */
};

/* The same roles in the Z 8's palette (560 entries): entries before index
 * 50 keep their place; 62 -> 60, 58 -> 56 and 385 -> 384 move with the
 * entries the Z 8 lacks (Z 6III 50, 57, 267, 271, 377) and adds. */
static const struct swap swaps_z8[] = {
    { 60, 0xFCD723FFu, 0xE36617FFu },  /* highlight bar: accent */
    { 384, 0xCCCCCCFFu, 0xE36617FFu }, /* scroll thumb: accent */
    { 35, 0x222222FFu, 0x171608FFu },  /* row background: bg */
    { 39, 0x2B2B2BFFu, 0x242418FFu },  /* title bar: divider-faint */
    { 56, 0xFFFFFFFFu, 0xFDFDFDFFu },  /* text: text */
    { 23, 0xBEBEBEFFu, 0x71716BFFu },  /* values, row icons, >: text-dim */
    { 19, 0xA0A0A0FFu, 0x71716BFFu },  /* grey elements: text-dim */
    { 17, 0x8C8C8CFFu, 0x4D4C46FFu },  /* highlight on the tab column: border */
    { 45, 0x464646FFu, 0x4D4C46FFu },  /* live-view border line: border */
};

#define SWAPS (sizeof swaps_z8 / sizeof swaps_z8[0])

static void log_line(const struct nlr_api *api, const char *what)
{
    uint32_t n = 0;
    while (what[n])
        n++;
    int fd = api->fs_open("THEMELOG.TXT", NLR_OPEN_WRITE);
    if (fd < 0)
        return;
    api->fs_write(fd, what, n);
    api->fs_write(fd, "\n", 1);
    api->fs_close(fd);
}

int32_t nlr_extension_init(const struct nlr_api *api)
{
    if (!api || api->abi < 8u) {
        if (api && api->abi >= 5u)
            log_line(api, "refused: needs a loader with ABI 8 (firmware by name)");
        return 0;
    }
    struct nlr_named palette;
    if (api->named("gui.palette", &palette) || palette.kind != NLR_NAMED_TABLE) {
        log_line(api, "refused: this body's profile has no gui.palette");
        return 0;
    }
    const struct swap *swaps;
    if (palette.size == 561u * 4u)
        swaps = swaps_z6iii;
    else if (palette.size == 560u * 4u)
        swaps = swaps_z8;
    else {
        log_line(api, "refused: a palette of a size this theme does not know");
        return 0;
    }
    if (!(palette.flags & NLR_NAMED_STOCK)) {
        log_line(api, "refused: the palette is not the stock one (another theme?)");
        return 0;
    }
    for (uint32_t n = 0; n < SWAPS; n++) {
        uint32_t now = 0;
        if (4u * swaps[n].index + 4u > palette.size ||
            api->table_read("gui.palette", 4u * swaps[n].index, &now, 4) || now != swaps[n].stock) {
            log_line(api, "refused: a palette entry is not the stock colour");
            return 0;
        }
    }
    for (uint32_t n = 0; n < SWAPS; n++)
        if (api->table_write("gui.palette", 4u * swaps[n].index, &swaps[n].theme, 4)) {
            log_line(api, "a write was refused half way; restoring the stock palette");
            api->table_restore("gui.palette");
            return 23;
        }
    log_line(api, palette.size == 560u * 4u ? "theme in place (gui.palette, by name, Z 8 set)"
                                            : "theme in place (gui.palette, by name)");
    return 0;
}
