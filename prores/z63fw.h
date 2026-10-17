/*
 * The Z 6III firmware C 2.00 as this plugin uses it: addresses, the stock
 * instruction words every patched site must hold, and the firmware
 * functions the plugin calls.
 *
 * Everything here was read out of z63_eg.elf (sha of the module in
 * README.md) and the camera's initialized data image. A plugin that
 * hardcodes addresses is only safe if it refuses to install on anything
 * else, so every site carries the stock word it was built against and
 * install() checks all of them before it changes a single byte
 * (prores.c, fingerprint()).
 *
 * Three kinds of address appear below:
 *   - .data / store RAM (0xa02..., 0xa03...): ordinary writable memory the
 *     plugin stores to directly.
 *   - rodata (0x43c..., 0x42b...): read-only at run time. The plugin only
 *     ever reads these; where the camera must see something else, a copy
 *     goes in the code cave and the pointer that names it (in .data) is
 *     repointed.
 *   - code (0x40d..., 0x409...): the firmware's own text. Writable only
 *     through NLoader's code channel (api->code_patch), 4..64 bytes at a
 *     time inside one cache line.
 */
#ifndef Z63FW_H
#define Z63FW_H

#include "nlr.h"

/* ---- the plugin's own memory -------------------------------------------
 *
 * One block from the loader's allocator holds everything the camera reads
 * from this plugin: the item records and event tables the menu engine
 * builds pages from, the bit-rate table the encoder looks its target up
 * in, the codec-name table the shooting display reads, and the sample
 * description template the MOV file is built from. A resident extension's
 * allocations are kept for the boot, as its code is.
 *
 * The plugin keeps no pointer to it. A resident extension with writable
 * static data would cost a second 2 MiB execution block, so the one
 * callback that needs the block finds it again the way everything else
 * does: through the firmware's own .data, where the video page record
 * names our item array -- which is the start of the block.
 */
#define BLOCK_BYTES        0x2400u
#define OFF_VIDEO_ITEMS    0x0000u /* 46 * 44, and the block's anchor */
#define OFF_VIDEO_EVPTRS   0x0800u /* 46 * 8 */
#define OFF_VIDEO_TABLE    0x0980u /* our row's event records */
#define OFF_NRAW_TABLE     0x0f00u /* the N-RAW row's, with our action */
#define OFF_SUB_ITEMS      0x1480u /* 4 * 44 */
#define OFF_SUB_EVPTRS     0x1540u /* 4 * 8 */
#define OFF_SUB_TABLES     0x1560u /* 4 event tables, one per option row */
#define OFF_SUB_ELEMS      0x1c60u /* 4 * 0x44 */
#define OFF_RATE           0x2100u /* 12 * 24: the bit-rate table in use */
#define OFF_CODECS         0x2240u /* 7 * 16: codec names by file type */
#define OFF_TEMPLATE       0x22c0u /* 0x56: the sample description */
#define FW_RATE_TABLE      0x42b72828u /* 12 rows of 24 bytes, rodata */
#define FW_CODEC_TABLE     0x440899e0u /* {u64 key, u64 name} * 7, rodata */
#define FW_DESC_TEMPLATE   0x42b7145au /* the 'apch' sample description */
#define DESC_BYTES         0x56u

/* ---- the video recording menu (window 0x10a) ---------------------------- */
#define VIDEO_PAGE         0xa028cae10ull /* .data: the page record */
#define VIDEO_HANDLERS     0xa028f55e0ull /* .data: five handler pointers */
#define VIDEO_ITEMS        0x43cb31e8u  /* rodata: 45 item records */
#define VIDEO_EVPTRS       0x43cb39a8u  /* rodata: 45 event-table pointers */
#define VIDEO_ROWS         45u
#define VIDEO_NRAW_ROW     9u           /* "Video quality (N-RAW)", 0x31b3 */
#define VIDEO_NRAW_ITEM    0x31b3u
#define VIDEO_OUR_ROW      10u          /* ours goes right after it */
#define VIDEO_REFRESH      0x40595fdcu  /* stock handler slot 4 */

/* ---- the N-RAW quality sub-page (window 0x119), which ours shares ------- */
#define SUB_PAGE           0xa028cb2c0ull /* .data: the page record */
#define SUB_HANDLERS       0xa028f5928ull /* .data: five handler pointers */
#define SUB_ELEMS          0x43cb75d8u  /* rodata: 4 element records */
#define SUB_ITEMS          0x43cb76e8u  /* rodata: 2 item records */
#define SUB_EVPTRS         0x43cb7740u  /* rodata: 2 event-table pointers */
#define SUB_ROWS           2u
#define SUB_ENTRY          0x4058fc5cu  /* stock handler slot 0 */
#define SUB_TITLE_CTL      0x82cu       /* the title element's control id */
#define SUB_NRAW_WINDOW    0x119u

