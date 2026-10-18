/*
 * HE RAW for high-speed frame capture: every frame of a C15/C30/C60 burst,
 * pre-release included, goes to the card as a NEF of the ring's own HE RAW
 * instead of its JPEG.
 *
 * The camera keeps every high-speed frame in a pool-63 ring slot as
 * TicoRAW HE (3.0 bpp: FX 9,186,816 bytes, byte for byte the format of the
 * strip of its own high-efficiency NEF), decodes it for the JPEG, and lets
 * the slot go. Three hooks, all through the loader's code channel, make the
 * NEFs:
 *
 *   - the drain's submit (sub_40F7F1B0), which hands each frame to the
 *     pipeline: a record of the frame -- its request, metadata block, ring
 *     slot, payload -- a few words, nothing copied;
 *   - the pool free (sub_4103E664): a ring slot whose frame's NEF is still
 *     to be written is not freed but kept, and given back once the NEF is
 *     on the card (the camera frees it long before its file's turn);
 *   - the card writer (sub_406FFFD0): when it is about to write a ring
 *     frame's JPEG, the same file object goes to it as a NEF (nef.c builds
 *     the head from the JPEG's own Exif), and the JPEG is skipped.
 *
 * The NEF is the camera's own writer's work: its name, folder, timestamp
 * and index entry. A frame whose NEF cannot be written goes out as its
 * JPEG, so no frame is lost.
 *
 * Two more set how many bits the ring keeps, for the tiers above HE
 * (z63fw.h has the chain):
 *
 *   - the ring allocation (sub_40E6C0C4) when a lane start calls it: on the
 *     high-speed lane, the slot sizes and the TENC rate index of the tier,
 *     before the ring is allocated and TENC set up;
 *   - the TDEC set-up (loc_40E67250), when the high-speed stage calls it:
 *     TDEC decodes at that rate.
 *
 * README.md and NOTES.md say how each of these was found.
 */
#include "hsfc.h"
#include "nef.h"

#define APPS_DIR   "A:\\NLOADER\\APPS\\"
#define CONFIG     "A:\\NLOADER\\APPS\\HERAW.CFG"
#define CHUNK      0x40000u  /* 256 KiB per write request */

/* HERAW.CFG on the card, one word each. */
#define TEST_DEBUG    1u  /* a note of every burst, HERAW.TXT */
#define TEST_JPEG     2u  /* the NEF beside the JPEG instead of for it */
#define TEST_EMULATE  4u  /* every JPEG is a frame, the JPEG its payload:
                           * the emulator, which has no ring */

/* What the hooks keep, in the block. */
struct heraw_state {
    uint32_t tests;
    uint32_t session;       /* the note's number this power-on */
    uint32_t frames;        /* recorded by the submit hook */
    uint32_t tried;         /* JPEGs a frame was found for */
    uint32_t written;       /* NEFs written */
    uint32_t failed;        /* NEFs the writer refused: the JPEG went out */
    uint32_t lost;          /* the slot was gone by the JPEG's turn */
    uint32_t other;         /* JPEGs that were no ring frame's */
    uint32_t kept;          /* slots taken from the pool free */
    uint32_t returned;      /* given back after the NEF */
    uint32_t given_up;      /* given back unwritten: overrun or too old */
    uint32_t ring_frees;    /* ring-pool frees the hook has seen */
    uint32_t activity;      /* changes whenever any of the above does */
    uint32_t noted;
    int32_t hook_pool;      /* the loader's answer when it was attached */
    int32_t last_result;    /* the writer's, or ours: < 0 */
    uint32_t last_id;
    uint32_t last_total;
    uint32_t last_built;
    uint32_t last_alt;
    uint32_t last_types;
    uint32_t last_to_write;
    uint32_t index;         /* the ring's rate index this lane, 0 before one */
    uint32_t lanes;         /* high-speed lane starts */
    uint32_t waits;         /* lane starts a new tier had to wait past */
    uint32_t tdec_rated;    /* TDEC set-ups given a tier's index */
    uint32_t ring_beat;     /* submits, ring frees, ring TDEC set-ups */
    uint32_t ring_quiet;    /* the task's polls since ring_beat moved */
    uint32_t slot_fx;       /* the slot sizes the last lane start set */
    uint32_t slot_dx;
};
_Static_assert(sizeof(struct heraw_state) <= STATE_MAX,
               "the state outgrew its room in the block");

/* One per ring frame the drain hands over. */
struct frame_rec {
    uint64_t r;
    uint64_t m;
    uint64_t e;
    uint64_t payload;
    uint32_t bytes;
    uint16_t width;
    uint16_t height;
    uint16_t black[4];
    uint32_t sequence;
    int32_t result;         /* 0 written, FRAME_SLOT_GONE, or the error */
    uint32_t state;         /* FRAME_FREE, FRAME_RECORDED, ... */
    uint32_t order;         /* the drain's count */
    uint32_t written_order; /* the writer's count */
    uint32_t kept;          /* 1: the slot is ours to give back */
    uint32_t kept_ms;       /* when the program's task first saw it kept */
    uint32_t index;         /* the rate index it was compressed at */
};
#define FRAME_FREE      0u
#define FRAME_RECORDED  1u
#define FRAME_DONE      2u
#define FRAME_WRITING   3u
#define FRAME_SLOT_GONE 100
_Static_assert(sizeof(struct frame_rec) * FRAME_MAX <= FRAMES_BYTES,
               "the frame records outgrew their room in the block");

static volatile struct heraw_state *state_of(void)
{
    uint64_t block = plugin_block();
    if (!block)
        return 0;
    return (volatile struct heraw_state *)(uintptr_t)(block + OFF_STATE);
}

/* The frames become NEFs: HE RAW chosen, or the emulator's test. */
static int active(volatile struct heraw_state *state)
{
    return he_raw_on() || (state->tests & TEST_EMULATE);
}

/* ---- the ring's rate ---------------------------------------------------- */

/* TENC's output for a readout at a rate index: what the camera computes
 * (sub_40E67CFC), so what a slot holds. */
