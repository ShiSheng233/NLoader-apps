/*
 * The rows, in the camera's own menus.
 *
 * The menu engine reads four things when it builds a page, all of them
 * named by a page record in .data: the element list, the item records
 * (44 bytes each), one event table per row, and the row count. Rows are
 * found by item id and never by index, so a row inserted in the middle
 * changes no stock row's behaviour (NLoader's research/menus.md, QEMU
 * 2026-10-16). And handlers are five function pointers in .data.
 *
 * So the plugin needs no patched instruction here at all:
 *   - VIDEO RECORDING MENU gets a 46th row, right after "Video quality
 *     (N-RAW)": copies of the 45 stock item records and their event
 *     pointers live in the plugin's own block with ours inserted, and the
 *     page record names the copies.
 *   - That row opens the N-RAW quality page's window with a page record
 *     the plugin swapped for a four-row one -- the swap is the row's own
 *     event action, which the dispatcher runs before it builds the page,
 *     and the N-RAW row's action (a copy of its table, with one action
 *     changed) puts the stock record back. One window, two pages.
 *   - Custom setting d3, Pre-Release Capture options, gets a third row:
 *     an ON/OFF switch like the camera's own.
 *   - Two handler pointers are wrapped: the video menu's refresh, so the
 *     row shows the grade at its right, and d3's, so the switch shows its
 *     state. Both call the stock handler first and keep its result.
 *
 * All of it lives in one block from the loader's allocator. None of the
 * callbacks below keeps a pointer to it -- a resident extension with
 * writable static data would cost a second execution block -- so the one
 * that needs it reads the video page record, which names the block's
 * first bytes (plugin_block()).
 */
#include "menu.h"
#include "block.h"
#include "codec.h"

static const uint32_t OPT_ID[GRADES] = {
    ITEM_OPT_0, ITEM_OPT_1, ITEM_OPT_2, ITEM_OPT_3
};

#define TITLE_TEXT "Video quality (ProRes)"
#define D3_TEXT    "Pre-release RAW (HE)"

/* ---- small readers ------------------------------------------------------ */

/* Records of an event table, terminator included. */
static uint32_t table_len(uint64_t table)
{
    for (uint32_t n = 0; n < 80u; n++)
        if ((uint32_t)rd64(table + n * EVENT_BYTES) == EVENT_END)
            return n + 1u;
    return 0;
}

/* The item id of the row the cursor is on, the way the stock handlers
 * read it: the page record behind the window, its item array, and the
 * cursor plus the scroll top. */
static uint32_t selected_item(void *window)
{
    uint64_t w = (uint64_t)(uintptr_t)window;
    uint64_t page = rd64(w + WINDOW_PAGE);
    uint64_t items = *(volatile const uint64_t *)(uintptr_t)(page + PAGE_ITEMS);
    uint32_t row = rd16(w + WINDOW_CURSOR) + rd16(w + WINDOW_SCROLL);
    return *(volatile const uint32_t *)(uintptr_t)(items + row * ITEM_BYTES +
                                                   ITEM_ID);
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
    if (!slot_ready(SLOT_VIDEO_LABEL) || !slot_ready(SLOT_VIDEO_VALUE) ||
        !slot_ready(SLOT_SUB_TITLE) || !slot_ready(SLOT_D3_LABEL))
        return 0;
    for (uint32_t i = 0; i < GRADES; i++)
        if (!slot_ready(SLOT_OPT0 + i))
            return 0;
    return 1;
}

void menu_show_grade(void)
{
    const char *text = "";
    if (fw_setting_get(SETTING_MOVIE_FILE_TYPE) == FILE_TYPE_PRORES_422)
        text = codec_grade_short(prores_grade());
    fw_text_store(text, SLOT_VIDEO_VALUE, TEXT_STYLE_VALUE);
}

/* ---- the page behind the video row -------------------------------------
 *
 * Window 0x119 draws whatever its page record says. These two actions are
 * the event actions of the two rows that open it, and the dispatcher runs
 * an action before the transition, so each row decides which page the
 * window is about to build. The record is three pointers and a count in
 * .data; the count goes last, so a build that raced the swap would see a
 * two-row page of our four-row arrays, whose first two rows are rows of
 * the same shape.
 */