/* ---- menu engine details ------------------------------------------------ */
#define ITEM_BYTES         44u
#define ITEM_LABEL         0x00u /* string id, or a runtime text slot */
#define ITEM_FLAGS         0x10u
#define ITEM_VALUE         0x14u
#define ITEM_ID            0x18u
#define ITEM_STYLE         0x1cu
#define PAGE_ELEMS         0x08u
#define PAGE_ITEMS         0x10u
#define PAGE_EVPTRS        0x18u
#define PAGE_COUNT         0x20u /* u16 */
#define WINDOW_CURSOR      0x32u /* u16, relative to the scroll top */
#define WINDOW_PAGE        0x50u
#define WINDOW_SCROLL      0x58u
#define ELEM_BYTES         0x44u
#define ELEM_TYPE          0x04u
#define ELEM_STRING        1u    /* draws a string resource */
#define ELEM_SLOT_TEXT     2u    /* draws a runtime text slot */
#define EVENT_BYTES        32u
#define EVENT_END          0x3f2u /* the terminator every table ends with */
#define EVENT_MYMENU       0x2aeu /* "add to My Menu", refused on our rows */
#define NAV_STAY           0x712u
#define VALUE_WIDGET       0x4dbeu

/* Item flags. The stock value row (0x0010104d) shows a label from the
 * string table and a value the page's refresh handler stores in a text
 * slot; bit 9 moves the label into a slot too, which is the combination
 * NLoader's own Setup row uses (0x101240) and the one QEMU drew as a value
 * row with both texts from slots (research/menus.md). */
#define FLAGS_LABEL_SLOT   0x200u
#define FLAGS_VALUE_SLOT   0x009u  /* bits 0 + 3: the value is a text slot */
#define FLAGS_MARK         0x040u  /* the > mark */

/* Runtime text slots. The loader borrows 180..251 for its own page; the
 * stock image writes no constant slot above 189. These eight are the
 * plugin's, written once at install and whenever the choice changes. */
#define SLOT_VIDEO_LABEL   252u
#define SLOT_VIDEO_VALUE   253u
#define SLOT_OPT0          254u /* 254..257: the four grades */
#define SLOT_SUB_TITLE     258u
#define TEXT_STYLE_LABEL   0x200u
#define TEXT_STYLE_VALUE   4u
#define TEXT_STYLE_TITLE   3u

/* Item ids for the rows the plugin adds: inside the range the engine's
 * per-id visibility table covers (0x2988..0x4daf), used by no item record
 * of the module, and -- so that no stock code path can mean one of them --
 * named by no instruction immediate and by no aligned word anywhere in the
 * image. */
#define ITEM_VIDEO_ROW     0x317du
#define ITEM_OPT_0         0x3cd5u
#define ITEM_OPT_1         0x3cd7u
#define ITEM_OPT_2         0x3cd9u
#define ITEM_OPT_3         0x3cebu

/* ---- the ProRes record service (sub_40d22760) --------------------------- */
/*
 * Encoding class 0 (the ProRes 422 file type) takes the hardware type from
 * a word of .data nothing in the image writes -- so it is 0, "422 HQ",
 * always -- and refuses anything but 0 and 1. The driver below it accepts
 * 0..7 and sends 0..3 through the 4:2:2 setup, which is where LT and Proxy
 * live. Two patches open that path: the load reads the plugin's stored
 * choice instead of the unused word, and the compare lets 0..3 through.
 * Class 1 (ProRes RAW) keeps reading the stock word, so RAW is untouched.
 */
#define TYPE_ADDR_SITE     0x40d229f4u /* MOVZ X8,#0xd68 / (X10's) / MOVK X8,#0x332,LSL#16 */
#define TYPE_ADDR_STOCK_0  0xd281ad08u
#define TYPE_ADDR_STOCK_1  0xd281bd8au /* MOVZ X10,#0xdec: kept as it is */
#define TYPE_ADDR_STOCK_2  0xf2a06648u
#define TYPE_LOAD_SITE     0x40d22a14u /* LDR W8,[X8] / STR W9,[X10] / CMP W8,#2 */
#define TYPE_LOAD_STOCK_0  0xb9400108u
#define TYPE_LOAD_STOCK_1  0xb9000149u /* STR W9,[X10]: kept as it is */
#define TYPE_LOAD_STOCK_2  0x7100091fu
#define TYPE_LOAD_NEW_0    0x39400108u /* LDRB W8,[X8] */
#define TYPE_LOAD_NEW_2    0x7100111fu /* CMP W8,#4 */

