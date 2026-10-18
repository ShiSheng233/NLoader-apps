/*
 * The Z 6III firmware C 2.00 as HERAW uses it: addresses, the stock words
 * every hooked site must hold, and the firmware functions it calls.
 *
 * Everything here was read out of z63_eg.elf and the camera's initialized
 * data image (see ../prores and README.md for how). A program that
 * hardcodes addresses is only safe if it refuses to install on anything
 * else, so every site carries the words it was built against and the
 * install checks all of them before it changes a single byte.
 *
 * Three kinds of address appear below:
 *   - .data / store RAM (0xa02..., 0xa03..., 0xa04...): ordinary memory the
 *     program reads and, for page records and handler pointers, writes.
 *   - rodata (0x43e..., 0x42b..., 0x441...): read only.
 *   - code (0x40..., 0x41...): the firmware's own text. Four functions are
 *     detoured through NLoader's code channel (api->code_hook), which
 *     checks the four words it replaces and gives back a continuation.
 */
#ifndef Z63FW_H
#define Z63FW_H

#include "nlr.h"

#define FW(type, address) ((type)(uintptr_t)(address))

/* ---- the program's own memory -------------------------------------------
 *
 * One block from the loader's allocator: the HSFC page's rows and event
 * tables the menu engine reads, the state the hooks keep, a record per
 * ring frame, and the NEF's head while it is written. A resident
 * extension's allocations are kept for the boot, as its code is.
 *
 * The program keeps no pointer to it -- a resident extension with
 * writable static data would cost a second 2 MiB execution block -- and
 * finds it again through the firmware's own .data: the HSFC page record
 * names our item array, which is the start of the block.
 */
#define BLOCK_BYTES        0x19600u
#define OFF_HSFC_ITEMS     0x0000u  /* 7 * 44: the page's rows, and the anchor */
#define OFF_HSFC_EVPTRS    0x0180u  /* 7 * 8 */
#define OFF_HSFC_JPEG      0x0200u  /* the JPEG rows' event table, copied */
#define OFF_HSFC_RAW       0x0700u  /* the HE RAW rows', one each */
#define HSFC_TABLE_MAX     40u      /* records each, 0x500 bytes */
#define HSFC_TABLE_BYTES   0x500u
#define OFF_STATE          0x2000u  /* 0x400: what the hooks keep */
#define STATE_MAX          0x400u
#define OFF_PARTS          0x2400u  /* 0x100: four part descriptors */
#define OFF_FRAMES         0x2600u  /* 0x5000: a record per ring frame */
#define FRAMES_BYTES       0x5000u
/* More records than the ring has slots (225 FX frames): a frame waiting
 * for the writer holds a slot, so no record is reused while its frame
 * still waits. 64 was not enough for C60, whose pre-release hands up to
 * 60 frames to the pipeline at once (body, 2026-10-17). */
#define FRAME_MAX          256u
#define OFF_NEF            0x7600u  /* the NEF's head: a copy of the JPEG's */
#define NEF_HEADER_MAX     0x12000u /* Exif block and our directories */
_Static_assert(OFF_HSFC_RAW + 5u * HSFC_TABLE_BYTES <= OFF_STATE,
               "the event tables outgrew their room in the block");
_Static_assert(OFF_NEF + NEF_HEADER_MAX <= BLOCK_BYTES,
               "the NEF head outgrew the block");

/* ---- Image quality (HSFC): its options page (window 0x30) ---------------
 * Two rows, JPEG fine (0x2ecf) and JPEG normal (0x2ed0), one event table
 * for both; OK calls sub_40612F74, which stores item 0x04000F64 = (row !=
 * fine), and goes back. The entry (slot 0) puts the cursor and the radio
 * mark on the stored choice. The plugin's copy has five more, HE RAW at
 * 3.0 to 7.0 bits per pixel. */
#define HSFC_PAGE          0xa028d86f0ull
#define HSFC_HANDLERS      0xa028f36c8ull
#define HSFC_ITEMS         0x43eac474u  /* rodata: 2 item records */
#define HSFC_EVPTRS        0x43eac4d0u  /* rodata: 2 event-table pointers */
#define HSFC_ROWS          2u
#define HSFC_FINE_ITEM     0x2ecfu
#define HSFC_NORMAL_ITEM   0x2ed0u
#define HSFC_ENTRY         0x40607b60u  /* stock handler slot 0 */
#define HSFC_PICK          0x40612f74u  /* the rows' OK action */
/* ... and the page it is opened from, Image quality settings (window
 * 0x2e), whose refresh (slot 4) writes the row's value. */