static void open_prores_page(struct fw_nav *nav)
{
    (void)nav;
    uint64_t block = plugin_block();
    wr64(SUB_PAGE + PAGE_ELEMS, block + OFF_SUB_ELEMS);
    wr64(SUB_PAGE + PAGE_ITEMS, block + OFF_SUB_ITEMS);
    wr64(SUB_PAGE + PAGE_EVPTRS, block + OFF_SUB_EVPTRS);
    wr16(SUB_PAGE + PAGE_COUNT, (uint16_t)GRADES);
}

static void open_nraw_page(struct fw_nav *nav)
{
    (void)nav;
    wr64(SUB_PAGE + PAGE_ELEMS, SUB_ELEMS);
    wr64(SUB_PAGE + PAGE_ITEMS, SUB_ITEMS);
    wr64(SUB_PAGE + PAGE_EVPTRS, SUB_EVPTRS);
    wr16(SUB_PAGE + PAGE_COUNT, (uint16_t)SUB_ROWS);
}

/* OK on one of the four grades: store it, and let the bit-rate table and
 * the shooting display follow. The transition closes the page, and the
 * video menu's refresh then shows the new value. */
static void pick_grade(struct fw_nav *nav)
{
    uint32_t id = selected_item(nav->window);
    for (uint32_t grade = 0; grade < GRADES; grade++) {
        if (OPT_ID[grade] != id)
            continue;
        wr8(STORE_PRORES_GRADE, (uint8_t)grade);
        codec_apply(plugin_block(), grade);
        menu_show_grade();
        return;
    }
}

/* The page's entry handler: ours when the four-row record is in place,
 * the camera's otherwise. It gives the title its text (the element is a
 * runtime text slot in our copy, which the builder creates hidden) and
 * puts the cursor and the radio mark on the stored grade -- the same two
 * calls, with the same guard, as the stock entry. */
static uint64_t sub_entry(void *window)
{
    if (rd16(SUB_PAGE + PAGE_COUNT) != (uint16_t)GRADES)
        return FW(fw_handler_t, SUB_ENTRY)(window);
    void *container = (void *)((uint8_t *)window + 0x18);
    void *title = fw_widget_find(SUB_TITLE_CTL, container);
    if (title) {
        fw_text_store(TITLE_TEXT, SLOT_SUB_TITLE, TEXT_STYLE_TITLE);
        fw_text_bind(title, SLOT_SUB_TITLE);
        fw_widget_show(title, 1);
    }
    uint32_t id = OPT_ID[prores_grade()];
    if (fw_entry_guard() != 1u)
        fw_set_cursor(window, id);
    return fw_set_mark(window, id, 1u);
}

/* ---- the wrapped refresh handlers --------------------------------------- */

static uint64_t video_refresh(void *window)
{
    uint64_t result = FW(fw_handler_t, VIDEO_REFRESH)(window);
    void *value = fw_row_value(window, ITEM_VIDEO_ROW, VALUE_WIDGET);
    if (value) {
        menu_show_grade();
        fw_text_bind(value, SLOT_VIDEO_VALUE);
    }
    return result;
}

static uint64_t d3_refresh(void *window)
{
    uint64_t result = FW(fw_handler_t, D3_REFRESH)(window);
    void *value = fw_row_value(window, ITEM_D3_ROW, VALUE_WIDGET);
    if (value)
        fw_switch_set(value, pre_raw_on());
    return result;
}

/* OK on the switch: the camera's switches flip and stay on the page. The
 * stored byte is this plugin's instead of a store item, and the rest --
 * the toggle's new pictures, the row marked, the page repainted -- is the
 * tail of the camera's own switch action, called with the state. The item
 * record's value word follows too, so a page built again without a
 * refresh shows the same thing. */
static void toggle_pre_raw(struct fw_nav *nav)
{
    uint32_t on = pre_raw_on() ? 0u : 1u;
    wr8(STORE_PRE_RAW, (uint8_t)on);
    uint64_t record = plugin_block() - OFF_VIDEO_ITEMS + OFF_D3_ITEMS +
                      D3_ROWS * ITEM_BYTES;
    *(volatile uint32_t *)(uintptr_t)(record + ITEM_VALUE) =
        on ? SWITCH_ON : SWITCH_OFF;
    fw_switch_apply(nav->window, on);
}

