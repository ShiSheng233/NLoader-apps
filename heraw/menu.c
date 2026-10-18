/*
 * The HE RAW options, in the camera's own menu.
 *
 * PHOTO SHOOTING MENU > Image quality settings > Image quality (HSFC)
 * offers JPEG fine and JPEG normal. It gets five more rows, HE RAW at 3.0,
 * 4.0, 5.0, 6.0 and 7.0 bits per pixel (hsfc.c makes the ring keep that
 * many), the way PRORES adds its rows: the menu engine builds a page from a
 * page record
 * in .data that names the item records (44 bytes each), one event table per
 * row, and the row count, and finds rows by item id. So the program needs
 * no patched instruction here at all:
 *
 *   - the page record names copies, in the program's block, of the two
 *     stock rows and their table, with ours after them;
 *   - the stock rows' OK (sub_40612F74, which stores item 0x04000F64) is
 *     wrapped to turn HE RAW off first; ours turns it on and leaves the
 *     JPEG setting alone, since the camera still makes the JPEG the NEF is
 *     built from;
 *   - two handler pointers are wrapped: the page's entry, so the cursor and
 *     the radio mark are on HE RAW when it is chosen, and the refresh of
 *     the page the HSFC row is on, so the row says RAW.
 *
 * None of these keeps a pointer to the block: they find it through the
 * page record (plugin_block()).
 */
#include "menu.h"
#include "block.h"

/* One label per tier, its text slot SLOT_HSFC_FIRST + tier - 1. */
static const char TIER_LABEL[TIER_COUNT][16] = {
    "HE RAW 3.0 bpp", "HE RAW 4.0 bpp", "HE RAW 5.0 bpp",
    "HE RAW 6.0 bpp", "HE RAW 7.0 bpp",
};
static const uint32_t TIER_ITEM[TIER_COUNT] = ITEM_HSFC_TIERS;

/* Records of an event table, terminator included. */
static uint32_t table_len(uint64_t table)
{
    for (uint32_t n = 0; n < 80u; n++)
        if ((uint32_t)rd64(table + n * EVENT_BYTES) == EVENT_END)
            return n + 1u;
    return 0;
}

static int slot_ready(uint32_t slot)
{
    if (slot >= TEXT_SLOT_COUNT)
        return 0;
    uint64_t buffer = rd64(TEXT_SLOTS + slot * TEXT_SLOT_BYTES +
                           TEXT_SLOT_BUFFER);
    /* The GUI's own buffers are in the camera's RAM; anything else -- zero
     * above all -- means this slot was never given one. */
    return buffer >= 0xa02000000ull && buffer < 0xa0a000000ull;
}

int menu_slots_ready(void)
{
    for (uint32_t n = 0; n < TIER_COUNT; n++)
        if (!slot_ready(SLOT_HSFC_FIRST + n))
            return 0;
    return 1;
}

/* ---- the handlers ------------------------------------------------------- */

/* The parent row's value: the camera's own, FINE or NORM, unless HE RAW is
 * chosen -- then RAW, the camera's own symbol for it, whatever the tier
 * (the font has no digits to say which). The value is a string
 * of the image-quality symbol font; a text slot drawn there comes out as
 * other symbols. */
static uint64_t quality_refresh(void *window)
{
    uint64_t result = FW(fw_handler_t, QUALITY_REFRESH)(window);
    if (!he_raw_on())
        return result;
    void *value = fw_row_value(window, QUALITY_HSFC_ROW, VALUE_WIDGET);
    if (value)
        fw_text_bind(value, QUALITY_RAW_STRING);
    return result;
}

/* The page's entry: the stock one puts the cursor and the radio mark on
 * fine or normal; with HE RAW chosen, the same two calls, on the tier's
 * row. */
static uint64_t hsfc_entry(void *window)
{
    uint32_t tier = he_raw_tier();
    if (!tier)
        return FW(fw_handler_t, HSFC_ENTRY)(window);
    uint32_t item = TIER_ITEM[tier - 1u];
    if (fw_entry_guard() != 1u)
        fw_set_cursor(window, item);
    return fw_set_mark(window, item, 1u);
}