#define QUALITY_HANDLERS   0xa028f3678ull
#define QUALITY_REFRESH    0x406077d8u
#define QUALITY_HSFC_ROW   0x2eceu
/* The row's value is a string of the image-quality symbol font (FINE
 * 0xd33, NORM 0xb3b); a text slot drawn there comes out as other symbols.
 * "RAW" in that font is what the Image quality row shows for RAW alone:
 * entry 13 of its table (sub_40607878, 0x06000502 == 1). */
#define QUALITY_STRINGS    0x42bdb3acu
#define QUALITY_RAW_INDEX  13u
#define QUALITY_RAW_STRING 0xd37u

/* ---- menu engine details ------------------------------------------------ */
#define ITEM_BYTES         44u
#define ITEM_LABEL         0x00u /* string id, or a runtime text slot */
#define ITEM_FLAGS         0x10u
#define ITEM_ID            0x18u
#define PAGE_ITEMS         0x10u
#define PAGE_EVPTRS        0x18u
#define PAGE_COUNT         0x20u /* u16 */
#define EVENT_BYTES        32u
#define EVENT_END          0x3f2u /* the terminator every table ends with */
#define EVENT_MYMENU       0x2aeu /* "add to My Menu", refused on our row */
#define VALUE_WIDGET       0x4dbeu
#define FLAGS_LABEL_SLOT   0x200u /* the label is a runtime text slot */
#define TEXT_STYLE_LABEL   0x200u

/* The runtime text slots the menu engine draws from: 300 records of 24
 * bytes, each naming the buffer the writer copies into. A slot no stock
 * path ever wrote may still have no buffer, and the writer would copy to
 * address zero, so every slot is checked before it is used. */
#define TEXT_SLOTS         0xa03425680ull
#define TEXT_SLOT_BYTES    24u
#define TEXT_SLOT_BUFFER   8u
#define TEXT_SLOT_COUNT    300u

/* The text slots and item ids of the HE RAW rows, one per tier. The slots
 * are past PRORES's eight (252..258; the loader borrows 180..251) and
 * have buffers (260 on are never written by the camera, in the emulator).
 * The ids are in the range the engine's per-id visibility table covers,
 * visible there, used by no item record of the module, and named by no
 * instruction immediate or aligned word of the image or of a booted
 * camera's RAM (hello/itemids.py and a scan, 2026-10-17); 0x317f is the
 * first release's HE RAW row, kept for tier 1. */
#define SLOT_HSFC_FIRST    259u     /* 259..263 */
#define ITEM_HSFC_TIERS    { 0x317fu, 0x3ce9u, 0x3cefu, 0x3cf7u, 0x45dfu }

/* Event-record guard that always refuses, so a cloned record can be
 * disabled in place. */
#define FW_GUARD_NEVER     0x403c2d0cu

/* ---- high-speed frames -------------------------------------------------
 *
 * Every frame of a high-speed burst (C15/C30/C60, pre-release included) is
 * TENC-compressed into a pool-63 slot as HE RAW (TicoRAW, 3.0 bpp: FX
 * 9,186,816 bytes), decoded by TDEC for the JPEG, and its slot released.
 */
/* The drain calls this for every history frame it submits for
 * development, with the frame's request block in x0 and nothing else
 * calls it (two call sites, both in sub_40f5a028). Its first four
 * instructions are position-independent, so the loader's code channel can
 * detour the entry and give back a continuation. */
#define SUBMIT_SITE    0x40f7f1b0u
#define SUBMIT_STOCK_0 0xd10183ffu /* SUB SP,SP,#0x60 */
#define SUBMIT_STOCK_1 0xa90457feu /* STP X30,X21,[SP,#0x40] */
#define SUBMIT_STOCK_2 0xa9054ff4u /* STP X20,X19,[SP,#0x50] */
#define SUBMIT_STOCK_3 0xd28d98a9u /* MOVZ X9,#0x6cc5 */
#define R_SLOT_E           0x6c0u /* the ring slot of this frame, in R */
#define R_SLOT_M           0x710u /* its metadata block */
#define E_SHAPE            0x38u  /* two u16: the sensor readout size */
#define E_PAYLOAD          0x40u  /* 512-aligned, the TENC output */