/* ---- building the copies ------------------------------------------------ */

/* One event table, cloned record by record. `target` names the records to
 * change (their transition's window), `action` is what they do instead and
 * `next` the window they go to (0 keeps the record's own). The My Menu
 * record, which would let the user pin a row whose id means nothing
 * without this plugin, is disabled in place by a guard that refuses. */
static void clone_table(uint64_t dst, uint64_t src, uint32_t records,
                        uint64_t target, uint64_t action, uint64_t next)
{
    for (uint32_t n = 0; n < records; n++) {
        uint64_t record[4];
        for (uint32_t w = 0; w < 4u; w++)
            record[w] = rd64(src + n * EVENT_BYTES + w * 8u);
        if (record[0] == EVENT_MYMENU)
            record[2] = FW_GUARD_NEVER;
        else if (record[3] == target) {
            record[1] = action;
            if (next)
                record[3] = next;
        }
        for (uint32_t w = 0; w < 4u; w++)
            wr64(dst + n * EVENT_BYTES + w * 8u, record[w]);
    }
}

static void item_from(uint8_t *record, uint64_t src)
{
    for (uint32_t n = 0; n < ITEM_BYTES; n++)
        record[n] = rd8(src + n);
}

static void item_put(uint64_t dst, const uint8_t *record)
{
    for (uint32_t n = 0; n < ITEM_BYTES; n++)
        *(volatile uint8_t *)(uintptr_t)(dst + n) = record[n];
}

static void item_set(uint8_t *record, uint32_t at, uint32_t value)
{
    record[at] = (uint8_t)value;
    record[at + 1] = (uint8_t)(value >> 8);
    record[at + 2] = (uint8_t)(value >> 16);
    record[at + 3] = (uint8_t)(value >> 24);
}

static uint32_t item_get(uint64_t src, uint32_t at)
{
    return rd32(src + at);
}

/* The video menu: 46 item records and 46 event pointers, ours at index 10.
 * The N-RAW row keeps its place and its table is replaced by a copy whose
 * open action restores the stock sub-page. */
static void video_arrays(uint64_t block)
{
    uint64_t nraw_table = rd64(VIDEO_EVPTRS + VIDEO_NRAW_ROW * 8u);
    uint32_t records = table_len(nraw_table);

    clone_table(block + OFF_VIDEO_TABLE, nraw_table, records, SUB_NRAW_WINDOW,
                (uint64_t)(uintptr_t)&open_prores_page, 0);
    clone_table(block + OFF_NRAW_TABLE, nraw_table, records, SUB_NRAW_WINDOW,
                (uint64_t)(uintptr_t)&open_nraw_page, 0);

    uint64_t items = block + OFF_VIDEO_ITEMS, evptrs = block + OFF_VIDEO_EVPTRS;
    for (uint32_t row = 0, out = 0; row <= VIDEO_ROWS; row++, out++) {
        if (row == VIDEO_OUR_ROW) {
            uint64_t anchor = VIDEO_ITEMS + VIDEO_NRAW_ROW * ITEM_BYTES;
            uint8_t record[ITEM_BYTES];
            item_from(record, anchor);
            item_set(record, ITEM_LABEL, SLOT_VIDEO_LABEL);
            item_set(record, ITEM_FLAGS,
                     item_get(anchor, ITEM_FLAGS) | FLAGS_LABEL_SLOT);
            item_set(record, ITEM_VALUE, SLOT_VIDEO_VALUE);
            item_set(record, ITEM_ID, ITEM_VIDEO_ROW);
            item_put(items + out * ITEM_BYTES, record);
            wr64(evptrs + out * 8u, block + OFF_VIDEO_TABLE);
            out++;
        }
        if (row == VIDEO_ROWS)
            break;
        block_copy_fw(items + out * ITEM_BYTES,
                      VIDEO_ITEMS + row * ITEM_BYTES, ITEM_BYTES);
        uint64_t table = rd64(VIDEO_EVPTRS + row * 8u);
        if (row == VIDEO_NRAW_ROW)
            table = block + OFF_NRAW_TABLE;
        wr64(evptrs + out * 8u, table);
    }
}

/* The four-grade page: the stock page's elements with the title turned
 * into a runtime text slot, four option rows cloned from "High quality",
 * and one event table per row whose OK stores the grade. */