/* OK on an HE RAW row: an action of its own per row, since the action is
 * told nothing about which row it was. The JPEG setting stays as it was:
 * the camera still makes the JPEG, whose Exif, MakerNote and preview the
 * NEF is built from. */
#define PICK_TIER(n)                                \
    static void pick_tier_##n(struct fw_nav *nav)   \
    {                                               \
        (void)nav;                                  \
        wr8(STORE_HE_RAW, n);                       \
    }
PICK_TIER(1)
PICK_TIER(2)
PICK_TIER(3)
PICK_TIER(4)
PICK_TIER(5)

static uint64_t pick_tier(uint32_t tier)
{
    switch (tier) {
    case 1: return (uint64_t)(uintptr_t)&pick_tier_1;
    case 2: return (uint64_t)(uintptr_t)&pick_tier_2;
    case 3: return (uint64_t)(uintptr_t)&pick_tier_3;
    case 4: return (uint64_t)(uintptr_t)&pick_tier_4;
    default: return (uint64_t)(uintptr_t)&pick_tier_5;
    }
}

/* OK on JPEG fine or normal: HE RAW off, and the camera's own action. */
static void pick_jpeg(struct fw_nav *nav)
{
    wr8(STORE_HE_RAW, 0u);
    FW(fw_action_t, HSFC_PICK)(nav);
}

/* ---- building the copies ------------------------------------------------ */

static void copy_table(uint64_t dst, uint64_t src, uint32_t records)
{
    for (uint32_t n = 0; n < records * EVENT_BYTES; n += 8u)
        wr64(dst + n, rd64(src + n));
}

static void replace_action(uint64_t table, uint32_t records, uint64_t from,
                           uint64_t to)
{
    for (uint32_t n = 0; n < records; n++)
        if (rd64(table + n * EVENT_BYTES + 8u) == from)
            wr64(table + n * EVENT_BYTES + 8u, to);
}

/* The My Menu record would let the user pin a row whose id means nothing
 * without this program: disabled in place by a guard that refuses. */
static void refuse_my_menu(uint64_t table, uint32_t records)
{
    for (uint32_t n = 0; n < records; n++)
        if (rd64(table + n * EVENT_BYTES) == EVENT_MYMENU)
            wr64(table + n * EVENT_BYTES + 16u, FW_GUARD_NEVER);
}

static void item_set(uint8_t *record, uint32_t at, uint32_t value)
{
    record[at] = (uint8_t)value;
    record[at + 1] = (uint8_t)(value >> 8);
    record[at + 2] = (uint8_t)(value >> 16);
    record[at + 3] = (uint8_t)(value >> 24);
}

/* The two stock rows on one copied table whose OK turns HE RAW off first,
 * and ours, each cloned from "JPEG normal" with its label in a text slot,
 * on a copy of its own whose OK picks its tier. */