static uint32_t payload_bytes(uint32_t index, uint32_t width, uint32_t height)
{
    uint64_t rate = rd32(TENC_RATES + index * 4u);
    return (uint32_t)((rate * width * height / 80u) & ~511ull);
}

/* The index the ring of the running (or last) high-speed lane keeps. */
static uint32_t ring_index(volatile struct heraw_state *state)
{
    uint32_t index = state ? state->index : 0u;
    return index && index < RATE_COUNT ? index : HE_INDEX;
}

/* A slot for a tier: the stock slot's room past its HE frame, past the
 * tier's frame instead. */
static uint32_t slot_bytes(uint32_t index, uint32_t stock, uint32_t width,
                           uint32_t height)
{
    if (index == HE_INDEX)
        return stock;
    return payload_bytes(index, width, height) +
           (stock - payload_bytes(HE_INDEX, width, height));
}

/* ---- the frame records -------------------------------------------------- */

static volatile struct frame_rec *frame_at(uint64_t block, uint32_t n)
{
    return (volatile struct frame_rec *)(uintptr_t)
        (block + OFF_FRAMES + n * sizeof(struct frame_rec));
}

/* A record changes hands between three tasks -- the drain records it, the
 * card writer writes its NEF, the program's own task gives up on it -- so
 * the step out of FRAME_RECORDED is taken by exactly one of them. */
static int frame_claim(volatile struct frame_rec *f, uint32_t to)
{
    uint32_t expected = FRAME_RECORDED;
    return __atomic_compare_exchange_n((uint32_t *)&f->state, &expected, to,
                                       0, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE);
}

static uint64_t pool_free_direct(uint64_t slot);

/* Give a kept slot back, once, past our own hook. */
static void frame_release(volatile struct frame_rec *f, volatile uint32_t *count)
{
    if (__atomic_exchange_n((uint32_t *)&f->kept, 0u, __ATOMIC_ACQ_REL)) {
        pool_free_direct(f->e);
        (*count)++;
    }
}

/* The drain hands a frame over (its request block in x0). Runs on the
 * drain's task: memory only. */
static void record_frame(uint64_t r)
{
    volatile struct heraw_state *state = state_of();
    if (!state || !he_raw_on() || !r)
        return;
    uint64_t e = rd64(r + R_SLOT_E), m = rd64(r + R_SLOT_M);
    if (!e || !m)
        return;
    uint64_t payload = rd64(e + E_PAYLOAD);
    uint32_t width = rd16(e + E_SHAPE), height = rd16(e + E_SHAPE + 2u);
    if (!payload || !width || !height || width > 9000u || height > 9000u)
        return;

    /* The oldest record goes first; one whose frame is still waiting
     * after 256 newer ones is not coming. */
    uint64_t block = plugin_block();
    volatile struct frame_rec *f = frame_at(block, state->frames % FRAME_MAX);
    frame_claim(f, FRAME_DONE);
    frame_release(f, &state->given_up);
    f->state = FRAME_FREE;
    f->r = r;
    f->m = m;
    f->e = e;
    f->payload = payload;
    f->index = ring_index(state);
    f->bytes = payload_bytes(f->index, width, height);
    f->width = (uint16_t)width;
    f->height = (uint16_t)height;
    for (uint32_t k = 0; k < 4u; k++)
        f->black[k] = rd16(m + M_BLACK + k * 2u);
    f->sequence = rd32(m + M_SEQUENCE);
    f->result = 0;
    f->order = state->frames;
    f->written_order = 0;
    f->kept = 0;
    f->kept_ms = 0;
    f->state = FRAME_RECORDED;            /* last: the writer reads it */
    state->frames++;
    state->activity++;
}

/* ---- the NEF, through the camera's own writer --------------------------- */

NLR_HOOK_CONTINUE(write_original, WRITE_FILE_SITE,
                  WRITE_FILE_STOCK_0, WRITE_FILE_STOCK_1,
                  WRITE_FILE_STOCK_2, WRITE_FILE_STOCK_3)

typedef uint64_t (*write_file_t)(uint64_t card, uint64_t format,
                                 uint64_t object);

/* What nef_via_writer changes, so all of it can be put back. */
struct parts_saved {
    uint64_t parts[8];     /* E+0x00 .. E+0x38 */
    uint64_t chunks[4];    /* C's first two {pointer, length, used} */
    uint32_t lengths[9];   /* obj+0x4C .. obj+0x6C */
    uint32_t id;
    uint64_t file_bytes;
};

/* The JPEG's 640 x 424 MPF image -- the one the camera's playback shows a
 * NEF from (its MakerNote PreviewIFD; a 1620 x 1080 SubIFD alone was not
 * enough, body, 2026-10-17). The writer puts a JPEG together from seven
 * parts (kind 0: 0, 11, 4, 2, 5, 3, 6): the main image's header and scan,
 * then each MPF image's header -- a chunk of the chunk list, +0x20 and
 * +0x30, length at +0xC: the SOI and the image's APP1 -- and the rest of
 * it (DQT, SOF, ... the scan), from the entry's +0x08 and +0x10
 * descriptors, two bytes in (past their own SOI). Of the two, the
 * narrower. */
struct nef_preview {
    uint64_t header;
    uint32_t header_bytes;
    uint64_t data;
    uint32_t data_bytes;
};

static uint32_t jpeg_width(uint64_t at, uint32_t bytes)
{
    uint32_t i = 2u;
    while (i + 9u < bytes && rd8(at + i) == 0xffu) {
        uint32_t marker = rd8(at + i + 1u);
        uint32_t length = ((uint32_t)rd8(at + i + 2u) << 8) | rd8(at + i + 3u);
        if (marker == 0xc0u || marker == 0xc1u || marker == 0xc2u)
            return ((uint32_t)rd8(at + i + 7u) << 8) | rd8(at + i + 8u);
        if (marker == 0xdau || length < 2u)
            break;
        i += 2u + length;
    }
    return 0;
}