static void sub_arrays(uint64_t block)
{
    for (uint32_t n = 0; n < 4u; n++) {
        uint8_t element[ELEM_BYTES];
        uint64_t src = SUB_ELEMS + n * ELEM_BYTES;
        for (uint32_t b = 0; b < ELEM_BYTES; b++)
            element[b] = rd8(src + b);
        if (item_get(src, ELEM_TYPE) == ELEM_STRING) {
            item_set(element, 0, SLOT_SUB_TITLE);
            item_set(element, ELEM_TYPE, ELEM_SLOT_TEXT);
        }
        for (uint32_t b = 0; b < ELEM_BYTES; b++)
            *(volatile uint8_t *)(uintptr_t)(block + OFF_SUB_ELEMS +
                                             n * ELEM_BYTES + b) = element[b];
    }

    uint64_t stock_table = rd64(SUB_EVPTRS);
    uint32_t records = table_len(stock_table);
    uint64_t stock_ok = rd64(stock_table + 5u * EVENT_BYTES + 8u);

    for (uint32_t grade = 0; grade < GRADES; grade++) {
        uint8_t record[ITEM_BYTES];
        item_from(record, SUB_ITEMS);
        item_set(record, ITEM_LABEL, SLOT_OPT0 + grade);
        item_set(record, ITEM_FLAGS,
                 item_get(SUB_ITEMS, ITEM_FLAGS) | FLAGS_LABEL_SLOT);
        item_set(record, ITEM_ID, OPT_ID[grade]);
        item_put(block + OFF_SUB_ITEMS + grade * ITEM_BYTES, record);

        /* The stock rows' OK records all name the same handler; every one
         * of them becomes ours, and the transition (back) is kept. */
        uint64_t table = block + OFF_SUB_TABLES +
                         grade * records * EVENT_BYTES;
        clone_table(table, stock_table, records, 0, 0, 0);
        for (uint32_t n = 0; n < records; n++)
            if (rd64(table + n * EVENT_BYTES + 8u) == stock_ok)
                wr64(table + n * EVENT_BYTES + 8u,
                     (uint64_t)(uintptr_t)&pick_grade);
        wr64(block + OFF_SUB_EVPTRS + grade * 8u, table);
    }
}

/* Pre-Release Capture options: three rows, the third a switch of ours. */
static void d3_arrays(uint64_t block)
{
    uint64_t anchor = D3_ITEMS + D3_ANCHOR_ROW * ITEM_BYTES;
    uint64_t anchor_table = rd64(D3_EVPTRS + D3_ANCHOR_ROW * 8u);
    uint32_t records = table_len(anchor_table);

    clone_table(block + OFF_D3_TABLE, anchor_table, records, D3_SUB_WINDOW,
                (uint64_t)(uintptr_t)&toggle_pre_raw, NAV_STAY);

    for (uint32_t row = 0; row < D3_ROWS; row++) {
        block_copy_fw(block + OFF_D3_ITEMS + row * ITEM_BYTES,
                      D3_ITEMS + row * ITEM_BYTES, ITEM_BYTES);
        wr64(block + OFF_D3_EVPTRS + row * 8u, rd64(D3_EVPTRS + row * 8u));
    }

    uint8_t record[ITEM_BYTES];
    item_from(record, anchor);
    item_set(record, ITEM_LABEL, SLOT_D3_LABEL);
    item_set(record, ITEM_FLAGS, FLAGS_SWITCH_ROW | FLAGS_LABEL_SLOT);
    item_set(record, ITEM_VALUE, pre_raw_on() ? SWITCH_ON : SWITCH_OFF);
    item_set(record, ITEM_ID, ITEM_D3_ROW);
    item_put(block + OFF_D3_ITEMS + D3_ROWS * ITEM_BYTES, record);
    wr64(block + OFF_D3_EVPTRS + D3_ROWS * 8u, block + OFF_D3_TABLE);
}

/* ---- the fingerprint ---------------------------------------------------- */