/* A frame's ring slot goes back to pool 63 through sub_4103E664 (the pool
 * free; sub_41045758 is a branch to it) well before the card writer gets
 * to the frame: on the body every one of 31 C30 frames had lost its slot
 * by its JPEG's turn, and the request's own cleanup (sub_40E84B0C) never
 * saw one -- the TDEC step had already let it go. The plugin therefore
 * hooks the pool free itself: a pool-63 slot that belongs to a frame whose
 * NEF is still to come is not freed but kept, and given back through the
 * same function once the NEF is on the card. The descriptor's +0x10 is its
 * pool, which the function's fourth instruction loads. */
#define POOL_FREE_SITE     0x4103e664u
#define POOL_FREE_STOCK_0  0xf81e0ffeu /* STR X30,[SP,#-0x20]! */
#define POOL_FREE_STOCK_1  0xa9014ff4u /* STP X20,X19,[SP,#0x10] */
#define POOL_FREE_STOCK_2  0xaa0003f3u /* MOV X19,X0 */
#define POOL_FREE_STOCK_3  0xb9401001u /* LDR W1,[X0,#0x10] */
#define SLOT_POOL          0x10u      /* the descriptor's pool */
#define RING_POOL          63u
#define KEEP_LIMIT_MS      60000u /* a kept slot is given back after this */
#define M_SEQUENCE         0x120u /* the frame's number in the burst */
#define M_BLACK            0x2b4u /* four black levels, u16 */

/* ---- how many bits the ring keeps ---------------------------------------
 *
 * TENC compresses to a fixed size: rate * width * height / 80 bytes, 512
 * aligned, the rate (tenths of a bit per pixel) from a nine-entry table
 * {10,20,30,40,50,60,70,15,25}. The index is chosen when the capture lane
 * starts: sub_40E6C038 stores the lane type at 0xA03D272E0 and
 * sub_40E99328's answer at +0xB8 -- 2 (3.0 bpp, HE) for the high-speed
 * lane, 12 -- and the TENC set-up (sub_40E67CFC) reads it there. The ring
 * slots for that lane are allocated right after (sub_40E6C0C4, then again
 * as frames go), each the size the slot table names for the mode.
 *
 * TDEC, which decodes a ring frame for its JPEG, has its own copy of the
 * table, read by sub_40E675C0 from its set-up block's +0x98. The
 * high-speed lane's TDEC stage (sub_40E896CC, which decodes nothing but
 * ring slots, R+0x6C0) builds that block on its stack with index 2 as an
 * immediate (0x40E8974C) and hands it to sub_40E673EC, which calls the
 * set-up loc_40E67250 from 0x40E67400 -- its only caller. Other lanes set
 * TDEC up with index 3 (sub_40E6ABA4, 0xA03D273A0) or one from the frame
 * (sub_40E89C74), so the table itself stays as it is.
 *
 * So the plugin, for a tier above HE:
 *   - when a lane start calls the ring allocation sub_40E6C0C4 (hooked;
 *     the latch is done by then, the allocation and the TENC set-up are
 *     not), on the high-speed lane: the slot table, then the index at
 *     +0xB8;
 *   - at the TDEC set-up (hooked), called back from the high-speed stage:
 *     the block's +0x98, from 2 to the tier's index.
 * Nothing changes while a lane runs: a new tier waits for the next lane
 * start with the ring empty (hsfc.c). A hook's 16 bytes must not cross a
 * 64-byte line, which the latch itself (0x40E6C038) does. */
/* The ring allocation, called by the two lane starts right after the
 * latch (BL sub_40E6C038 at 0x40E79B20 / 0x40E6B3C8, BL here at 0x40E79B78
 * / 0x40E6B3D4) and once a frame by sub_40E75598 (0x40E75614), which the
 * hook leaves alone: it knows its caller by the return address. */