static void find_preview(uint64_t entry, uint64_t chunks,
                         struct nef_preview *out)
{
    out->header = out->data = 0;
    out->header_bytes = out->data_bytes = 0;
    for (uint32_t k = 0; k < 2u; k++) {
        uint64_t header = rd64(chunks + 0x20u + k * 0x10u);
        uint32_t header_bytes = rd32(chunks + 0x2cu + k * 0x10u);
        uint64_t desc = rd64(entry + 0x08u + k * 8u);
        if (!header || header_bytes < 16u || header_bytes > (1u << 20) || !desc)
            continue;
        uint64_t data = rd64(desc + 0x08u);
        uint32_t data_bytes = rd32(desc + 0x10u);
        if (!data || data_bytes <= 2u || data_bytes > (4u << 20))
            continue;
        if (rd8(header) != 0xffu || rd8(header + 1u) != 0xd8u ||
            rd8(data) != 0xffu || rd8(data + 1u) != 0xd8u)
            continue;
        /* The header chunk is the SOI and the image's own APP1 (65,538
         * bytes in the emulator); its frame header is in the scan's part. */
        uint32_t width = jpeg_width(data, data_bytes);
        if (!width || width > 1024u)
            continue;
        out->header = header;
        out->header_bytes = header_bytes;
        out->data = data + 2u;
        out->data_bytes = data_bytes - 2u;
        return;
    }
}

/* What a NEF needs of a ring frame. */
struct nef_frame {
    uint64_t payload;
    uint32_t bytes;
    uint32_t width;
    uint32_t height;
    uint16_t black[4];
};

/* The camera's own writer, given a NEF of ours (NOTES.md).
 *
 * It is about to write a JPEG from this file object. The same object goes
 * to it first as format 1, under the identity of a NEF with the same
 * number -- the order a RAW+JPEG shot writes its two files in -- and with
 * the ten parts pointing at the three pieces of nef.h:
 *
 *   part 0  head     the JPEG's Exif block and our directories (the block)
 *   part 5  preview  the 640 x 424 JPEG's header   (where the camera has it)
 *   part 6           and the rest of it            (where the camera has it)
 *   part 4  payload  the frame                     (the ring)
 *
 * and every other part empty. The shot's file-number record is made to
 * name a NEF -- only a NEF, unless the JPEG is kept too -- for the writer
 * opens no type the record does not name. Everything changed in the object
 * is put back the moment the writer returns. 0, the writer's error, or
 * ours (< 0).
 */