static void hsfc_arrays(uint64_t block)
{
    uint64_t stock_table = rd64(HSFC_EVPTRS);
    uint32_t records = table_len(stock_table);

    copy_table(block + OFF_HSFC_JPEG, stock_table, records);
    replace_action(block + OFF_HSFC_JPEG, records, HSFC_PICK,
                   (uint64_t)(uintptr_t)&pick_jpeg);

    for (uint32_t row = 0; row < HSFC_ROWS; row++) {
        block_copy_fw(block + OFF_HSFC_ITEMS + row * ITEM_BYTES,
                      HSFC_ITEMS + row * ITEM_BYTES, ITEM_BYTES);
        wr64(block + OFF_HSFC_EVPTRS + row * 8u, block + OFF_HSFC_JPEG);
    }
    uint64_t anchor = HSFC_ITEMS + (HSFC_ROWS - 1u) * ITEM_BYTES;
    for (uint32_t tier = 1; tier <= TIER_COUNT; tier++) {
        uint64_t table = block + OFF_HSFC_RAW + (tier - 1u) * HSFC_TABLE_BYTES;
        copy_table(table, stock_table, records);
        replace_action(table, records, HSFC_PICK, pick_tier(tier));
        refuse_my_menu(table, records);

        uint8_t record[ITEM_BYTES];
        for (uint32_t n = 0; n < ITEM_BYTES; n++)
            record[n] = rd8(anchor + n);
        item_set(record, ITEM_LABEL, SLOT_HSFC_FIRST + tier - 1u);
        item_set(record, ITEM_FLAGS,
                 rd32(anchor + ITEM_FLAGS) | FLAGS_LABEL_SLOT);
        item_set(record, ITEM_ID, TIER_ITEM[tier - 1u]);
        uint32_t row = HSFC_ROWS + tier - 1u;
        uint64_t ours = block + OFF_HSFC_ITEMS + row * ITEM_BYTES;
        for (uint32_t n = 0; n < ITEM_BYTES; n++)
            wr8(ours + n, record[n]);
        wr64(block + OFF_HSFC_EVPTRS + row * 8u, table);
    }
}

/* ---- the fingerprint ---------------------------------------------------- */

int menu_fingerprint(void)
{
    /* The page record still names the module's own rows: so nothing else
     * has moved it, and this program is not installed already. */
    if (rd64(HSFC_PAGE + PAGE_ITEMS) != HSFC_ITEMS ||
        rd64(HSFC_PAGE + PAGE_EVPTRS) != HSFC_EVPTRS ||
        rd16(HSFC_PAGE + PAGE_COUNT) != HSFC_ROWS)
        return 0;
    if (rd32(HSFC_ITEMS + ITEM_ID) != HSFC_FINE_ITEM ||
        rd32(HSFC_ITEMS + ITEM_BYTES + ITEM_ID) != HSFC_NORMAL_ITEM)
        return 0;
    /* Both rows on one table whose OK is the action pick_jpeg calls. */
    uint64_t table = rd64(HSFC_EVPTRS);
    uint32_t records = table_len(table);
    if (rd64(HSFC_EVPTRS + 8u) != table || !records ||
        records > HSFC_TABLE_MAX)
        return 0;
    int has_pick = 0;
    for (uint32_t n = 0; n < records; n++)
        if (rd64(table + n * EVENT_BYTES + 8u) == HSFC_PICK)
            has_pick = 1;
    if (!has_pick)
        return 0;
    if (rd32(QUALITY_STRINGS + QUALITY_RAW_INDEX * 4u) != QUALITY_RAW_STRING)
        return 0;
    /* The handlers the wrappers call. */
    if (rd64(HSFC_HANDLERS) != HSFC_ENTRY ||
        rd64(QUALITY_HANDLERS + 4u * 8u) != QUALITY_REFRESH)
        return 0;
    return 1;
}

/* ---- install ------------------------------------------------------------ */

int menu_install(uint64_t block)
{
    /* The text first: a row built before its slot was written would draw
     * whatever the slot held. */
    for (uint32_t n = 0; n < TIER_COUNT; n++)
        fw_text_store(TIER_LABEL[n], SLOT_HSFC_FIRST + n, TEXT_STYLE_LABEL);
    hsfc_arrays(block);

    /* Now the .data words the engine reads: the arrays before the count,
     * and the handlers last. Until the count changes, the camera builds the
     * same page it always did, out of copies of its own records. */
    wr64(HSFC_PAGE + PAGE_ITEMS, block + OFF_HSFC_ITEMS);
    wr64(HSFC_PAGE + PAGE_EVPTRS, block + OFF_HSFC_EVPTRS);
    wr16(HSFC_PAGE + PAGE_COUNT, (uint16_t)(HSFC_ROWS + TIER_COUNT));

    wr64(HSFC_HANDLERS, (uint64_t)(uintptr_t)&hsfc_entry);
    wr64(QUALITY_HANDLERS + 4u * 8u, (uint64_t)(uintptr_t)&quality_refresh);
    return 0;
}