#define RING_ALLOC_SITE    0x40e6c0c4u
#define RING_ALLOC_STOCK_0 0xd10283ffu /* SUB SP,SP,#0xa0 */
#define RING_ALLOC_STOCK_1 0xa9056ffeu /* STP X30,X27,[SP,#0x50] */
#define RING_ALLOC_STOCK_2 0xa90667fau /* STP X26,X25,[SP,#0x60] */
#define RING_ALLOC_STOCK_3 0xa9075ff8u /* STP X24,X23,[SP,#0x70] */
#define LANE_LATCH_CALL_A  0x40e79b20u
#define LANE_LATCH_A_STOCK 0x97ffc946u /* BL sub_40E6C038 */
#define LANE_ALLOC_CALL_A  0x40e79b78u
#define LANE_ALLOC_A_STOCK 0x97ffc953u /* BL sub_40E6C0C4 */
#define LANE_LATCH_CALL_B  0x40e6b3c8u
#define LANE_LATCH_B_STOCK 0x9400031cu
#define LANE_ALLOC_CALL_B  0x40e6b3d4u
#define LANE_ALLOC_B_STOCK 0x9400033cu
#define LANE_TYPE          0x10u       /* in the lane block, x0 */
#define HSFC_LANE          12u
#define TENC_PARAMS        0xa03d272e0ull /* +0: the lane type */
#define TENC_INDEX         0xa03d27398ull /* +0xb8: the rate index */
#define TDEC_SETUP_SITE    0x40e67250u
#define TDEC_SETUP_STOCK_0 0xa9bd5ffeu /* STP X30,X23,[SP,#-0x30]! */
#define TDEC_SETUP_STOCK_1 0xa90157f6u /* STP X22,X21,[SP,#0x10] */
#define TDEC_SETUP_STOCK_2 0xa9024ff4u /* STP X20,X19,[SP,#0x20] */
#define TDEC_SETUP_STOCK_3 0xd28ff914u /* MOVZ X20,#0x7fc8 */
#define TDEC_RING_CALL     0x40e67400u /* in sub_40E673EC: BL the set-up */
#define TDEC_RING_CALL_STOCK 0x97ffff94u
#define TDEC_RING_RETURN   0x40e67404u
#define TDEC_CFG_INDEX     0x98u
#define TDEC_INDEX_SITE    0x40e8974cu
#define TDEC_INDEX_STOCK   0x52800049u /* MOVZ W9,#2 */
#define TENC_RATES         0xa02b0f8acull
#define TDEC_RATES         0xa02b0f884ull
#define RATE_COUNT         9u
#define HE_INDEX           2u          /* 3.0 bpp */
/* sub_40E99328's answer for the high-speed lane, which the install checks
 * is still 2. */
#define LANE_INDEX_SITE    0x40e9936cu
#define LANE_INDEX_STOCK   0x52800040u /* MOVZ W0,#2 */
/* Slot bytes per mode (sub_410456A0: the lane block's +0x4C minus 3):
 * C30, C120, C60, C15. Pool 63 gives each a whole number of 0x470400-byte
 * granules: 2 for FX (225 slots), 1 for C120's DX (450). */
#define SLOT_SIZES         0xa02b5a670ull
#define SLOT_MODES         4u
#define SLOT_FX_STOCK      0x8e0800u
#define SLOT_DX_STOCK      0x3d2300u
#define RING_GRANULE       0x470400u
/* The readouts the slots hold (the frame's E+0x38 on the body). */
#define DX_WIDTH           4000u
#define DX_HEIGHT          2664u
/* The emulator has no ring: its stand-in frame (the "emulate" test) is
 * declared the FX readout, so the NEF's directories have the real shape. */
#define FX_WIDTH           6064u
#define FX_HEIGHT          4040u

/* ---- the card writer ---------------------------------------------------- */
/* The writer's own entry point, one call per file: (card, format, file
 * object). The format is the entry's +0x2B0, read at 0x40e83bfc, and by
 * this point the camera has built the file's header -- the Exif of a JPEG
 * -- and the JPEG itself. The file object is the writer table's entry from
 * +0x48 on; its first word is the file's identity, whose low four bits say
 * JPG (0 here), NEF (2), and the rest the slot, DCF folder and number. */