static int64_t nef_via_writer(uint64_t card, uint64_t object,
                              const struct nef_frame *frame, int only)
{
    volatile struct heraw_state *state = state_of();
    uint64_t block = plugin_block();
    if (!state || !block)
        return -1;
    uint64_t entry = rd64(object + OBJ_ENTRY);
    uint64_t chunks = rd64(object + OBJ_CHUNKS);
    uint64_t m = rd64(object + OBJ_M);
    uint32_t id = rd32(object);
    /* Only the shape every JPEG so far has had: the object is the entry
     * from +0x48, the chunk list is the entry's own, the id says JPEG. */
    if (entry + ENTRY_FILE_ID != object || chunks != entry + ENTRY_CHUNKS ||
        !m || (id & 0xfu) != 0u || !id)
        return -2;
    uint64_t jpeg = rd64(chunks);
    uint32_t jpeg_bytes = rd32(chunks + 8u);
    if (!jpeg || !jpeg_bytes)
        return -3;
    state->last_alt = rd32(m + M_NEF_ALT);
    if (state->last_alt == 1u) {
        /* Part 5 is then read from where part 7 is: the preview would land
         * in the wrong place. */
        state->last_result = -10;
        return -10;
    }
    uint32_t slot = (id >> 29) & 3u;
    if (slot > 1u) {
        state->last_result = -11;
        return -11;
    }
    uint64_t card_context = rd64(CARD_CONTEXT_0 + slot * 8u);
    uint64_t record = card_context ?
        fw_file_record(card_context + CARD_RECORDS, id, 0) : 0;
    if (!record || rd32(record) != id) {
        state->last_result = -12;
        return -12;
    }
    uint16_t types = rd16(record + RECORD_TYPES);
    uint16_t to_write = rd16(record + RECORD_TO_WRITE);
    state->last_types = types;
    state->last_to_write = to_write;
    if (!(types & 1u) || !(to_write & 1u)) {
        state->last_result = -13;
        return -13;
    }

    /* Before anything of the entry changes: the JPEG's own preview. */
    struct nef_preview preview;
    find_preview(entry, chunks, &preview);

    struct nef_source source;
    if (frame) {                          /* a ring frame */
        source.payload = frame->payload;
        source.payload_bytes = frame->bytes;
        source.width = frame->width;
        source.height = frame->height;
        for (uint32_t n = 0; n < 4u; n++)
            source.black[n] = frame->black[n];
    } else {                              /* the emulator's stand-in */
        source.payload = jpeg;
        source.payload_bytes = jpeg_bytes;
        source.width = FX_WIDTH;
        source.height = FX_HEIGHT;
        for (uint32_t n = 0; n < 4u; n++)
            source.black[n] = 1008u;
    }
    source.cfa = 0;
    source.jpeg = jpeg;
    source.jpeg_bytes = jpeg_bytes;
    source.preview_bytes = preview.header_bytes + preview.data_bytes;
    struct nef_plan plan;
    int built = nef_build(block + OFF_NEF, NEF_HEADER_MAX, &source, &plan);
    state->last_built = (uint32_t)built;
    if (built < 0) {
        state->last_result = built;
        return built;
    }
    /* The writer hands our bytes to the card's DMA. */
    fw_dcache_clean((void *)(uintptr_t)(block + OFF_NEF),
                    (plan.head_bytes + 63u) & ~63u);

    /* Four descriptors of our own. Part 7's is always there: it is read
     * whether it is wanted or not, so it is an empty one. A descriptor's
     * +0x10 is how much of the part is data; the writer pads the rest with
     * zeros, so the two are made the same. */
    uint64_t strip = block + OFF_PARTS, head = strip + 0x40u;
    uint64_t scan = strip + 0x80u, empty = strip + 0xc0u;
    for (uint32_t n = 0; n < 0x100u; n += 4u)
        wr32(strip + n, 0);
    wr64(strip + 0x10u, plan.payload);
    wr32(strip + 0x18u, plan.payload_bytes);
    wr64(head + 0x08u, preview.header);
    wr32(head + 0x10u, preview.header_bytes);
    wr64(scan + 0x08u, preview.data);
    wr32(scan + 0x10u, preview.data_bytes);
    uint32_t total = plan.head_bytes + plan.preview_bytes + plan.payload_bytes;

    struct parts_saved saved;
    for (uint32_t n = 0; n < 8u; n++)
        saved.parts[n] = rd64(entry + n * 8u);
    for (uint32_t n = 0; n < 4u; n++)
        saved.chunks[n] = rd64(chunks + n * 8u);
    for (uint32_t n = 0; n < 9u; n++)
        saved.lengths[n] = rd32(object + OBJ_LEN_FIRST + n * 4u);
    saved.id = id;
    saved.file_bytes = rd64(object + OBJ_FILE_BYTES);

    wr64(entry + 0x00u, strip);                          /* part 4 */
    wr64(entry + 0x08u, plan.preview_bytes ? head : 0);  /* part 5 */
    wr64(entry + 0x10u, plan.preview_bytes ? scan : 0);  /* part 6 */
    /* E+0x18 stays: the writer reads it for the file's record */
    wr64(entry + 0x20u, empty);                          /* part 7 */
    wr64(entry + 0x28u, 0);                              /* parts 8, 9, 10 */
    wr64(entry + 0x30u, 0);
    wr64(entry + 0x38u, 0);
    wr64(chunks, plan.head);                             /* part 0 */
    wr64(chunks + 0x10u, 0);                             /* part 11 */
    for (uint32_t n = 0; n < 9u; n++)                    /* part 1 and the */
        wr32(object + OBJ_LEN_FIRST + n * 4u, 0);        /* rest: nothing */
    if (plan.preview_bytes)
        wr32(object + OBJ_LEN_FIRST, preview.data_bytes);      /* part 6 */
    wr32(object + OBJ_LEN_STRIP, plan.payload_bytes);
    wr32(object + OBJ_LEN_HEAD, plan.head_bytes);
    wr64(object + OBJ_FILE_BYTES, total);
    uint32_t nef = (id & ~0xfu) | FILE_ID_NEF;
    wr32(object, nef);
    /* Beside the JPEG, the shot gains a NEF; instead of it, the shot is a
     * NEF and nothing else -- so the record empties, and the shot is done,
     * when the NEF is written. */
    uint16_t nef_bit = (uint16_t)(1u << FILE_ID_NEF);
    wr16(record + RECORD_TYPES, only ? nef_bit : (uint16_t)(types | nef_bit));
    wr16(record + RECORD_TO_WRITE,
         only ? nef_bit : (uint16_t)(to_write | nef_bit));

    uint64_t result = ((write_file_t)(void *)&write_original)(
        card, WRITE_FORMAT_NEF, object);

    /* A NEF that replaced the JPEG keeps its identity: whatever the camera
     * does with the object after the writer is about the NEF now. */
    wr32(object, only && result == 0 ? nef : saved.id);
    wr64(object + OBJ_FILE_BYTES, saved.file_bytes);
    for (uint32_t n = 0; n < 9u; n++)
        wr32(object + OBJ_LEN_FIRST + n * 4u, saved.lengths[n]);
    for (uint32_t n = 0; n < 4u; n++)
        wr64(chunks + n * 8u, saved.chunks[n]);
    for (uint32_t n = 0; n < 8u; n++)
        wr64(entry + n * 8u, saved.parts[n]);
    /* The record stays as the writer left it when the NEF was written, and
     * loses the NEF again when it was not, or the shot would wait for it
     * for ever. */
    if (result != 0 && rd32(record) == id) {
        if (only) {                       /* the JPEG goes out after all */
            wr16(record + RECORD_TYPES, types);
            wr16(record + RECORD_TO_WRITE, to_write);
        } else {
            wr16(record + RECORD_TYPES, (uint16_t)(rd16(record + RECORD_TYPES) &
                                                   ~(1u << FILE_ID_NEF)));
            wr16(record + RECORD_TO_WRITE,
                 (uint16_t)(rd16(record + RECORD_TO_WRITE) &
                            ~(1u << FILE_ID_NEF)));
        }
    }

    state->last_result = (int32_t)result;
    state->last_id = nef;
    state->last_total = total;
    return (int64_t)(int32_t)result;
}

/* A JPEG the writer is about to put on the card: if it is a ring frame's,
 * that frame's NEF goes first. The frame is found by its metadata block,
 * which the drain's record and the writer's file object (+8) share. 1 when
 * the NEF replaced the JPEG, which is then not written. */