/* The bit-rate lookup: one table for every consumer (recording, remaining
 * time). Two sites build its address into X9 as MOVZ + three MOVKs, which
 * is what names the plugin's copy instead. The first site's MOVZ is the
 * last word of its cache line, so it takes two calls; the second fits in
 * one. */
#define RATE_SITE_A        0x40d223bcu /* MOVZ X9,#0x2828 */
#define RATE_SITE_A_HIGH   0x40d223c0u /* MOVK #0x42b7,LSL#16 .. LSL#48 */
#define RATE_SITE_B        0x40d22540u /* the same five words, one line */
#define RATE_STOCK_MOVZ    0xd2850509u
#define RATE_STOCK_MOVK    0xf2a856e9u /* MOVK X9,#0x42b7,LSL#16 */
#define RATE_ROWS          12u
#define RATE_ROW_BYTES     24u
#define RATE_RATE_OFF      0x10u /* u64 bits per second, a whole Mibit/s */

/* The sample description the MOV file carries. The builder copies a
 * 0x56-byte 'apch' template out of rodata; the plugin keeps a copy of its
 * own, with the chosen grade's FourCC and encoder name, and the four
 * instructions that name the template -- one cache line, one call -- name
 * the copy instead. Nothing of this plugin runs while a file is written. */
#define TEMPLATE_SITE      0x40cefd10u
#define TEMPLATE_STOCK_0   0xd2828b41u /* MOVZ X1,#0x145a */
#define TEMPLATE_STOCK_1   0xf2a856e1u /* MOVK X1,#0x42b7,LSL#16 */
#define TEMPLATE_STOCK_2   0x52800ac2u /* MOVZ W2,#0x56 -- kept */
#define TEMPLATE_STOCK_3   0xf2c00001u /* MOVK X1,#0,LSL#32 */
#define DESC_FOURCC        0x04u
#define DESC_NAME_LEN      0x31u /* a 32-byte Pascal string: length then text */
#define DESC_NAME          0x32u
#define DESC_NAME_END      0x51u /* the last byte of the 32-byte field */

/* Playback: the demuxer accepts one FourCC. All four grades share the
 * first three characters, so the compare becomes one on the top 24 bits. */
#define PARSE_SITE         0x40ceb320u
#define PARSE_STOCK_0      0x528c6d08u /* MOVZ W8,#0x6368 */
#define PARSE_STOCK_1      0x72ac2e08u /* MOVK W8,#0x6170,LSL#16 */
#define PARSE_STOCK_2      0x6b08001fu /* CMP W0,W8 */
#define PARSE_NEW_0        0x528e0c68u /* MOVZ W8,#0x7063 */
#define PARSE_NEW_1        0x72a00c28u /* MOVK W8,#0x0061,LSL#16 */
#define PARSE_NEW_2        0x6b40211fu /* CMP W8,W0,LSR#8 */

/* The codec name the shooting display shows, from a table keyed by the
 * file type. Its address is built into X21 by four instructions
 * interleaved with another register's, all inside one cache line. */
#define CODEC_SITE         0x405198c0u /* MOVZ X21,#0x99e0 */
#define CODEC_SITE_WORDS   8u
#define CODEC_STOCK_MOVZ   0xd2933c15u
#define CODEC_STOCK_MOVK   0xf2a88115u /* MOVK X21,#0x4408,LSL#16 */
#define CODEC_ROWS         7u
#define CODEC_PRORES_KEY   4u

/* ---- where the choice is stored ----------------------------------------
 *
 * Group 0x0f of the camera's settings store holds the movie items; item
 * 0x0f0b (WbMovieSameStill) is one byte at the start of a four-byte slot
 * and the three bytes behind it are padding the store carries but no item
 * claims. The grade is the first of them (HERAW keeps its choice in the
 * second), so it is saved and restored with the bank the user is shooting
 * (0x2b0 apart; bank A is the one the menus edit).
 */
#define STORE_PRORES_GRADE 0xa03f0aef5ull /* 0..3: HQ, 422, LT, Proxy */
#define STORE_ANCHOR       0xa03f0aef4ull /* item 0x0f0b itself, read to check */

/* The runtime text slots the menu engine draws from: 300 records of 24
 * bytes, each naming the buffer the writer copies into. A slot no stock
 * path ever wrote may still have no buffer, and the writer would copy to
 * address zero, so every slot is checked before it is used. */
#define TEXT_SLOTS         0xa03425680ull
#define TEXT_SLOT_BYTES    24u
#define TEXT_SLOT_BUFFER   8u
#define TEXT_SLOT_COUNT    300u