int menu_fingerprint(void)
{
    /* The three page records still name the module's own arrays and hold
     * the row counts this build read: so nothing else has moved them, and
     * this plugin is not installed already. */
    if (rd64(VIDEO_PAGE + PAGE_ITEMS) != VIDEO_ITEMS ||
        rd64(VIDEO_PAGE + PAGE_EVPTRS) != VIDEO_EVPTRS ||
        rd16(VIDEO_PAGE + PAGE_COUNT) != VIDEO_ROWS)
        return 0;
    if (rd64(SUB_PAGE + PAGE_ELEMS) != SUB_ELEMS ||
        rd64(SUB_PAGE + PAGE_ITEMS) != SUB_ITEMS ||
        rd64(SUB_PAGE + PAGE_EVPTRS) != SUB_EVPTRS ||
        rd16(SUB_PAGE + PAGE_COUNT) != SUB_ROWS)
        return 0;
    if (rd64(D3_PAGE + PAGE_ITEMS) != D3_ITEMS ||
        rd64(D3_PAGE + PAGE_EVPTRS) != D3_EVPTRS ||
        rd16(D3_PAGE + PAGE_COUNT) != D3_ROWS)
        return 0;
    /* The rows ours are cloned from, by item id. */
    if (item_get(VIDEO_ITEMS + VIDEO_NRAW_ROW * ITEM_BYTES, ITEM_ID) !=
            VIDEO_NRAW_ITEM ||
        item_get(SUB_ITEMS, ITEM_ID) != 0x31b4u ||
        item_get(D3_ITEMS + D3_ANCHOR_ROW * ITEM_BYTES, ITEM_ID) != 0x36e5u)
        return 0;
    /* The handlers the wrappers call. */
    if (rd64(VIDEO_HANDLERS + 4u * 8u) != VIDEO_REFRESH ||
        rd64(SUB_HANDLERS) != SUB_ENTRY ||
        rd64(D3_HANDLERS + 4u * 8u) != D3_REFRESH)
        return 0;
    return 1;
}

/* ---- install ------------------------------------------------------------ */

int menu_install(const struct nlr_api *api, uint64_t block)
{
    (void)api;
    /* Every table this plugin hands the engine must terminate, or a build
     * would walk off the end of one. */
    if (!table_len(rd64(VIDEO_EVPTRS + VIDEO_NRAW_ROW * 8u)) ||
        !table_len(rd64(SUB_EVPTRS)) ||
        !table_len(rd64(D3_EVPTRS + D3_ANCHOR_ROW * 8u)))
        return -NLR_API_EINVAL;

    /* The texts first: a row built before its slot was written would draw
     * whatever the slot held. */
    fw_text_store(TITLE_TEXT, SLOT_VIDEO_LABEL, TEXT_STYLE_LABEL);
    fw_text_store(TITLE_TEXT, SLOT_SUB_TITLE, TEXT_STYLE_TITLE);
    fw_text_store(D3_TEXT, SLOT_D3_LABEL, TEXT_STYLE_LABEL);
    for (uint32_t grade = 0; grade < GRADES; grade++)
        fw_text_store(codec_grade_name(grade), SLOT_OPT0 + grade,
                      TEXT_STYLE_LABEL);
    menu_show_grade();

    video_arrays(block);
    sub_arrays(block);
    d3_arrays(block);

    /* Now the .data words the engine reads. The arrays go in before the
     * counts, and the handlers last: until the count changes, the camera
     * builds the same pages it always did out of copies of its own
     * records. */
    wr64(VIDEO_PAGE + PAGE_ITEMS, block + OFF_VIDEO_ITEMS);
    wr64(VIDEO_PAGE + PAGE_EVPTRS, block + OFF_VIDEO_EVPTRS);
    wr16(VIDEO_PAGE + PAGE_COUNT, (uint16_t)(VIDEO_ROWS + 1u));

    wr64(D3_PAGE + PAGE_ITEMS, block + OFF_D3_ITEMS);
    wr64(D3_PAGE + PAGE_EVPTRS, block + OFF_D3_EVPTRS);
    wr16(D3_PAGE + PAGE_COUNT, (uint16_t)(D3_ROWS + 1u));

    wr64(VIDEO_HANDLERS + 4u * 8u, (uint64_t)(uintptr_t)&video_refresh);
    wr64(D3_HANDLERS + 4u * 8u, (uint64_t)(uintptr_t)&d3_refresh);
    wr64(SUB_HANDLERS, (uint64_t)(uintptr_t)&sub_entry);
    return 0;
}