static int nef_for_jpeg(uint64_t card, uint64_t object)
{
    volatile struct heraw_state *state = state_of();
    uint64_t block = plugin_block();
    if (!state || !block || !active(state))
        return 0;
    int only = !(state->tests & TEST_JPEG);
    if (state->tests & TEST_EMULATE) {    /* the emulator: every JPEG */
        state->tried++;
        int64_t result = nef_via_writer(card, object, 0, only);
        if (result == 0)
            state->written++;
        else
            state->failed++;
        state->activity++;
        return only && result == 0;
    }
    uint64_t m = rd64(object + OBJ_M);
    volatile struct frame_rec *found = 0;
    for (uint32_t n = 0; n < FRAME_MAX; n++) {
        volatile struct frame_rec *f = frame_at(block, n);
        if (f->state == FRAME_RECORDED && f->m == m &&
            (!found || f->order > found->order))
            found = f;
    }
    state->activity++;
    if (!found) {
        state->other++;                   /* not a ring frame's JPEG */
        return 0;
    }
    /* Taken out first, then checked: the drain reuses a record once 256
     * newer frames have come, and may be doing so right now. */
    uint32_t order = found->order;
    uint64_t r = found->r, e = found->e;
    struct nef_frame frame;
    frame.payload = found->payload;
    frame.bytes = found->bytes;
    frame.width = found->width;
    frame.height = found->height;
    for (uint32_t n = 0; n < 4u; n++)
        frame.black[n] = found->black[n];
    if (found->order != order || found->m != m ||
        !frame_claim(found, FRAME_WRITING)) {
        state->other++;
        return 0;
    }
    found->written_order = state->tried++;
    /* The payload is still the frame's while the request holds the slot,
     * or while we do (keep_slot). */
    if (!found->kept && rd64(r + R_SLOT_E) != e) {
        found->result = FRAME_SLOT_GONE;
        found->state = FRAME_DONE;
        state->lost++;
        return 0;
    }
    int64_t result = nef_via_writer(card, object, &frame, only);
    found->result = (int32_t)result;
    found->state = FRAME_DONE;
    frame_release(found, &state->returned);
    if (result == 0)
        state->written++;
    else
        state->failed++;
    return only && result == 0;
}

/* ---- the hooks ---------------------------------------------------------- */

/* The pool free, about to take a slot back: if it is the ring slot of a
 * frame whose NEF is still to be written, it stays ours instead, and the
 * caller is told it was freed (it only clears its own pointer). Called
 * for every pool from every task, so: one load to leave early, no locks. */
static int keep_slot(uint64_t slot)
{
    volatile struct heraw_state *state = state_of();
    if (!state || !slot)
        return 0;
    if (rd32(slot + SLOT_POOL) != RING_POOL)
        return 0;
    state->ring_beat++;
    if (!he_raw_on())
        return 0;
    uint64_t block = plugin_block();
    state->ring_frees++;
    for (uint32_t n = 0; n < FRAME_MAX; n++) {
        volatile struct frame_rec *f = frame_at(block, n);
        if (f->e != slot || f->kept)
            continue;
        if (f->state != FRAME_RECORDED && f->state != FRAME_WRITING)
            continue;
        f->kept = 1;
        state->kept++;
        state->activity++;
        return 1;
    }
    return 0;
}

NLR_HOOK_CONTINUE(pool_free_original, POOL_FREE_SITE,
                  POOL_FREE_STOCK_0, POOL_FREE_STOCK_1,
                  POOL_FREE_STOCK_2, POOL_FREE_STOCK_3)

typedef uint64_t (*pool_free_t)(uint64_t slot);

static uint64_t pool_free_direct(uint64_t slot)
{
    return ((pool_free_t)(void *)&pool_free_original)(slot);
}

static uint64_t pool_free_hook(uint64_t slot)
{
    if (keep_slot(slot))
        return 1;                         /* "freed" */
    return pool_free_direct(slot);
}

static uint64_t write_hook(uint64_t card, uint64_t format, uint64_t object)
{
    if (card == 0 && format == 0 && nef_for_jpeg(card, object))
        return 0;         /* the NEF was the file: no JPEG */
    return ((write_file_t)(void *)&write_original)(card, format, object);
}

NLR_HOOK_CONTINUE(submit_original, SUBMIT_SITE,
                  SUBMIT_STOCK_0, SUBMIT_STOCK_1,
                  SUBMIT_STOCK_2, SUBMIT_STOCK_3)

typedef uint64_t (*submit_t)(uint64_t r, uint32_t flag);

static uint64_t submit_hook(uint64_t r, uint32_t flag)
{
    volatile struct heraw_state *state = state_of();
    if (state)
        state->ring_beat++;
    record_frame(r);
    return ((submit_t)(void *)&submit_original)(r, flag);
}

/* Nothing of a ring is in flight: no frame waiting for its NEF, no slot
 * kept, and no submit, ring free or ring TDEC set-up for a second (or none
 * at all since the program started). Then no slot of the old size holds a
 * frame still to be decoded or written, and a new rate can take over. */
static int ring_idle(volatile struct heraw_state *state, uint64_t block)
{
    if (state->ring_beat && state->ring_quiet < 2u)
        return 0;
    for (uint32_t n = 0; n < FRAME_MAX; n++) {
        volatile struct frame_rec *f = frame_at(block, n);
        if (f->kept || f->state == FRAME_RECORDED ||
            f->state == FRAME_WRITING)
            return 0;
    }
    return 1;
}

/* A capture lane is starting (the lane block in x0): its rate index is
 * latched, its ring not yet allocated nor TENC set up. On the high-speed
 * lane, the slots and the rate index of the chosen tier -- the camera's own
 * for HE or JPEG. A tier chosen while a ring is still in flight waits for
 * a later lane start; until then the lane keeps the rate it had, so every
 * frame is decoded and written at the rate it was compressed at. */
static void lane_started(uint64_t lane)
{
    volatile struct heraw_state *state = state_of();
    uint64_t block = plugin_block();
    if (!state || !block || rd32(lane + LANE_TYPE) != HSFC_LANE)
        return;
    uint32_t index = ring_index(state);
    uint32_t want = tier_index(he_raw_tier());
    if (want != index) {
        if (ring_idle(state, block))
            index = want;
        else
            state->waits++;
    }
    uint32_t fx = slot_bytes(index, SLOT_FX_STOCK, FX_WIDTH, FX_HEIGHT);
    uint32_t dx = slot_bytes(index, SLOT_DX_STOCK, DX_WIDTH, DX_HEIGHT);
    for (uint32_t mode = 0; mode < SLOT_MODES; mode++)
        wr32(SLOT_SIZES + mode * 4u, mode == 1u ? dx : fx);
    wr32(TENC_INDEX, index);
    state->index = index;
    state->slot_fx = fx;
    state->slot_dx = dx;
    state->lanes++;
    state->activity++;
}