#define WRITE_FILE_SITE    0x406fffd0u
#define WRITE_FILE_STOCK_0 0xf81e0ffeu /* STR X30,[SP,#-0x20]! */
#define WRITE_FILE_STOCK_1 0xa9014ff4u /* STP X20,X19,[SP,#0x10] */
#define WRITE_FILE_STOCK_2 0xaa0203f3u /* MOV X19,X2 */
#define WRITE_FILE_STOCK_3 0x2a0103f4u /* MOV W20,W1 */
#define ENTRY_FILE_ID      0x48u  /* the object, within the entry */
#define ENTRY_CHUNKS       0x2b8u /* {u64 pointer, u32 length, u32 used} */
#define FILE_ID_NEF        2u

/* The writer's NEF, format 1: ten parts, each a pointer and a length the
 * writer reads out of the file object (sub_40701F38, read with Unicorn and
 * checked against a body NEF's own table -- ../nef-writer-notes.md). The
 * file object is the entry from +0x48 on: [obj+0x10] is the entry itself
 * (E), [obj+0x18] its chunk list (C, entry+0x2B8), [obj+8] the frame's
 * metadata block (M). In the order written:
 *
 *   part  pointer               length
 *    0    [C]                   obj+0x64        the TIFF and its directories
 *    5    [[E+0x08]+0x08]       [[E+0x08]+0x10] when M+0x2FC is not 1
 *    1    [C] + obj+0x6C        obj+0x68
 *    8    [[E+0x28]+0x18]       obj+0x54        none when E+0x28 is 0
 *    9    [[E+0x30]+0x18]       obj+0x58        none when E+0x30 is 0
 *   10    [[E+0x38]+0x18]       obj+0x5C        none when E+0x38 is 0
 *    6    [[E+0x10]+0x08]       obj+0x4C        none when E+0x10 is 0
 *    7    [[E+0x20]+0x08]       obj+0x50        E+0x20 is read regardless
 *    4    [[E+0x00]+0x10]       obj+0x60        the raw strip
 *   11    [C+0x10]              [C+0x1C]        none when C+0x10 is 0
 *
 * A part of length 0 is not written; the file's size is obj+0x40. A body
 * NEF fills them as 105,096 + 92,837 + 10,451 + 98,304 + 147,456 + 0 +
 * 451,584 + 809,984 + 25,306,955 = 27,022,667 bytes (part 5's own length
 * was not in the dump; it is the remainder). */
#define OBJ_ENTRY          0x10u
#define OBJ_CHUNKS         0x18u
#define OBJ_M              0x08u
#define OBJ_FILE_BYTES     0x40u
#define OBJ_LEN_FIRST      0x4cu /* parts 6, 7, 8, 9, 10: five words */
#define OBJ_LEN_STRIP      0x60u
#define OBJ_LEN_HEAD       0x64u
#define M_NEF_ALT          0x2fcu
#define WRITE_FORMAT_NEF   1u

/* Which files of a shot are still to be written. The card layer keeps a
 * record per file number while its files are being written: a mask of the
 * types the shot has (+0x24) and of the types still to come (+0x48), a
 * bit per type -- 1 JPEG, 4 NEF. A writer asked to open a type the record
 * does not name refuses (sub_408ADE50), and the record goes away when the
 * last of its files is written; a JPEG-only shot has 1 and 1. Found as the
 * writer finds it: sub_408BFB58(card context + 0x203280, id, 0), the card
 * context at 0xA04B60290 for the id's slot 0 and 0xA04B60298 for slot 1
 * (sub_408B36B4; slot 2 goes elsewhere and is not handled). */
#define CARD_CONTEXT_0     0xa04b60290ull
#define CARD_RECORDS       0x203280u
#define RECORD_TYPES       0x24u
#define RECORD_TO_WRITE    0x48u
typedef uint64_t (*fw_file_record_t)(uint64_t records, uint32_t id,
                                     uint64_t copy);
#define fw_file_record     FW(fw_file_record_t, 0x408bfb58u)

/* ---- where the choice is stored ----------------------------------------
 *
 * Group 0x0f of the camera's settings store holds the movie items; item
 * 0x0f0b is one byte at the start of a four-byte slot and the three bytes
 * behind it are padding the store carries but no item claims. PRORES keeps
 * its grade in the first; HE RAW is the second, so it is saved and restored
 * with the bank the user is shooting.
 */
#define STORE_HE_RAW       0xa03f0aef6ull /* 0 JPEG as the menu says, else
                                            * the HE RAW tier */