/* ---- the settings the plugin reads ------------------------------------- */
#define SETTING_MOVIE_FILE_TYPE 0x06000f06u /* 4 = ProRes 422 HQ 10-bit */
#define FILE_TYPE_PRORES_422    4u

/* ---- firmware functions ------------------------------------------------- */
#define FW(type, address) ((type)(uintptr_t)(address))

typedef uint32_t (*fw_setting_get_t)(uint32_t tagged_id);
typedef uint32_t (*fw_setting_set_t)(uint32_t tagged_id, uint32_t value);
typedef void *(*fw_widget_find_t)(uint32_t id, void *container);
typedef void *(*fw_row_value_t)(void *window, uint32_t item_id, uint32_t kind);
typedef void (*fw_text_store_t)(const char *text, uint32_t slot, uint32_t style);
typedef void (*fw_text_bind_t)(void *widget, uint32_t slot);
typedef void (*fw_widget_show_t)(void *widget, uint32_t visible);
typedef uint32_t (*fw_entry_guard_t)(void);
typedef void (*fw_set_cursor_t)(void *window, uint32_t item_id);
typedef uint64_t (*fw_set_mark_t)(void *window, uint32_t item_id, uint32_t marked);
typedef uint64_t (*fw_handler_t)(void *window);

#define fw_setting_get FW(fw_setting_get_t, 0x403ffcd0u)
#define fw_setting_set FW(fw_setting_set_t, 0x403ffcd4u)
#define fw_widget_find FW(fw_widget_find_t, 0x403ada94u)
#define fw_row_value   FW(fw_row_value_t, 0x4052c7a0u)
#define fw_text_store  FW(fw_text_store_t, 0x404066c8u)
#define fw_text_bind   FW(fw_text_bind_t, 0x4040a1acu)
#define fw_widget_show FW(fw_widget_show_t, 0x40409f74u)
#define fw_entry_guard FW(fw_entry_guard_t, 0x403bf168u)
#define fw_set_cursor  FW(fw_set_cursor_t, 0x403c0f8cu)
#define fw_set_mark    FW(fw_set_mark_t, 0x403c0ffcu)

/* Event-record actions the plugin reuses: a guard that always refuses (so
 * a cloned record can be disabled in place) and the no-op action stock
 * rows use when the transition alone does the work. */
#define FW_GUARD_NEVER     0x403c2d0cu
#define FW_ACTION_NOTHING  0x403c2c5cu

/* The navigation block every action is called with (core/include/nlr/menu.h
 * describes the same three words). */
struct fw_nav {
    void *window;
    uint32_t code;
    uint32_t next;
};

/* ---- small helpers ------------------------------------------------------ */
static inline uint32_t rd32(uint64_t address)
{
    return *(volatile const uint32_t *)(uintptr_t)address;
}

static inline uint16_t rd16(uint64_t address)
{
    return *(volatile const uint16_t *)(uintptr_t)address;
}

static inline uint64_t rd64(uint64_t address)
{
    return *(volatile const uint64_t *)(uintptr_t)address;
}

static inline uint8_t rd8(uint64_t address)
{
    return *(volatile const uint8_t *)(uintptr_t)address;
}

static inline void wr8(uint64_t address, uint8_t value)
{
    *(volatile uint8_t *)(uintptr_t)address = value;
}

static inline void wr16(uint64_t address, uint16_t value)
{
    *(volatile uint16_t *)(uintptr_t)address = value;
}

static inline void wr32(uint64_t address, uint32_t value)
{
    *(volatile uint32_t *)(uintptr_t)address = value;
}

static inline void wr64(uint64_t address, uint64_t value)
{
    *(volatile uint64_t *)(uintptr_t)address = value;
}

/* The block, found again without a pointer of this program's own: the
 * video page record names our item array, and that array is the block's
 * first bytes. Only valid once menu_install has run. */
static inline uint64_t plugin_block(void)
{
    return rd64(VIDEO_PAGE + PAGE_ITEMS);
}

/* The two instructions that build a 64-bit constant into a register. */
static inline uint32_t movz_x(uint32_t rd, uint32_t imm16)
{
    return 0xd2800000u | (imm16 << 5) | rd;
}

static inline uint32_t movk_x(uint32_t rd, uint32_t imm16, uint32_t shift)
{
    return 0xf2800000u | ((shift / 16u) << 21) | (imm16 << 5) | rd;
}

/* The grade the user chose, clamped: nothing may hand the encoder a type
 * the 4:2:2 setup does not accept. */
static inline uint32_t prores_grade(void)
{
    return rd8(STORE_PRORES_GRADE) & 3u;
}

#endif /* Z63FW_H */