NLR_HOOK_CONTINUE(alloc_original, RING_ALLOC_SITE,
                  RING_ALLOC_STOCK_0, RING_ALLOC_STOCK_1,
                  RING_ALLOC_STOCK_2, RING_ALLOC_STOCK_3)

typedef uint64_t (*ring_alloc_t)(uint64_t lane, uint64_t count);

/* The ring allocation: from a lane start, the tier goes in first; from the
 * frame-by-frame top-up, nothing changes. */
static uint64_t alloc_hook(uint64_t lane, uint64_t count)
{
    uint64_t caller = (uint64_t)(uintptr_t)__builtin_return_address(0);
    if (lane && (caller == LANE_ALLOC_CALL_A + 4u ||
                 caller == LANE_ALLOC_CALL_B + 4u))
        lane_started(lane);
    return ((ring_alloc_t)(void *)&alloc_original)(lane, count);
}

NLR_HOOK_CONTINUE(tdec_original, TDEC_SETUP_SITE,
                  TDEC_SETUP_STOCK_0, TDEC_SETUP_STOCK_1,
                  TDEC_SETUP_STOCK_2, TDEC_SETUP_STOCK_3)

typedef uint64_t (*tdec_setup_t)(uint64_t cfg, uint64_t flag);

/* TDEC is about to be set up from the block in x0. Called back from the
 * high-speed TDEC stage -- the detour is a branch, so the link register
 * still says who called -- the block carries the index 2 the stage wrote
 * as an immediate: for a tier above HE, the ring's index instead. Every
 * other TDEC set-up goes through untouched. */
static uint64_t tdec_hook(uint64_t cfg, uint64_t flag)
{
    uint64_t caller = (uint64_t)(uintptr_t)__builtin_return_address(0);
    volatile struct heraw_state *state = state_of();
    if (state && caller == TDEC_RING_RETURN && cfg) {
        state->ring_beat++;
        uint32_t index = ring_index(state);
        if (index != HE_INDEX && rd32(cfg + TDEC_CFG_INDEX) == HE_INDEX) {
            wr32(cfg + TDEC_CFG_INDEX, index);
            state->tdec_rated++;
        }
    }
    return ((tdec_setup_t)(void *)&tdec_original)(cfg, flag);
}

/* ---- the program's own task --------------------------------------------- */

struct note {
    char bytes[2048];
    uint32_t length;
};

static void note_put(struct note *n, const char *text)
{
    while (text && *text && n->length < sizeof n->bytes - 1u)
        n->bytes[n->length++] = *text++;
}

static void note_hex(struct note *n, uint64_t value, uint32_t digits)
{
    for (uint32_t i = digits; i-- > 0;) {
        uint32_t nibble = (uint32_t)((value >> (4u * i)) & 15u);
        if (n->length < sizeof n->bytes - 1u)
            n->bytes[n->length++] = (char)(nibble < 10u ? '0' + nibble
                                                        : 'a' + nibble - 10u);
    }
}

static void note_dec(struct note *n, int32_t value)
{
    char digits[12];
    uint32_t count = 0;
    uint32_t u = value < 0 ? (uint32_t)-value : (uint32_t)value;
    if (value < 0)
        note_put(n, "-");
    do {
        digits[count++] = (char)('0' + u % 10u);
        u /= 10u;
    } while (u && count < sizeof digits);
    while (count--)
        if (n->length < sizeof n->bytes - 1u)
            n->bytes[n->length++] = digits[count];
}

/* HERAW.TXT for the first power-on with a note, then HERAW02.TXT, ...:
 * the card layer's create refuses a file already there (0x301), so every
 * power-on takes the first number not on the card, and writes over its own
 * file (0x101) as the bursts come. */
static void note_path(char *path, uint32_t number)
{
    static const char dir[] = APPS_DIR;
    static const char name[] = "HERAW";
    uint32_t at = 0;
    for (uint32_t n = 0; dir[n]; n++)
        path[at++] = dir[n];
    for (uint32_t n = 0; name[n]; n++)
        path[at++] = name[n];
    if (number > 1u) {
        path[at++] = (char)('0' + number / 10u % 10u);
        path[at++] = (char)('0' + number % 10u);
    }
    path[at++] = '.';
    path[at++] = 'T';
    path[at++] = 'X';
    path[at++] = 'T';
    path[at] = 0;
}

static void note_session(volatile struct heraw_state *state)
{
    char path[48];
    if (!state->session) {
        state->session = 99u;
        for (uint32_t number = 1; number < 100u; number++) {
            note_path(path, number);
            int32_t fd = fw_file_open(path, FILE_READ, 0);
            if (fd < 0) {
                state->session = number;
                break;
            }
            fw_file_close(fd);
        }
    }
}

static int32_t note_open(volatile struct heraw_state *state)
{
    char path[48];
    note_session(state);
    note_path(path, state->session);
    int32_t fd = fw_file_open(path, FILE_WRITE_CREATE, FILE_ATTRIBUTES);
    if (fd < 0)
        fd = fw_file_open(path, FILE_WRITE_OVER, FILE_ATTRIBUTES);
    return fd;
}