/* Tier n (1..5) keeps n + 2 bits per pixel: rate index n + 1. 1 is the
 * camera's own HE, and what the first release stored for HE RAW. */
#define TIER_COUNT         5u

/* ---- firmware functions ------------------------------------------------- */
/* The card, through the firmware's own file services (the same entries the
 * loader's profile names): a hook handler runs on a camera task, where the
 * loader's API would attribute the call to whatever program is running. */
typedef int32_t (*fw_file_open_t)(const char *path, uint32_t flags, uint32_t mode);
typedef int32_t (*fw_file_write_t)(int32_t fd, const void *buf, uint32_t bytes,
                                   uint32_t *written);
typedef int32_t (*fw_file_close_t)(int32_t fd);
typedef void (*fw_dcache_inv_t)(void *addr, uint32_t len);
typedef int32_t (*fw_file_read_t)(int32_t fd, void *buf, uint32_t bytes,
                                  uint32_t *read);
#define fw_file_open  FW(fw_file_open_t, 0x402e3674u)
#define fw_file_read  FW(fw_file_read_t, 0x402e3774u)
#define FILE_READ         0u     /* flags & 3: 0 read, 1 write, 2 both */
#define fw_file_write FW(fw_file_write_t, 0x402e37e0u)
#define fw_file_close FW(fw_file_close_t, 0x402e36d4u)
#define fw_dcache_inv FW(fw_dcache_inv_t, 0x40d32244u)
/* The entry that writes dirty lines back rather than discarding them: the
 * card layer may take a buffer by DMA, so anything this program filled in
 * goes back to memory before it is handed over. */
#define fw_dcache_clean FW(fw_dcache_inv_t, 0x40d321bcu)
#define FILE_WRITE_CREATE 0x301u /* write, create; refuses a file already there */
#define FILE_WRITE_OVER   0x101u /* write, create; an existing file is written
                                  * over from the start, not shortened (how
                                  * the firmware rewrites its own files) */
#define FILE_ATTRIBUTES   0x20u  /* the archive bit a new file gets */

typedef void *(*fw_row_value_t)(void *window, uint32_t item_id, uint32_t kind);
typedef void (*fw_text_store_t)(const char *text, uint32_t slot, uint32_t style);
typedef void (*fw_text_bind_t)(void *widget, uint32_t slot);
typedef uint32_t (*fw_entry_guard_t)(void);
typedef void (*fw_set_cursor_t)(void *window, uint32_t item_id);
typedef uint64_t (*fw_set_mark_t)(void *window, uint32_t item_id, uint32_t marked);
typedef uint64_t (*fw_handler_t)(void *window);

#define fw_row_value   FW(fw_row_value_t, 0x4052c7a0u)
#define fw_text_store  FW(fw_text_store_t, 0x404066c8u)
#define fw_text_bind   FW(fw_text_bind_t, 0x4040a1acu)
#define fw_entry_guard FW(fw_entry_guard_t, 0x403bf168u)
#define fw_set_cursor  FW(fw_set_cursor_t, 0x403c0f8cu)
#define fw_set_mark    FW(fw_set_mark_t, 0x403c0ffcu)

/* The navigation block every action is called with (core/include/nlr/menu.h
 * describes the same three words). */
struct fw_nav {
    void *window;
    uint32_t code;
    uint32_t next;
};
typedef void (*fw_action_t)(struct fw_nav *nav);

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
 * HSFC page record names our item array, which is the block's first bytes.
 * 0 until the menu is installed -- while the record still names the stock
 * rows -- and every hook does nothing then. */
static inline uint64_t plugin_block(void)
{
    uint64_t items = rd64(HSFC_PAGE + PAGE_ITEMS);
    return items == HSFC_ITEMS ? 0 : items - OFF_HSFC_ITEMS;
}

/* 0 off, or 1..TIER_COUNT; anything else in the byte is off. */
static inline uint32_t he_raw_tier(void)
{
    uint32_t tier = rd8(STORE_HE_RAW);
    return tier <= TIER_COUNT ? tier : 0u;
}

static inline uint32_t he_raw_on(void)
{
    return he_raw_tier() != 0u;
}

static inline uint32_t tier_index(uint32_t tier)
{
    return tier ? tier + 1u : HE_INDEX;
}

#endif /* Z63FW_H */