static void write_note(const struct nlr_api *api,
                       volatile struct heraw_state *state)
{
    struct note n;
    n.length = 0;
    note_session(state);
    note_put(&n, "heraw\nsession ");
    note_dec(&n, (int32_t)state->session);
    note_put(&n, "\nHE RAW tier ");
    note_dec(&n, (int32_t)he_raw_tier());
    note_put(&n, ", ring index ");
    note_dec(&n, (int32_t)state->index);
    note_put(&n, " (lanes ");
    note_dec(&n, (int32_t)state->lanes);
    note_put(&n, ", waited ");
    note_dec(&n, (int32_t)state->waits);
    note_put(&n, ", TDEC at the tier's rate ");
    note_dec(&n, (int32_t)state->tdec_rated);
    note_put(&n, "), slots ");
    note_hex(&n, state->slot_fx, 8);
    note_put(&n, "/");
    note_hex(&n, state->slot_dx, 8);
    note_put(&n, ", tests ");
    note_dec(&n, (int32_t)state->tests);
    note_put(&n, "\nframes ");
    note_dec(&n, (int32_t)state->frames);
    note_put(&n, " tried ");
    note_dec(&n, (int32_t)state->tried);
    note_put(&n, " written ");
    note_dec(&n, (int32_t)state->written);
    note_put(&n, " failed ");
    note_dec(&n, (int32_t)state->failed);
    note_put(&n, " slot gone ");
    note_dec(&n, (int32_t)state->lost);
    note_put(&n, " other jpegs ");
    note_dec(&n, (int32_t)state->other);
    note_put(&n, "\nslots: kept ");
    note_dec(&n, (int32_t)state->kept);
    note_put(&n, " returned ");
    note_dec(&n, (int32_t)state->returned);
    note_put(&n, " given up ");
    note_dec(&n, (int32_t)state->given_up);
    note_put(&n, " ring frees ");
    note_dec(&n, (int32_t)state->ring_frees);
    note_put(&n, " hook ");
    note_dec(&n, state->hook_pool);
    note_put(&n, "\nlast: result ");
    note_dec(&n, state->last_result);
    note_put(&n, " id ");
    note_hex(&n, state->last_id, 8);
    note_put(&n, " size ");
    note_dec(&n, (int32_t)state->last_total);
    note_put(&n, " built ");
    note_dec(&n, (int32_t)state->last_built);
    note_put(&n, " M+2fc ");
    note_dec(&n, (int32_t)state->last_alt);
    note_put(&n, " record ");
    note_hex(&n, state->last_types, 4);
    note_put(&n, "/");
    note_hex(&n, state->last_to_write, 4);
    /* Per frame, the newest 64: drain order, burst sequence, rate index,
     * result (0 written, 100 slot gone, - error, ? never reached the
     * writer). */
    note_put(&n, "\nframes (order:sequence:index:result)");
    uint64_t block = plugin_block();
    uint32_t first = state->frames > 64u ? state->frames - 64u : 0u;
    for (uint32_t order = first, k = 0; order < state->frames; order++, k++) {
        volatile struct frame_rec *f = frame_at(block, order % FRAME_MAX);
        if (f->order != order)
            continue;
        note_put(&n, (k & 7u) ? " " : "\n ");
        note_dec(&n, (int32_t)f->order);
        note_put(&n, ":");
        note_dec(&n, (int32_t)f->sequence);
        note_put(&n, ":");
        note_dec(&n, (int32_t)f->index);
        note_put(&n, ":");
        if (f->state == FRAME_DONE)
            note_dec(&n, f->result);
        else
            note_put(&n, "?");
    }
    note_put(&n, "\nms ");
    note_dec(&n, (int32_t)api->time_ms());
    note_put(&n, "\n");
    /* Always the whole buffer: a file opened again is written over from
     * the start and not shortened, so a shorter note would keep the end
     * of the one before it. */
    while (n.length < sizeof n.bytes - 1u)
        n.bytes[n.length++] = ' ';
    n.bytes[sizeof n.bytes - 1u] = '\n';
    int32_t fd = note_open(state);
    if (fd < 0)
        return;
    for (uint32_t done = 0; done < sizeof n.bytes;) {
        uint32_t written = 0;
        fw_dcache_clean((void *)(n.bytes + done), sizeof n.bytes - done);
        if (fw_file_write(fd, n.bytes + done, sizeof n.bytes - done,
                          &written) != 0 || !written)
            break;
        done += written;
    }
    fw_file_close(fd);
}

/* Twice a second: a kept slot whose JPEG has not come within a minute is
 * given back unwritten -- the ring is the camera's -- and, with "debug",
 * the note once a burst and its writing have been quiet for three
 * seconds. */
static void heraw_task(void *arg)
{
    const struct nlr_api *api = arg;
    volatile struct heraw_state *state = state_of();
    if (!state)
        return;
    uint32_t quiet_mark = 0, quiet_polls = 0, beat_mark = 0;
    for (;;) {
        api->sleep_ms(500);
        uint64_t block = plugin_block();
        uint32_t now_ms = api->time_ms();
        uint32_t beat = state->ring_beat;
        if (beat != beat_mark) {
            beat_mark = beat;
            state->ring_quiet = 0;
        } else if (state->ring_quiet < 1000u) {
            state->ring_quiet++;
        }
        for (uint32_t k = 0; k < FRAME_MAX; k++) {
            volatile struct frame_rec *f = frame_at(block, k);
            if (!f->kept || f->state != FRAME_RECORDED)
                continue;
            if (!f->kept_ms) {
                f->kept_ms = now_ms ? now_ms : 1u;
            } else if (now_ms - f->kept_ms > KEEP_LIMIT_MS &&
                       frame_claim(f, FRAME_DONE)) {
                f->result = FRAME_SLOT_GONE;
                frame_release(f, &state->given_up);
                state->activity++;
            }
        }
        if (!(state->tests & TEST_DEBUG))
            continue;
        uint32_t now = state->activity;
        if (now != quiet_mark) {
            quiet_mark = now;
            quiet_polls = 0;
        } else if (now != state->noted && ++quiet_polls >= 6u) {
            state->noted = now;
            write_note(api, state);
        }
    }
}

/* ---- install ------------------------------------------------------------ */

static int word_at(const char *buffer, uint32_t got, const char *word,
                   uint32_t length)
{
    for (uint32_t at = 0; at + length <= got; at++) {
        uint32_t n = 0;
        while (n < length && buffer[at + n] == word[n])
            n++;
        if (n == length)
            return 1;
    }
    return 0;
}

static uint32_t read_tests(void)
{
    char buffer[64];
    int32_t fd = fw_file_open(CONFIG, FILE_READ, 0);
    if (fd < 0)
        return 0;
    uint32_t got = 0;
    int32_t result = fw_file_read(fd, buffer, sizeof buffer, &got);
    fw_file_close(fd);
    if (result != 0 || !got)
        return 0;
    if (got > sizeof buffer)
        got = sizeof buffer;
    uint32_t tests = 0;
    if (word_at(buffer, got, "debug", 5u))
        tests |= TEST_DEBUG;
    if (word_at(buffer, got, "jpeg", 4u))
        tests |= TEST_JPEG;
    if (word_at(buffer, got, "emulate", 7u))
        tests |= TEST_EMULATE;
    return tests;
}

uint32_t hsfc_tests(void)
{
    volatile struct heraw_state *state = state_of();
    return state ? state->tests : 0u;
}

static int site_is(uint64_t site, const uint32_t expect[4])
{
    for (uint32_t n = 0; n < 4u; n++)
        if (rd32(site + n * 4u) != expect[n])
            return 0;
    return 1;
}

static const uint32_t SUBMIT_EXPECT[4] = {
    SUBMIT_STOCK_0, SUBMIT_STOCK_1, SUBMIT_STOCK_2, SUBMIT_STOCK_3
};
static const uint32_t WRITE_EXPECT[4] = {
    WRITE_FILE_STOCK_0, WRITE_FILE_STOCK_1, WRITE_FILE_STOCK_2,
    WRITE_FILE_STOCK_3
};
static const uint32_t POOL_EXPECT[4] = {
    POOL_FREE_STOCK_0, POOL_FREE_STOCK_1, POOL_FREE_STOCK_2,
    POOL_FREE_STOCK_3
};

static const uint32_t ALLOC_EXPECT[4] = {
    RING_ALLOC_STOCK_0, RING_ALLOC_STOCK_1, RING_ALLOC_STOCK_2,
    RING_ALLOC_STOCK_3
};
static const uint32_t TDEC_EXPECT[4] = {
    TDEC_SETUP_STOCK_0, TDEC_SETUP_STOCK_1, TDEC_SETUP_STOCK_2,
    TDEC_SETUP_STOCK_3
};
static const uint32_t RATES_EXPECT[RATE_COUNT] = {
    10u, 20u, 30u, 40u, 50u, 60u, 70u, 15u, 25u
};

/* The rate chain of z63fw.h as this build knows it: both tables, the two
 * places that pick index 2, the calls the hooks know their callers by --
 * each lane start latching before it allocates -- and the stock slots. */
static int rates_fingerprint(void)
{
    for (uint32_t n = 0; n < RATE_COUNT; n++)
        if (rd32(TENC_RATES + n * 4u) != RATES_EXPECT[n] ||
            rd32(TDEC_RATES + n * 4u) != RATES_EXPECT[n])
            return 0;
    for (uint32_t mode = 0; mode < SLOT_MODES; mode++)
        if (rd32(SLOT_SIZES + mode * 4u) !=
            (mode == 1u ? SLOT_DX_STOCK : SLOT_FX_STOCK))
            return 0;
    return rd32(LANE_INDEX_SITE) == LANE_INDEX_STOCK &&
           rd32(TDEC_INDEX_SITE) == TDEC_INDEX_STOCK &&
           rd32(TDEC_RING_CALL) == TDEC_RING_CALL_STOCK &&
           rd32(LANE_LATCH_CALL_A) == LANE_LATCH_A_STOCK &&
           rd32(LANE_ALLOC_CALL_A) == LANE_ALLOC_A_STOCK &&
           rd32(LANE_LATCH_CALL_B) == LANE_LATCH_B_STOCK &&
           rd32(LANE_ALLOC_CALL_B) == LANE_ALLOC_B_STOCK &&
           site_is(RING_ALLOC_SITE, ALLOC_EXPECT) &&
           site_is(TDEC_SETUP_SITE, TDEC_EXPECT);
}

int hsfc_fingerprint(void)
{
    return site_is(SUBMIT_SITE, SUBMIT_EXPECT) &&
           site_is(WRITE_FILE_SITE, WRITE_EXPECT) &&
           site_is(POOL_FREE_SITE, POOL_EXPECT) && rates_fingerprint();
}

int hsfc_install(const struct nlr_api *api, uint64_t block)
{
    volatile struct heraw_state *state =
        (volatile struct heraw_state *)(uintptr_t)(block + OFF_STATE);
    for (uint32_t n = 0; n < sizeof *state; n += 4u)
        *(volatile uint32_t *)((uintptr_t)state + n) = 0;
    /* The records start empty: on a body, a fresh block holds whatever the
     * memory held. */
    for (uint32_t n = 0; n < FRAMES_BYTES; n += 4u)
        wr32(block + OFF_FRAMES + n, 0);
    state->tests = read_tests();

    if (api->task_start(heraw_task, (void *)(uintptr_t)api, 24u * 1024u, 0) < 0)
        return -NLR_API_EBUSY;
    /* The pool free first: it keeps nothing until a frame is recorded, and
     * a frame recorded with no one to keep its slot would only lose it. The
     * writer next; then TDEC before the lane start, so no lane gets a
     * tier's rate that its decode would not follow; the submit last. HE RAW
     * can be chosen at any time, so all five stay attached; until then each
     * lets the camera through after a load or two. */
    int result = api->code_hook((void *)(uintptr_t)POOL_FREE_SITE, POOL_EXPECT,
                                (void *)&pool_free_hook);
    state->hook_pool = result;
    if (result != 0)
        return result;
    result = api->code_hook((void *)(uintptr_t)WRITE_FILE_SITE, WRITE_EXPECT,
                            (void *)&write_hook);
    if (result != 0)
        return result;
    result = api->code_hook((void *)(uintptr_t)TDEC_SETUP_SITE, TDEC_EXPECT,
                            (void *)&tdec_hook);
    if (result != 0)
        return result;
    result = api->code_hook((void *)(uintptr_t)RING_ALLOC_SITE, ALLOC_EXPECT,
                            (void *)&alloc_hook);
    if (result != 0)
        return result;
    return api->code_hook((void *)(uintptr_t)SUBMIT_SITE, SUBMIT_EXPECT,
                          (void *)&submit_hook);
}
